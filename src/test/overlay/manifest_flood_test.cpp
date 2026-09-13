#include <test/jtx.h>

#include <xrpld/app/rdb/Wallet.h>
#include <xrpld/core/DatabaseCon.h>
#include <xrpld/overlay/detail/ManifestMessageDiscard.h>
#include <xrpld/overlay/detail/ManifestMessages.h>
#include <xrpld/overlay/detail/ProtocolMessage.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Sign.h>

#include <boost/beast/core/multi_buffer.hpp>
#include <boost/filesystem.hpp>

#include <array>
#include <limits>

namespace ripple {
namespace test {

class manifest_flood_test : public beast::unit_test::suite
{
    static constexpr auto capped = ManifestRateLimitCapPolicy::capped;
    static constexpr auto uncapped = ManifestRateLimitCapPolicy::uncapped;

    static Manifest
    makeManifest(SecretKey const& master, std::uint32_t sequence)
    {
        STObject st(sfGeneric);
        st[sfSequence] = sequence;
        st[sfPublicKey] = derivePublicKey(KeyType::ed25519, master);
        if (!Manifest::revoked(sequence))
        {
            auto const signing = randomKeyPair(KeyType::ed25519);
            st[sfSigningPubKey] = signing.first;
            sign(st, HashPrefix::manifest, KeyType::ed25519, signing.second);
        }
        sign(
            st,
            HashPrefix::manifest,
            KeyType::ed25519,
            master,
            sfMasterSignature);
        Serializer s;
        st.add(s);
        return std::move(*deserializeManifest(s.getString()));
    }

    static Manifest
    clone(Manifest const& m)
    {
        return std::move(*deserializeManifest(m.serialized));
    }

    static std::vector<std::uint8_t>
    header(
        std::uint32_t wire,
        std::uint32_t plain = 0,
        protocol::MessageType type = protocol::mtMANIFESTS)
    {
        std::vector<std::uint8_t> h(plain ? 10 : 6);
        auto pack = [&](int offset, std::uint32_t n) {
            for (int i = 3; i >= 0; --i)
            {
                h[offset + i] = n & 0xFF;
                n >>= 8;
            }
        };
        pack(0, wire);
        h[4] = static_cast<std::uint16_t>(type) >> 8;
        h[5] = static_cast<std::uint16_t>(type) & 0xFF;
        if (plain)
        {
            pack(6, plain);
            h[0] |= 0x90;
        }
        return h;
    }

    // Every manifest carried by a greeting, checking each batch's bounds.
    std::vector<Manifest>
    parseGreeting(std::vector<std::shared_ptr<Message>> const& messages)
    {
        std::vector<Manifest> result;
        for (auto const& message : messages)
        {
            auto const& buffer =
                message->getBuffer(compression::Compressed::Off);
            BEAST_EXPECT(buffer.size() <= maxManifestBatchBytes + 6);
            protocol::TMManifests batch;
            if (!BEAST_EXPECT(
                    batch.ParseFromArray(buffer.data() + 6, buffer.size() - 6)))
                continue;
            BEAST_EXPECT(manifestBatchWithinLimits(batch));
            for (auto const& item : batch.list())
            {
                auto manifest = deserializeBoundedManifest(item.stobject());
                if (BEAST_EXPECT(manifest))
                    result.push_back(std::move(*manifest));
            }
        }
        return result;
    }

    static std::size_t
    cacheSize(ManifestCache const& cache)
    {
        std::size_t n = 0;
        cache.for_each_manifest([&n](Manifest const&) { ++n; });
        return n;
    }

