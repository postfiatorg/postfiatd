#ifndef RIPPLE_OVERLAY_OBJECTBYHASHLIMITS_H_INCLUDED
#define RIPPLE_OVERLAY_OBJECTBYHASHLIMITS_H_INCLUDED

#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/ReduceRelayCommon.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/messages.h>

#include <cstddef>
#include <cstdint>

namespace ripple {

inline constexpr int maxObjectByHashRequestObjects = 4096;
inline constexpr std::size_t maxObjectByHashReplyBytes = 8 * 1024 * 1024;

inline bool
objectByHashRequestWithinLimits(protocol::TMGetObjectByHash const& request)
{
    // Reduced-relay peers legitimately request the existing 10,000-hash
    // transaction queue in one message. Preserve that protocol limit.
    auto const limit =
        request.type() == protocol::TMGetObjectByHash::otTRANSACTIONS
        ? reduce_relay::MAX_TX_QUEUE_SIZE
        : maxObjectByHashRequestObjects;
    return request.objects_size() <= limit;
}

// Account for exact protobuf wire bytes BEFORE copying any object data or
// peer-controlled index into the response. Work and reply storage are bounded
// independently; a repeated hash must not amplify a small query without limit.
class ObjectByHashReplyBudget
{
    protocol::TMGetObjectByHash& reply_;
    std::size_t bytes_;

    static std::size_t
    varintBytes(std::uint64_t value)
    {
        std::size_t n = 1;
        while (value >= 128)
        {
            value >>= 7;
            ++n;
        }
        return n;
    }

    static std::size_t
    fieldTagBytes(int field, unsigned wireType)
    {
        return varintBytes((std::uint64_t(field) << 3) | wireType);
    }

    static std::size_t
    blobBytes(int field, std::size_t size)
    {
        return fieldTagBytes(field, 2) + varintBytes(size) + size;
    }

public:
    explicit ObjectByHashReplyBudget(protocol::TMGetObjectByHash& reply)
        : reply_(reply), bytes_(Message::messageSize(reply))
    {
    }

    // Do not modify reply_ outside this builder after constructing it.
    bool
    append(protocol::TMIndexedObject const& requested, Slice const data)
    {
        if (!requested.has_hash() || requested.hash().size() != 32 ||
            bytes_ > maxObjectByHashReplyBytes ||
            reply_.objects_size() >= maxObjectByHashRequestObjects)
            return false;

        auto const remaining = maxObjectByHashReplyBytes - bytes_;
        auto const indexSize =
            requested.has_nodeid() ? requested.nodeid().size() : 0;
        // Subtraction checks also bound arithmetic before adding wire overhead.
        if (data.size() > remaining || indexSize > remaining - data.size())
            return false;

        auto objectBytes =
            blobBytes(protocol::TMIndexedObject::kHashFieldNumber, 32) +
            blobBytes(protocol::TMIndexedObject::kDataFieldNumber, data.size());
        if (requested.has_nodeid())
            objectBytes += blobBytes(
                protocol::TMIndexedObject::kIndexFieldNumber, indexSize);
        if (requested.has_ledgerseq())
            objectBytes +=
                fieldTagBytes(
                    protocol::TMIndexedObject::kLedgerSeqFieldNumber, 0) +
                varintBytes(requested.ledgerseq());

        auto const encodedBytes = blobBytes(
            protocol::TMGetObjectByHash::kObjectsFieldNumber, objectBytes);
        if (encodedBytes > remaining)
            return false;

        auto& object = *reply_.add_objects();
        object.set_hash(requested.hash());
        if (data.empty())
            object.set_data("");
        else
            object.set_data(data.data(), data.size());
        if (requested.has_nodeid())
            object.set_index(requested.nodeid());
        if (requested.has_ledgerseq())
            object.set_ledgerseq(requested.ledgerseq());
        bytes_ += encodedBytes;
        return true;
    }
};

}  // namespace ripple

#endif
