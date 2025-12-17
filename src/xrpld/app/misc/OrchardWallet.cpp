//------------------------------------------------------------------------------
/*
    This file is part of postfiatd
    Copyright (c) 2024 PostFiat Developers

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

#include <xrpld/app/misc/OrchardWallet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>

#include <orchard-postfiat/src/ffi/bridge.rs.h>

namespace ripple {

OrchardWallet::OrchardWallet() : state_(::orchard_wallet_state_new())
{
}

OrchardWallet::~OrchardWallet() = default;

OrchardWallet::OrchardWallet(OrchardWallet&&) noexcept = default;

OrchardWallet&
OrchardWallet::operator=(OrchardWallet&&) noexcept = default;

bool
OrchardWallet::addIncomingViewingKey(Blob const& ivk_bytes)
{
    if (ivk_bytes.size() != 64)
    {
        return false;
    }

    try
    {
        rust::Slice<const uint8_t> ivk_slice{ivk_bytes.data(), ivk_bytes.size()};
        ::orchard_wallet_state_add_ivk(*state_, ivk_slice);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool
OrchardWallet::removeIncomingViewingKey(Blob const& ivk_bytes)
{
    if (ivk_bytes.size() != 64)
    {
        return false;
    }

    try
    {
        rust::Slice<const uint8_t> ivk_slice{ivk_bytes.data(), ivk_bytes.size()};
        ::orchard_wallet_state_remove_ivk(*state_, ivk_slice);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::size_t
OrchardWallet::getIncomingViewingKeyCount() const
{
    return ::orchard_wallet_state_get_ivk_count(*state_);
}

std::uint64_t
OrchardWallet::getBalance() const
{
    return ::orchard_wallet_state_get_balance(*state_);
}

std::size_t
OrchardWallet::getNoteCount(bool includeSpent) const
{
    return ::orchard_wallet_state_get_note_count(*state_, includeSpent);
}

bool
OrchardWallet::appendCommitment(uint256 const& cmx)
{
    try
    {
        std::array<uint8_t, 32> cmx_array;
        std::memcpy(cmx_array.data(), cmx.data(), 32);
        ::orchard_wallet_state_append_commitment(*state_, cmx_array);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::size_t
OrchardWallet::tryDecryptNotes(
    OrchardBundleWrapper const& bundle,
    uint256 const& txHash,
    std::uint32_t ledgerSeq)
{
    try
    {
        std::array<uint8_t, 32> tx_hash_array;
        std::memcpy(tx_hash_array.data(), txHash.data(), 32);

        return ::orchard_wallet_state_try_decrypt_notes(
            *state_,
            *bundle.getRustBundle(),
            ledgerSeq,
            tx_hash_array);
    }
    catch (...)
    {
        return 0;
    }
}

std::optional<uint256>
OrchardWallet::getAnchor() const
{
    try
    {
        auto anchor_vec = ::orchard_wallet_state_get_anchor(*state_);

        if (anchor_vec.size() != 32)
        {
            return std::nullopt;
        }

        uint256 anchor;
        std::memcpy(anchor.data(), anchor_vec.data(), 32);
        return anchor;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

void
OrchardWallet::markSpent(uint256 const& nullifier)
{
    std::array<uint8_t, 32> nf_array;
    std::memcpy(nf_array.data(), nullifier.data(), 32);
    ::orchard_wallet_state_mark_spent(*state_, nf_array);
}

void
OrchardWallet::checkpoint(std::uint32_t ledgerSeq)
{
    ::orchard_wallet_state_checkpoint(*state_, ledgerSeq);
}

std::uint32_t
OrchardWallet::getLastCheckpoint() const
{
    return ::orchard_wallet_state_last_checkpoint(*state_);
}

void
OrchardWallet::reset()
{
    ::orchard_wallet_state_reset(*state_);
}

OrchardWalletState*
OrchardWallet::getRustState()
{
    return &(*state_);
}

}  // namespace ripple