    void
    testIngressAndSync()
    {
        testcase("Listed keys always sync; unlisted keys sync up to the bound");
        jtx::Env env(*this);
        ManifestCache cache;
        ManifestCache publishers;
        ValidatorList validators(
            cache,
            publishers,
            env.timeKeeper(),
            env.app().config().legacy("database_path"),
            env.journal);

        auto const master = randomSecretKey();
        auto const original = makeManifest(master, 1);
        auto const masterKey = original.masterKey;

        // Before the key is listed its manifest is peer gossip: admitted, but
        // counted against the unlisted bound.
        BEAST_EXPECT(!validators.listed(masterKey));
        BEAST_EXPECT(
            cache.applyManifest(clone(original), capped) ==
            ManifestDisposition::accepted);
        BEAST_EXPECT(cache.untrustedCount() == 1);

        // Listing the key frees its slot without touching the manifest.
        BEAST_EXPECT(validators.load(
            {}, {toBase58(TokenType::NodePublic, masterKey)}, {}));
        BEAST_EXPECT(validators.listed(masterKey));
        BEAST_EXPECT(cache.untrustedCount() == 0);
        BEAST_EXPECT(cache.getManifest(masterKey));

        std::vector<std::string> listed;
        for (std::size_t i = 0; i < maxManifestBatchCount; ++i)
        {
            auto m = makeManifest(randomSecretKey(), 1);
            listed.push_back(toBase58(TokenType::NodePublic, m.masterKey));
            BEAST_EXPECT(
                cache.applyManifest(std::move(m), uncapped) ==
                ManifestDisposition::accepted);
        }
        // Unlisted gossip is retained within the bound: a live manifest and
        // a revocation.
        auto const stranger = makeManifest(randomSecretKey(), 1);
        auto const junk = makeManifest(
            randomSecretKey(), std::numeric_limits<std::uint32_t>::max());
        BEAST_EXPECT(
            cache.applyManifest(clone(stranger), capped) ==
            ManifestDisposition::accepted);
        BEAST_EXPECT(
            cache.applyManifest(clone(junk), capped) ==
            ManifestDisposition::accepted);
        BEAST_EXPECT(cache.untrustedCount() == 2);
        BEAST_EXPECT(validators.load({}, listed, {}));

        // With no unlisted budget the greeting is exactly the listed keys.
        {
            auto const messages = makeManifestMessages(cache, validators, 0);
            BEAST_EXPECT(messages.size() == 2);
            auto const sent = parseGreeting(messages);
            BEAST_EXPECT(sent.size() == maxManifestBatchCount + 1);
            for (auto const& manifest : sent)
                BEAST_EXPECT(validators.listed(manifest.masterKey));
        }
        // With a budget the live unlisted entry rides along, after the listed
        // ones; the unlisted revocation is never sent.
        {
            auto const sent =
                parseGreeting(makeManifestMessages(cache, validators, 10));
            BEAST_EXPECT(sent.size() == maxManifestBatchCount + 2);
            std::size_t unlisted = 0;
            for (auto const& manifest : sent)
            {
                if (validators.listed(manifest.masterKey))
                    continue;
                ++unlisted;
                BEAST_EXPECT(manifest.masterKey == stranger.masterKey);
                BEAST_EXPECT(!manifest.revoked());
            }
            BEAST_EXPECT(unlisted == 1);
        }

        auto const rotation = makeManifest(master, 2);
        BEAST_EXPECT(
            cache.applyManifest(clone(rotation), uncapped) ==
            ManifestDisposition::accepted);
        auto const revocation =
            makeManifest(master, std::numeric_limits<std::uint32_t>::max());
        BEAST_EXPECT(
            cache.applyManifest(clone(revocation), uncapped) ==
            ManifestDisposition::accepted);
        BEAST_EXPECT(cache.revoked(masterKey));
        BEAST_EXPECT(!cache.getManifest(masterKey));
        BEAST_EXPECT(cache.getManifestIncludingRevoked(masterKey));

        // A fresh peer must receive a listed revocation, not just live keys.
        // Otherwise it can accept an obsolete signing key after reconnecting.
        ManifestCache fresh;
        std::size_t revocationsSent = 0;
        for (auto& manifest :
             parseGreeting(makeManifestMessages(cache, validators, 0)))
        {
            BEAST_EXPECT(validators.listed(manifest.masterKey));
            if (manifest.revoked())
            {
                ++revocationsSent;
                BEAST_EXPECT(manifest.masterKey == masterKey);
            }
            BEAST_EXPECT(
                fresh.applyManifest(std::move(manifest), uncapped) ==
                ManifestDisposition::accepted);
        }
        BEAST_EXPECT(revocationsSent == 1);
        BEAST_EXPECT(fresh.revoked(masterKey));
        BEAST_EXPECT(
            fresh.applyManifest(clone(rotation), uncapped) ==
            ManifestDisposition::stale);
        BEAST_EXPECT(
            cache.applyManifest(clone(original), uncapped) ==
            ManifestDisposition::stale);
        BEAST_EXPECT(
            !deserializeBoundedManifest(std::string(maxManifestBytes + 1, 'x')));

        // A list change must take effect without any cache sequence change,
        // and the unlisted budget is a hard cap on what a greeting carries.
        ValidatorList empty(
            cache,
            publishers,
            env.timeKeeper(),
            env.app().config().legacy("database_path"),
            env.journal);
        BEAST_EXPECT(makeManifestMessages(cache, empty, 0).empty());
        BEAST_EXPECT(
            parseGreeting(makeManifestMessages(cache, empty, 5)).size() == 5);
        std::size_t live = 0;
        cache.for_each_manifest([&live](Manifest const& manifest) {
            if (!manifest.revoked())
                ++live;
        });
        BEAST_EXPECT(live == cacheSize(cache) - 2);
        auto const everything =
            parseGreeting(makeManifestMessages(cache, empty, 100000));
        BEAST_EXPECT(everything.size() == live);
        for (auto const& manifest : everything)
            BEAST_EXPECT(!manifest.revoked());
    }

