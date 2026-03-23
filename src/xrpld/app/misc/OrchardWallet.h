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

#ifndef RIPPLE_APP_MISC_ORCHARDWALLET_H_INCLUDED
#define RIPPLE_APP_MISC_ORCHARDWALLET_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/Blob.h>
#include <xrpl/protocol/OrchardBundle.h>

#include <orchard-postfiat/src/ffi/bridge.rs.h>

#include <memory>
#include <optional>

namespace ripple {

/**
 * @brief Server-side Orchard wallet for note tracking and balance queries
 *
 * This class wraps the Rust OrchardWalletState and provides a C++ interface
 * for managing server-side Orchard wallet state. Following Zcash's design,
 * the wallet state lives in Rust and is exposed via FFI.
 *
 * Key features:
 * - Track incoming viewing keys (IVKs)
 * - Maintain commitment tree for witness computation
 * - Store decrypted notes with metadata
 * - Track spent/unspent status via nullifiers
 * - Checkpoint at ledger boundaries for reorg support
 *
 * Persistence:
 * - Wallet state is serialized as a single blob to disk
 * - Uses Zcash's BridgeTree for automatic witness management
 * - Stored in application data directory
 */
class OrchardWallet
{
public:
    /**
     * @brief Create a new empty wallet
     */
    OrchardWallet();

    /**
     * @brief Destructor
     */
    ~OrchardWallet();

    // Disable copying (use shared_ptr for sharing)
    OrchardWallet(OrchardWallet const&) = delete;
    OrchardWallet&
    operator=(OrchardWallet const&) = delete;

    // Move is allowed
    OrchardWallet(OrchardWallet&&) noexcept;
    OrchardWallet&
    operator=(OrchardWallet&&) noexcept;

    /**
     * @brief Add an incoming viewing key to track
     *
     * @param ivk_bytes The 64-byte incoming viewing key
     * @return true if successful, false if invalid key
     */
    bool
    addIncomingViewingKey(Blob const& ivk_bytes);

    /**
     * @brief Remove an incoming viewing key
     *
     * @param ivk_bytes The 64-byte incoming viewing key to remove
     * @return true if successful, false if invalid key
     */
    bool
    removeIncomingViewingKey(Blob const& ivk_bytes);

    /**
     * @brief Get the number of registered IVKs
     *
     * @return Count of IVKs
     */
    std::size_t
    getIncomingViewingKeyCount() const;

    /**
     * @brief Get the total balance of unspent notes
     *
     * @return Balance in drops
     */
    std::uint64_t
    getBalance() const;

    /**
     * @brief Get the count of notes
     *
     * @param includeSpent If true, include spent notes in count
     * @return Note count
     */
    std::size_t
    getNoteCount(bool includeSpent = false) const;

    /**
     * @brief Append a commitment to the Merkle tree
     *
     * This should be called for ALL commitments in the ledger,
     * not just our notes, to maintain correct witness paths.
     *
     * @param cmx 32-byte note commitment
     * @return true if successful
     */
    bool
    appendCommitment(uint256 const& cmx);

    /**
     * @brief Try to decrypt notes from an Orchard bundle
     *
     * Attempts to decrypt all actions in the bundle using registered IVKs.
     * Successfully decrypted notes are added to the wallet.
     *
     * @param bundle The Orchard bundle to decrypt
     * @param txHash Transaction hash (32 bytes)
     * @param ledgerSeq Ledger sequence where this transaction appears
     * @return Number of notes successfully decrypted and added
     */
    std::size_t
    tryDecryptNotes(
        OrchardBundleWrapper const& bundle,
        uint256 const& txHash,
        std::uint32_t ledgerSeq);

    /**
     * @brief Get the current anchor (Merkle tree root)
     *
     * @return 32-byte anchor, or std::nullopt if tree is empty
     */
    std::optional<uint256>
    getAnchor() const;

    /**
     * @brief Mark a note as spent by its nullifier
     *
     * @param nullifier 32-byte nullifier
     */
    void
    markSpent(uint256 const& nullifier);

    /**
     * @brief Set a checkpoint at a ledger sequence
     *
     * This is used for reorg support - wallet can rewind to checkpoints.
     *
     * @param ledgerSeq Ledger sequence number
     */
    void
    checkpoint(std::uint32_t ledgerSeq);

    /**
     * @brief Get the last checkpoint ledger sequence
     *
     * @return Last checkpoint sequence, or 0 if no checkpoints
     */
    std::uint32_t
    getLastCheckpoint() const;

    /**
     * @brief Reset the wallet (clear all data)
     */
    void
    reset();

    /**
     * @brief Get the underlying Rust wallet state
     *
     * This is exposed for use by the scanner and other low-level operations.
     *
     * @return Pointer to the Rust OrchardWalletState
     */
    OrchardWalletState*
    getRustState();

private:
    rust::Box<OrchardWalletState> state_;
};

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_ORCHARDWALLET_H_INCLUDED
