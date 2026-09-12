#ifndef RIPPLE_OVERLAY_MANIFESTMESSAGES_H_INCLUDED
#define RIPPLE_OVERLAY_MANIFESTMESSAGES_H_INCLUDED

#include <xrpld/app/misc/Manifest.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/overlay/Message.h>

#include <memory>
#include <vector>

namespace ripple {

// Bound both protobuf overhead and attacker-controlled serialized objects.
inline constexpr std::size_t maxManifestBatchCount = 500;
inline constexpr std::size_t maxManifestBatchBytes = 512 * 1024;
inline constexpr std::size_t maxManifestBytes = 4096;

inline bool
manifestBatchWithinLimits(protocol::TMManifests const& batch)
{
    return batch.list_size() <= maxManifestBatchCount &&
        Message::messageSize(batch) <= maxManifestBatchBytes;
}

// Publisher manifests belong to publisherManifests(), and are authenticated by
// ValidatorList's signed-list path, not by this validator-manifest channel.
inline std::optional<Manifest>
deserializeListedManifest(
    std::string const& bytes,
    ValidatorList const& validators)
{
    if (bytes.size() > maxManifestBytes)
        return std::nullopt;
    auto manifest = deserializeManifest(bytes);
    if (!manifest)
        return std::nullopt;
    auto const listed = validators.getListedKey(manifest->masterKey);
    if (!listed || *listed != manifest->masterKey)
        return std::nullopt;
    return manifest;
}

inline std::vector<std::shared_ptr<Message>>
makeManifestMessages(
    ManifestCache const& cache,
    ValidatorList const& validators)
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

    // Follow ValidatorList -> ManifestCache lock ordering. Do not enumerate the
    // cache and call back into ValidatorList: that reverses the lock order.
    // Looking up listed keys also avoids scanning a legacy poisoned cache.
    validators.for_each_listed([&](PublicKey const& key, bool) {
        auto const serialized = cache.getManifest(key);
        if (!serialized || serialized->size() > maxManifestBytes)
            return;
        // Two nested length-delimited protobuf fields need at most 12 bytes
        // of overhead for an object bounded by maxManifestBytes.
        if (batch.list_size() >= maxManifestBatchCount ||
            Message::messageSize(batch) + serialized->size() + 12 >
                maxManifestBatchBytes)
            flush();
        batch.add_list()->set_stobject(*serialized);
    });
    flush();
    return messages;
}

}  // namespace ripple

#endif