    void
    testUntrustedBound()
    {
        testcase("Unlisted manifests are bounded, evicted oldest first");
        jtx::Env env(*this);
        ManifestCache cache(env.journal, 3);

        std::vector<SecretKey> secrets;
        std::vector<Manifest> manifests;
        for (int i = 0; i < 12; ++i)
        {
            secrets.push_back(randomSecretKey());
            manifests.push_back(makeManifest(secrets.back(), 1));
        }
        auto const key = [&](int i) { return manifests[i].masterKey; };
        auto const signing = [&](int i) { return *manifests[i].signingKey; };
        auto const present = [&](int i) {
            return cache.getManifestIncludingRevoked(key(i)).has_value();
        };
        auto const admit = [&](int i) {
            return cache.applyManifest(clone(manifests[i]), capped) ==
                ManifestDisposition::accepted;
        };
        auto const admitFresh = [&] {
            return cache.applyManifest(
                       makeManifest(randomSecretKey(), 1), capped) ==
                ManifestDisposition::accepted;
        };

        auto sequence = cache.sequence();
        for (int i = 0; i < 3; ++i)
        {
            BEAST_EXPECT(admit(i));
            BEAST_EXPECT(cache.sequence() > sequence);
            sequence = cache.sequence();
        }
        BEAST_EXPECT(cache.untrustedCount() == 3);
        BEAST_EXPECT(cacheSize(cache) == 3);

        // The fourth unlisted key evicts the oldest, mapping included.
        BEAST_EXPECT(admit(3));
        BEAST_EXPECT(cache.untrustedCount() == 3);
        BEAST_EXPECT(cacheSize(cache) == 3);
        BEAST_EXPECT(!present(0) && present(1) && present(2) && present(3));
        BEAST_EXPECT(cache.getMasterKey(signing(0)) == signing(0));
        BEAST_EXPECT(cache.getMasterKey(signing(3)) == key(3));
        BEAST_EXPECT(cache.sequence() > sequence);

        // Listed, configured, or database-loaded manifests are never counted
        // and never evicted, however much gossip arrives.
        BEAST_EXPECT(
            cache.applyManifest(clone(manifests[4]), uncapped) ==
            ManifestDisposition::accepted);
        BEAST_EXPECT(cache.untrustedCount() == 3);
        for (int i = 5; i < 8; ++i)
            BEAST_EXPECT(admit(i));
        BEAST_EXPECT(cache.untrustedCount() == 3);
        BEAST_EXPECT(cacheSize(cache) == 4);
        BEAST_EXPECT(present(4));
        BEAST_EXPECT(!present(1) && !present(2) && !present(3));
        BEAST_EXPECT(present(5) && present(6) && present(7));

        // Promotion frees a slot and protects the key from later eviction.
        cache.promoteToTrusted(key(5));
        BEAST_EXPECT(cache.untrustedCount() == 2);
        BEAST_EXPECT(admit(8));
        BEAST_EXPECT(cache.untrustedCount() == 3);
        BEAST_EXPECT(present(5) && present(6) && present(7) && present(8));
        BEAST_EXPECT(admit(9));
        BEAST_EXPECT(cache.untrustedCount() == 3);
        BEAST_EXPECT(present(5) && !present(6));
        BEAST_EXPECT(present(7) && present(8) && present(9));

        // An uncapped update to a counted key frees its slot as well.
        BEAST_EXPECT(admitFresh());
        BEAST_EXPECT(!present(7));
        BEAST_EXPECT(
            cache.applyManifest(makeManifest(secrets[8], 2), uncapped) ==
            ManifestDisposition::accepted);
        BEAST_EXPECT(cache.untrustedCount() == 2);
        for (int i = 0; i < 3; ++i)
            BEAST_EXPECT(admitFresh());
        BEAST_EXPECT(cache.untrustedCount() == 3);
        BEAST_EXPECT(present(8) && !present(9));

        // Unlisted revocations are evictable like any other unlisted entry.
        auto const revoked = makeManifest(
            randomSecretKey(), std::numeric_limits<std::uint32_t>::max());
        BEAST_EXPECT(
            cache.applyManifest(clone(revoked), capped) ==
            ManifestDisposition::accepted);
        BEAST_EXPECT(cache.revoked(revoked.masterKey));
        for (int i = 0; i < 3; ++i)
            BEAST_EXPECT(admitFresh());
        BEAST_EXPECT(!cache.revoked(revoked.masterKey));
        BEAST_EXPECT(cache.untrustedCount() == 3);

        // A stale uncapped sighting (for example the list's own copy of a
        // manifest already known from gossip) frees the slot as well.
        auto const sighted = makeManifest(randomSecretKey(), 1);
        BEAST_EXPECT(
            cache.applyManifest(clone(sighted), capped) ==
            ManifestDisposition::accepted);
        auto const counted = cache.untrustedCount();
        BEAST_EXPECT(
            cache.applyManifest(clone(sighted), uncapped) ==
            ManifestDisposition::stale);
        BEAST_EXPECT(cache.untrustedCount() == counted - 1);
        BEAST_EXPECT(cache.getManifest(sighted.masterKey));

        // A zero bound keeps nothing unlisted, and says so.
        ManifestCache zero(env.journal, 0);
        BEAST_EXPECT(
            zero.applyManifest(clone(manifests[10]), capped) ==
            ManifestDisposition::untrustedCapacity);
        BEAST_EXPECT(!zero.getManifest(key(10)));
        BEAST_EXPECT(
            zero.applyManifest(clone(manifests[10]), uncapped) ==
            ManifestDisposition::accepted);
        BEAST_EXPECT(zero.getManifest(key(10)));
        BEAST_EXPECT(zero.untrustedCount() == 0);
    }

