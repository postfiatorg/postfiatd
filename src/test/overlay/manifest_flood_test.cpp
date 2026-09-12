#include <test/jtx.h>

#include <xrpld/overlay/detail/ManifestMessageDiscard.h>
#include <xrpld/overlay/detail/ManifestMessages.h>
#include <xrpld/overlay/detail/ProtocolMessage.h>

#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Sign.h>

#include <boost/beast/core/multi_buffer.hpp>

#include <array>
#include <limits>

namespace ripple {
namespace test {

class manifest_flood_test : public beast::unit_test::suite
{
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

    void
    testIngressAndSync()
    {
        testcase("Unlisted ingress, bounded sync, rotations and revocations");
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
        auto original = makeManifest(master, 1);
        auto const masterKey = original.masterKey;
        BEAST_EXPECT(
            !deserializeListedManifest(original.serialized, validators));
        BEAST_EXPECT(!cache.getManifest(masterKey));
        BEAST_EXPECT(validators.load(
            {}, {toBase58(TokenType::NodePublic, masterKey)}, {}));
        auto accepted =
            deserializeListedManifest(original.serialized, validators);
        BEAST_EXPECT(accepted);
        BEAST_EXPECT(
            cache.applyManifest(std::move(*accepted)) ==
            ManifestDisposition::accepted);

        std::vector<std::string> listed;
        for (std::size_t i = 0; i < maxManifestBatchCount; ++i)
        {
            auto m = makeManifest(randomSecretKey(), 1);
            listed.push_back(toBase58(TokenType::NodePublic, m.masterKey));
            BEAST_EXPECT(
                cache.applyManifest(std::move(m)) ==
                ManifestDisposition::accepted);
        }
        // Legacy cache pollution must not be sent, even though it is retained.
        auto junk = makeManifest(
            randomSecretKey(), std::numeric_limits<std::uint32_t>::max());
        BEAST_EXPECT(
            cache.applyManifest(std::move(junk)) ==
            ManifestDisposition::accepted);
        BEAST_EXPECT(validators.load({}, listed, {}));

        auto messages = makeManifestMessages(cache, validators);
        BEAST_EXPECT(messages.size() == 2);
        std::size_t count = 0;
        for (auto const& message : messages)
        {
            auto const& buffer =
                message->getBuffer(compression::Compressed::Off);
            BEAST_EXPECT(buffer.size() <= maxManifestBatchBytes + 6);
            protocol::TMManifests batch;
            BEAST_EXPECT(
                batch.ParseFromArray(buffer.data() + 6, buffer.size() - 6));
            BEAST_EXPECT(manifestBatchWithinLimits(batch));
            count += batch.list_size();
            for (auto const& item : batch.list())
                BEAST_EXPECT(
                    deserializeListedManifest(item.stobject(), validators));
        }
        BEAST_EXPECT(count == maxManifestBatchCount + 1);

        auto rotation = makeManifest(master, 2);
        auto rotated =
            deserializeListedManifest(rotation.serialized, validators);
        BEAST_EXPECT(rotated);
        BEAST_EXPECT(
            cache.applyManifest(std::move(*rotated)) ==
            ManifestDisposition::accepted);
        auto revocation =
            makeManifest(master, std::numeric_limits<std::uint32_t>::max());
        auto revoked =
            deserializeListedManifest(revocation.serialized, validators);
        BEAST_EXPECT(revoked);
        BEAST_EXPECT(
            cache.applyManifest(std::move(*revoked)) ==
            ManifestDisposition::accepted);
        BEAST_EXPECT(cache.revoked(masterKey));
        BEAST_EXPECT(!cache.getManifest(masterKey));
        BEAST_EXPECT(cache.getManifestIncludingRevoked(masterKey));

        // A fresh peer must receive a listed revocation, not just live keys.
        // Otherwise it can accept an obsolete signing key after reconnecting.
        ManifestCache fresh;
        std::size_t revocationsSent = 0;
        auto const afterRevocation = makeManifestMessages(cache, validators);
        for (auto const& message : afterRevocation)
        {
            auto const& buffer =
                message->getBuffer(compression::Compressed::Off);
            protocol::TMManifests batch;
            if (!BEAST_EXPECT(
                    batch.ParseFromArray(buffer.data() + 6, buffer.size() - 6)))
                return;
            for (auto const& item : batch.list())
            {
                auto manifest =
                    deserializeListedManifest(item.stobject(), validators);
                if (!BEAST_EXPECT(manifest))
                    return;
                if (manifest->revoked())
                {
                    ++revocationsSent;
                    BEAST_EXPECT(manifest->masterKey == masterKey);
                }
                BEAST_EXPECT(
                    fresh.applyManifest(std::move(*manifest)) ==
                    ManifestDisposition::accepted);
            }
        }
        BEAST_EXPECT(revocationsSent == 1);
        BEAST_EXPECT(fresh.revoked(masterKey));
        BEAST_EXPECT(
            fresh.applyManifest(*deserializeManifest(rotation.serialized)) ==
            ManifestDisposition::stale);
        BEAST_EXPECT(
            cache.applyManifest(std::move(original)) ==
            ManifestDisposition::stale);
        BEAST_EXPECT(!deserializeListedManifest(
            std::string(maxManifestBytes + 1, 'x'), validators));

        // A list change must take effect without any cache sequence change.
        ValidatorList empty(
            cache,
            publishers,
            env.timeKeeper(),
            env.app().config().legacy("database_path"),
            env.journal);
        BEAST_EXPECT(makeManifestMessages(cache, empty).empty());
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
            auto const firstResult = invokeProtocolMessage(next, handler, hint);
            BEAST_EXPECT(!firstResult.second);
            BEAST_EXPECT(firstResult.first == pingBytes.size());
            auto const secondResult =
                invokeProtocolMessage(next + firstResult.first, handler, hint);
            BEAST_EXPECT(!secondResult.second);
            BEAST_EXPECT(secondResult.first == secondBytes.size());
            BEAST_EXPECT(
                handler.pingSequences == std::vector<std::uint32_t>({17, 18}));
            BEAST_EXPECT(drain.consume(boost::asio::buffer(h)).reject);
        }

        auto excessive = header(128 * 1024 * 1024 + 1);
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
        BEAST_EXPECT(
            timeout
                .consume(
                    boost::asio::buffer(byte), now + std::chrono::seconds(60))
                .reject);
    }

public:
    void
    run() override
    {
        testIngressAndSync();
        testBatchLimits();
        testFrameLimit();
        testLegacyDrain();
    }
};

BEAST_DEFINE_TESTSUITE(manifest_flood, overlay, ripple);

}  // namespace test
}  // namespace ripple
