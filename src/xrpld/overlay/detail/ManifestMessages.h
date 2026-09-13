#ifndef RIPPLE_OVERLAY_MANIFESTMESSAGES_H_INCLUDED
#define RIPPLE_OVERLAY_MANIFESTMESSAGES_H_INCLUDED

#include <xrpld/app/misc/Manifest.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/overlay/Message.h>

#include <xrpl/basics/random.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ripple {

// Bound both protobuf overhead and attacker-controlled serialized objects.
inline constexpr std::size_t maxManifestBatchCount = 500;
inline constexpr std::size_t maxManifestBatchBytes = 512 * 1024;
// Largest serialized manifest the format can legitimately carry:
//   sfVersion 4 + sfSequence 5 + sfPublicKey 35 + sfSigningPubKey 35 +
//   sfSignature 74 + sfMasterSignature 75 + sfDomain (256 + 3) = 487 bytes.
inline constexpr std::size_t maxManifestBytes = 512;

inline bool
manifestBatchWithinLimits(protocol::TMManifests const& batch)
{
    return batch.list_size() <= maxManifestBatchCount &&
        Message::messageSize(batch) <= maxManifestBatchBytes;
}

// Size-gate a serialized manifest before decoding it. Trust is decided by the
// caller against the validator list; this only rejects oversized input.
inline std::optional<Manifest>
deserializeBoundedManifest(std::string const& bytes)
{
    if (bytes.size() > maxManifestBytes)
        return std::nullopt;
    return deserializeManifest(bytes);
}

// The initial sync for a new peer: every listed key's manifest (revocations
// included), then up to maxUntrusted live manifests from unlisted keys chosen
// at random, all in count- and byte-bounded batches. Revocations of unlisted
// keys are never sent: they tell a peer nothing it can use and are exactly
// what a flood consists of. The greeting therefore grows with the validator
// list and the configured bound, never with the cache.
inline std::vector<std::shared_ptr<Message>>
makeManifestMessages(
    ManifestCache const& cache,
    ValidatorList const& validators,
    std::size_t maxUntrusted)
{
    std::vector<std::shared_ptr<Message>> messages;
    protocol::TMManifests batch;
    auto flush = [&] {
        if (batch.list_size())
        {
            messages.push_back(
                std::make_shared<Message>(batch, protocol::mtMANIFESTS));
            batch.Clear();
        }
    };
    auto append = [&](std::string const& serialized) {
        if (serialized.size() > maxManifestBytes)
            return;
        // Two nested length-delimited protobuf fields need at most 12 bytes
        // of overhead for an object bounded by maxManifestBytes.
        if (batch.list_size() >= maxManifestBatchCount ||
            Message::messageSize(batch) + serialized.size() + 12 >
                maxManifestBatchBytes)
            flush();
        batch.add_list()->set_stobject(serialized);
    };

    // Follow ValidatorList -> ManifestCache lock ordering. Do not enumerate the
    // cache and call back into ValidatorList: that reverses the lock order.
    validators.for_each_listed([&](PublicKey const& key, bool) {
        // A newly connected peer must learn listed-key revocations too.
        if (auto const serialized = cache.getManifestIncludingRevoked(key))
            append(*serialized);
    });

    if (maxUntrusted)
    {
        // Snapshot only master keys under the cache lock; trust checks and
        // payload lookups happen afterwards, so the two locks never nest in
        // reverse and a legacy poisoned cache costs one key copy per entry.
        std::vector<PublicKey> keys;
        cache.for_each_manifest(
            [&keys](std::size_t s) { keys.reserve(s); },
            [&keys](Manifest const& manifest) {
                if (!manifest.revoked())
                    keys.push_back(manifest.masterKey);
            });

        std::vector<PublicKey> unlisted;
        for (auto const& key : keys)
        {
            if (!validators.listed(key))
                unlisted.push_back(key);
        }

        // A random subset, so different peers spread different entries.
        std::shuffle(unlisted.begin(), unlisted.end(), default_prng());
        if (unlisted.size() > maxUntrusted)
            unlisted.erase(unlisted.begin() + maxUntrusted, unlisted.end());

        for (auto const& key : unlisted)
        {
            if (auto const serialized = cache.getManifest(key))
                append(*serialized);
        }
    }

    flush();
    return messages;
}

}  // namespace ripple

#endif