    void
    testUntrustedNotPersisted()
    {
        testcase("Only trusted keys survive a restart on disk");
        jtx::Env env(*this);
        auto const dbPath = boost::filesystem::current_path() /
            "manifest_flood_test_databases";
        boost::filesystem::create_directories(dbPath);
        {
            DatabaseCon::Setup setup;
            setup.dataDir = dbPath;
            auto dbCon =
                makeTestWalletDB(setup, "manifest_flood_test.db", env.journal);

            ManifestCache cache;
            auto const trusted = makeManifest(randomSecretKey(), 1);
            auto const trustedRevoked = makeManifest(
                randomSecretKey(), std::numeric_limits<std::uint32_t>::max());
            BEAST_EXPECT(
                cache.applyManifest(clone(trusted), uncapped) ==
                ManifestDisposition::accepted);
            BEAST_EXPECT(
                cache.applyManifest(clone(trustedRevoked), uncapped) ==
                ManifestDisposition::accepted);
            for (int i = 0; i < 5; ++i)
            {
                // A flood of unlisted revocations must not reach the wallet.
                auto junk = makeManifest(
                    randomSecretKey(),
                    std::numeric_limits<std::uint32_t>::max());
                BEAST_EXPECT(
                    cache.applyManifest(std::move(junk), capped) ==
                    ManifestDisposition::accepted);
            }
            BEAST_EXPECT(cacheSize(cache) == 7);

            cache.save(*dbCon, "ValidatorManifests", [&](PublicKey const& pk) {
                return pk == trusted.masterKey ||
                    pk == trustedRevoked.masterKey;
            });

            ManifestCache loaded;
            loaded.load(*dbCon, "ValidatorManifests");
            BEAST_EXPECT(cacheSize(loaded) == 2);
            BEAST_EXPECT(loaded.getManifest(trusted.masterKey));
            BEAST_EXPECT(loaded.revoked(trustedRevoked.masterKey));
            // Database-loaded entries are uncapped: a legacy poisoned wallet
            // cannot evict listed manifests while it drains.
            BEAST_EXPECT(loaded.untrustedCount() == 0);

            // A configured list publisher is persisted whatever its status,
            // so a publisher revocation survives a restart.
            ManifestCache publishers;
            ValidatorList validators(
                cache,
                publishers,
                env.timeKeeper(),
                env.app().config().legacy("database_path"),
                env.journal);
            auto const publisherKey = derivePublicKey(
                KeyType::ed25519, randomSecretKey());
            // Publisher keys are configured in hex, unlike validator keys.
            BEAST_EXPECT(validators.load({}, {}, {strHex(publisherKey)}));
            BEAST_EXPECT(validators.publisherConfigured(publisherKey));
            BEAST_EXPECT(!validators.publisherConfigured(trusted.masterKey));
        }
        boost::filesystem::remove_all(dbPath);
    }

