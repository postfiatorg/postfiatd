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

#ifndef RIPPLE_PROTOCOL_ORCHARD_BUNDLE_H_INCLUDED
#define RIPPLE_PROTOCOL_ORCHARD_BUNDLE_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>

#include <cstdint>
#include <optional>
#include <vector>
#include <memory>

// Include the CXX bridge header for Rust types
#include "orchard-postfiat/src/ffi/bridge.rs.h"

namespace ripple {

/**
 * @brief C++ wrapper for Orchard bundle
 *
 * An Orchard bundle contains one or more "actions" where each action
 * represents a spend (consuming a shielded note) and/or an output
 * (creating a new shielded note). The bundle includes Halo2 zero-knowledge
 * proofs that validate the actions without revealing sensitive information.
 *
 * This wrapper provides a safe C++ interface to the Rust Orchard implementation.
 */
class OrchardBundleWrapper
{
private:
    // Opaque pointer to Rust OrchardBundle
    std::unique_ptr<rust::Box<::OrchardBundle>> inner_;

    // Private constructor - use parse() to create instances
    explicit OrchardBundleWrapper(
        std::unique_ptr<rust::Box<::OrchardBundle>> bundle);

public:
    OrchardBundleWrapper() = delete;
    ~OrchardBundleWrapper();

    // Move-only type (contains unique_ptr to Rust data)
    OrchardBundleWrapper(OrchardBundleWrapper&&) noexcept;
    OrchardBundleWrapper& operator=(OrchardBundleWrapper&&) noexcept;
    OrchardBundleWrapper(OrchardBundleWrapper const&) = delete;
    OrchardBundleWrapper& operator=(OrchardBundleWrapper const&) = delete;

    /**
     * @brief Parse an Orchard bundle from serialized bytes
     *
     * @param data The serialized bundle data
     * @return Optional wrapper if parsing succeeds, nullopt otherwise
     */
    static std::optional<OrchardBundleWrapper>
    parse(Slice const& data);

    /**
     * @brief Serialize the bundle to bytes
     *
     * @return Serialized bundle data
     */
    Blob
    serialize() const;

    /**
     * @brief Check if the bundle is present (not empty)
     *
     * @return true if bundle contains actions, false otherwise
     */
    bool
    isPresent() const;

    /**
     * @brief Validate the bundle structure
     *
     * This performs basic structural validation. Full proof verification
     * is done separately via verifyProof().
     *
     * @return true if structure is valid, false otherwise
     */
    bool
    isValid() const;

    /**
     * @brief Get the value balance
     *
     * The value balance represents the net flow of value in/out of the
     * shielded pool:
     * - Positive: value flowing out (z->t unshielding)
     * - Negative: value flowing in (t->z shielding)
     * - Zero: fully shielded (z->z)
     *
     * @return Value balance in drops
     */
    std::int64_t
    getValueBalance() const;

    /**
     * @brief Get the anchor (Merkle tree root)
     *
     * The anchor commits to the state of the Orchard note commitment tree
     * at the time this bundle was created.
     *
     * @return 32-byte anchor value
     */
    uint256
    getAnchor() const;

    /**
     * @brief Get all nullifiers from this bundle
     *
     * Nullifiers are used to prevent double-spending of shielded notes.
     * Each nullifier must be unique across all transactions.
     *
     * @return Vector of 32-byte nullifiers
     */
    std::vector<uint256>
    getNullifiers() const;

    /**
     * @brief Get all note commitments from this bundle
     *
     * Note commitments (cmx) represent the outputs created by this transaction.
     * They are added to the Merkle tree and can be spent in future transactions.
     *
     * @return Vector of 32-byte note commitments
     */
    std::vector<uint256>
    getNoteCommitments() const;

    /**
     * @brief Get encrypted note data for all actions
     *
     * Returns data needed to trial-decrypt notes with a viewing key.
     * Each tuple contains (cmx, ephemeral_key, encrypted_note).
     *
     * @return Vector of (32-byte cmx, 32-byte epk, 580-byte ciphertext) tuples
     */
    struct EncryptedNoteData {
        uint256 cmx;
        Blob ephemeralKey;  // 32 bytes
        Blob encryptedNote; // 580 bytes
    };
    std::vector<EncryptedNoteData>
    getEncryptedNotes() const;

    /**
     * @brief Get the number of actions in this bundle
     *
     * @return Number of actions
     */
    std::size_t
    numActions() const;

    /**
     * @brief Verify the Halo2 proof for this bundle
     *
     * This performs cryptographic verification of the zero-knowledge proof,
     * ensuring all actions in the bundle are valid without revealing
     * sensitive information.
     *
     * @param sighash The transaction signature hash (32 bytes)
     * @return true if proof is valid, false otherwise
     */
    bool
    verifyProof(uint256 const& sighash) const;

    /**
     * @brief Get the raw Rust bundle pointer (for advanced use)
     *
     * @warning This exposes the internal Rust type. Use with caution.
     * @return Reference to the Rust bundle
     */
    rust::Box<::OrchardBundle> const&
    getRustBundle() const;
};

/**
 * @brief Batch verifier for multiple Orchard bundles
 *
 * Batch verification is more efficient than verifying bundles individually.
 * Use this when validating multiple transactions in a block.
 */
class OrchardBatchVerifier
{
private:
    std::unique_ptr<rust::Box<::OrchardBatchVerifier>> inner_;

public:
    OrchardBatchVerifier();
    ~OrchardBatchVerifier();

    // Move-only type
    OrchardBatchVerifier(OrchardBatchVerifier&&) noexcept;
    OrchardBatchVerifier& operator=(OrchardBatchVerifier&&) noexcept;
    OrchardBatchVerifier(OrchardBatchVerifier const&) = delete;
    OrchardBatchVerifier& operator=(OrchardBatchVerifier const&) = delete;

    /**
     * @brief Add a bundle to the batch
     *
     * @param bundle The Orchard bundle to verify
     * @param sighash The transaction signature hash
     */
    void
    add(OrchardBundleWrapper const& bundle, uint256 const& sighash);

    /**
     * @brief Verify all bundles in the batch
     *
     * This performs batch verification of all added bundles.
     *
     * @return true if all proofs are valid, false otherwise
     */
    bool
    verify();
};

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_ORCHARD_BUNDLE_H_INCLUDED
