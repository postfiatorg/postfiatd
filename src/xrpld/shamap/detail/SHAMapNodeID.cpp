//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/shamap/SHAMap.h>
#include <xrpld/shamap/SHAMapNodeID.h>

#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Serializer.h>

#include <algorithm>

namespace ripple {

static uint256 const&
depthMask(unsigned int depth)
{
    enum { mask_size = 65 };

    struct masks_t
    {
        uint256 entry[mask_size];

        masks_t()
        {
            uint256 selector;
            for (int i = 0; i < mask_size - 1; i += 2)
            {
                entry[i] = selector;
                *(selector.begin() + (i / 2)) = 0xF0;
                entry[i + 1] = selector;
                *(selector.begin() + (i / 2)) = 0xFF;
            }
            entry[mask_size - 1] = selector;
        }
    };

    static masks_t const masks;
    return masks.entry[depth];
}

// canonicalize the hash to a node ID for this depth
SHAMapNodeID::SHAMapNodeID(unsigned int depth, uint256 const& hash)
    : id_(hash), depth_(depth)
{
    // Every node ID's depth is stored here, so this is the one place that
    // can stop an out-of-range one: a depth past leafDepth would index
    // depthMask past its last entry and getRawString would narrow it to a
    // byte, silently renaming the node. Clamp rather than throw, since node
    // IDs are built from peer-supplied depths on the ledger data path.
    if (depth_ > SHAMap::leafDepth)
    {
        UNREACHABLE("ripple::SHAMapNodeID::SHAMapNodeID : depth within tree");
        depth_ = SHAMap::leafDepth;
        id_ = id_ & depthMask(depth_);
    }
    XRPL_ASSERT(
        id_ == (id_ & depthMask(depth_)),
        "ripple::SHAMapNodeID::SHAMapNodeID : hash and depth inputs do match");
}

std::string
SHAMapNodeID::getRawString() const
{
    Serializer s(33);
    s.addBitString(id_);
    s.add8(depth_);
    return s.getString();
}

SHAMapNodeID
SHAMapNodeID::getChildNodeID(unsigned int m) const
{
    XRPL_ASSERT(
        m < SHAMap::branchFactor,
        "ripple::SHAMapNodeID::getChildNodeID : valid branch input");

    // A SHAMap has exactly 65 levels, so nodes must not exceed that
    // depth; if they do, this breaks the invariant of never allowing
    // the construction of a SHAMapNodeID at an invalid depth. We assert
    // to catch this in debug builds.
    //
    // We throw (but never assert) if the node is at level 64, since
    // entries at that depth are leaf nodes and have no children and even
    // constructing a child node from them would break the above invariant.
    XRPL_ASSERT(
        depth_ <= SHAMap::leafDepth,
        "ripple::SHAMapNodeID::getChildNodeID : maximum leaf depth");

    if (depth_ >= SHAMap::leafDepth)
        Throw<std::logic_error>(
            "Request for child node ID of " + to_string(*this));

    if (id_ != (id_ & depthMask(depth_)))
        Throw<std::logic_error>("Incorrect mask for " + to_string(*this));

    SHAMapNodeID node{depth_ + 1, id_};
    node.id_.begin()[depth_ / 2] |= (depth_ & 1) ? m : (m << 4);
    return node;
}

[[nodiscard]] std::optional<SHAMapNodeID>
deserializeSHAMapNodeID(void const* data, std::size_t size)
{
    std::optional<SHAMapNodeID> ret;

    if (size == 33)
    {
        unsigned int depth = *(static_cast<unsigned char const*>(data) + 32);
        if (depth <= SHAMap::leafDepth)
        {
            auto const id = uint256::fromVoid(data);

            if (id == (id & depthMask(depth)))
                ret.emplace(depth, id);
        }
    }

    return ret;
}

[[nodiscard]] unsigned int
selectBranch(SHAMapNodeID const& id, uint256 const& hash)
{
    XRPL_ASSERT(
        id.getDepth() < SHAMap::leafDepth,
        "ripple::selectBranch : depth below leaf depth");

    // A depth-64 ID has no nibble left to select: depth / 2 would read one
    // byte past the end of the 32-byte key. Callers must not ask, but clamp
    // anyway so a wrong branch is the worst outcome.
    auto const depth = std::min(id.getDepth(), SHAMap::leafDepth - 1u);
    auto branch = static_cast<unsigned int>(*(hash.begin() + (depth / 2)));

    if (depth & 1)
        branch &= 0xf;
    else
        branch >>= 4;

    XRPL_ASSERT(
        branch < SHAMap::branchFactor, "ripple::selectBranch : maximum result");
    return branch;
}

SHAMapNodeID
SHAMapNodeID::createID(int depth, uint256 const& key)
{
    // The mask is chosen before the constructor runs, so its clamp cannot
    // cover this call: an out-of-range depth would index depthMask's table
    // while evaluating the argument.
    if (depth < 0 || depth > static_cast<int>(SHAMap::leafDepth))
    {
        UNREACHABLE("ripple::SHAMapNodeID::createID : depth within tree");
        depth = SHAMap::leafDepth;
    }
    return SHAMapNodeID(depth, key & depthMask(depth));
}

}  // namespace ripple