    void
    testBatchLimits()
    {
        testcase("300000-entry flood rejected before processing");
        auto revocation = makeManifest(
            randomSecretKey(), std::numeric_limits<std::uint32_t>::max());
        protocol::TMManifests batch;
        for (int i = 0; i < 300000; ++i)
            batch.add_list()->set_stobject(revocation.serialized);
        BEAST_EXPECT(!manifestBatchWithinLimits(batch));
        batch.Clear();
        for (std::size_t i = 0; i < maxManifestBatchCount; ++i)
            batch.add_list()->set_stobject(revocation.serialized);
        BEAST_EXPECT(manifestBatchWithinLimits(batch));
        batch.add_list()->set_stobject(revocation.serialized);
        BEAST_EXPECT(!manifestBatchWithinLimits(batch));
        batch.Clear();
        batch.add_list()->set_stobject(std::string(maxManifestBatchBytes, 'x'));
        BEAST_EXPECT(!manifestBatchWithinLimits(batch));
        // A padded manifest is refused before it is decoded.
        BEAST_EXPECT(revocation.serialized.size() <= maxManifestBytes);
        BEAST_EXPECT(deserializeBoundedManifest(revocation.serialized));
        BEAST_EXPECT(!deserializeBoundedManifest(
            revocation.serialized + std::string(maxManifestBytes, 'x')));
    }

    struct Handler
    {
        bool called = false;
        std::vector<std::uint32_t> pingSequences;
        void
        onMessage(std::shared_ptr<protocol::TMPing> const& ping)
        {
            called = true;
            pingSequences.push_back(ping->seq());
        }
        bool
        compressionEnabled() const
        {
            return true;
        }
        template <class... Args>
        void
        onMessageBegin(Args const&...)
        {
            called = true;
        }
        template <class... Args>
        void
        onMessage(Args const&...)
        {
            called = true;
        }
        template <class... Args>
        void
        onMessageEnd(Args const&...)
        {
            called = true;
        }
        void
        onMessageUnknown(std::uint16_t)
        {
            called = true;
        }
    };

    void
    testFrameLimit()
    {
        testcase("Release-build 26-bit boundary checked before serialization");
        protocol::TMManifests batch;
        // At this length the two protobuf wrappers occupy ten bytes.
        batch.add_list()->set_stobject(
            std::string(maximiumMessageSize - 11, 'x'));
        BEAST_EXPECT(Message::messageSize(batch) == maximiumMessageSize - 1);
        {
            Message valid(batch, protocol::mtMANIFESTS);
            auto const& b = valid.getBuffer(compression::Compressed::Off);
            BEAST_EXPECT((b[0] & 0xFC) == 0);
        }
        batch.mutable_list(0)->mutable_stobject()->push_back('x');
        BEAST_EXPECT(Message::messageSize(batch) == maximiumMessageSize);
        Message invalid(batch, protocol::mtMANIFESTS);
        BEAST_EXPECT(invalid.empty());
        BEAST_EXPECT(invalid.getBufferSize() == 0);
        BEAST_EXPECT(invalid.getBuffer(compression::Compressed::Off).empty());
        BEAST_EXPECT(invalid.getBuffer(compression::Compressed::On).empty());
        BEAST_EXPECT(invalid.getBuffer(compression::Compressed::On).empty());
        batch.mutable_list(0)->mutable_stobject()->push_back('x');
        Message excessive(batch, protocol::mtMANIFESTS);
        BEAST_EXPECT(excessive.empty());
        BEAST_EXPECT(excessive.getBuffer(compression::Compressed::On).empty());
        // Compressed input can express a 64 MiB decompressed length. Reject
        // before allocation/parsing, so relaying cannot throw at the boundary.
        auto const h =
            header(1024, maximiumMessageSize, protocol::mtTRANSACTION);
        Handler handler;
        std::size_t hint = 0;
        auto const result =
            invokeProtocolMessage(boost::asio::buffer(h), handler, hint);
        BEAST_EXPECT(
            result.second ==
            make_error_code(boost::system::errc::message_size));
        BEAST_EXPECT(!handler.called && result.first == 0);
    }

    void
    testLegacyDrain()
    {
        testcase("Bounded legacy drain preserves subsequent frames");
        for (bool compressed : {false, true})
        {
            ManifestMessageDiscard drain;
            std::uint32_t const wire = compressed ? 1024 : (1u << 26) + 1024;
            auto h = header(wire, compressed ? (1u << 26) + 1024 : 0);
            for (std::size_t n = 1; n < h.size(); ++n)
            {
                auto const r = drain.consume(boost::asio::buffer(h.data(), n));
                BEAST_EXPECT(r.needHeader && !r.consumed && !r.reject);
            }
            auto const r = drain.consume(boost::asio::buffer(h));
            BEAST_EXPECT(r.started && r.consumed == h.size() && !r.reject);
            // Drain with a fixed 4 KiB buffer, never a wire-sized allocation.
            std::array<std::uint8_t, 4096> chunk{};
            std::size_t remaining = wire;
            while (remaining > chunk.size())
            {
                auto const part = drain.consume(boost::asio::buffer(chunk));
                if (!BEAST_EXPECT(
                        part.consumed == chunk.size() && !part.reject))
                    return;
                remaining -= part.consumed;
            }
            protocol::TMPing ping;
            ping.set_type(protocol::TMPing::ptPING);
            ping.set_seq(17);
            Message message(ping, protocol::mtPING);
            auto const& pingBytes =
                message.getBuffer(compression::Compressed::Off);
            std::vector<std::uint8_t> tail(remaining, 0);
            tail.insert(tail.end(), pingBytes.begin(), pingBytes.end());
            ping.set_seq(18);
            Message second(ping, protocol::mtPING);
            auto const& secondBytes =
                second.getBuffer(compression::Compressed::Off);
            tail.insert(tail.end(), secondBytes.begin(), secondBytes.end());
            auto const last = drain.consume(boost::asio::buffer(tail));
            BEAST_EXPECT(last.consumed == remaining && !last.reject);
            auto const next = boost::asio::buffer(
                tail.data() + remaining, tail.size() - remaining);
            BEAST_EXPECT(!drain.consume(next).consumed);
            // Exercise the real parser, not just the header: TCP may deliver
            // the final dump bytes and multiple following frames together.
            Handler handler;
            std::size_t hint = 0;
            auto const firstResult = invokeProtocolMessage(
                std::array<boost::asio::const_buffer, 1>{next}, handler, hint);
            BEAST_EXPECT(!firstResult.second);
            BEAST_EXPECT(firstResult.first == pingBytes.size());
            auto const secondResult = invokeProtocolMessage(
                std::array<boost::asio::const_buffer, 1>{
                    next + firstResult.first},
                handler,
                hint);
            BEAST_EXPECT(!secondResult.second);
            BEAST_EXPECT(secondResult.first == secondBytes.size());
            BEAST_EXPECT(
                handler.pingSequences == std::vector<std::uint32_t>({17, 18}));
            BEAST_EXPECT(drain.consume(boost::asio::buffer(h)).reject);
        }

        // The largest 28-bit legacy frame still drains; a compressed dump
        // that claims to expand past that limit is rejected.
        auto largest = header((1u << 28) - 1);
        ManifestMessageDiscard atLimit;
        BEAST_EXPECT(atLimit.consume(boost::asio::buffer(largest)).started);
        auto excessive = header(1024, 1u << 28);
        ManifestMessageDiscard tooLarge;
        BEAST_EXPECT(tooLarge.consume(boost::asio::buffer(excessive)).reject);
        auto nonManifest = header((1u << 26) + 1, 0, protocol::mtTRANSACTION);
        ManifestMessageDiscard other;
        BEAST_EXPECT(!other.consume(boost::asio::buffer(nonManifest)).consumed);
        boost::system::error_code ec;
        BEAST_EXPECT(!detail::parseMessageHeader(
            ec, boost::asio::buffer(nonManifest), nonManifest.size()));
        BEAST_EXPECT(ec);

        ManifestMessageDiscard timeout;
        auto const now = std::chrono::steady_clock::now();
        auto h = header(maxManifestBatchBytes + 1);
        BEAST_EXPECT(timeout.consume(boost::asio::buffer(h), now).started);
        std::array<std::uint8_t, 1> byte{};
        BEAST_EXPECT(!timeout
                          .consume(
                              boost::asio::buffer(byte),
                              now + std::chrono::seconds(299))
                          .reject);
        BEAST_EXPECT(
            timeout
                .consume(
                    boost::asio::buffer(byte), now + std::chrono::seconds(300))
                .reject);
    }

public:
    void
    run() override
    {
        testIngressAndSync();
        testUntrustedBound();
        testUntrustedNotPersisted();
        testBatchLimits();
        testFrameLimit();
        testLegacyDrain();
    }
};

BEAST_DEFINE_TESTSUITE(manifest_flood, overlay, ripple);

}  // namespace test
}  // namespace ripple
