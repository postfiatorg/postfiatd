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

#ifndef RIPPLE_APP_MISC_ORCHARDSCANNER_H_INCLUDED
#define RIPPLE_APP_MISC_ORCHARDSCANNER_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/Blob.h>
#include <xrpld/ledger/ReadView.h>

#include <optional>
#include <vector>

// Forward declare NoteManager from FFI (defined in bridge.rs.h)
class NoteManager;

namespace ripple {

/**
 * @brief Represents a single Orchard note found during ledger scanning
 */
struct OrchardNote
{
    uint256 cmx;              // Note commitment (32 bytes)
    std::uint64_t amount;     // Amount in drops
    std::uint32_t ledger_seq; // Ledger sequence where note was created
    uint256 tx_hash;          // Transaction hash that created this note
    bool spent;               // True if nullifier exists in ledger
};


/**
 * @brief Scan ledger for Orchard notes owned by a specific viewing key
 *
 * This function iterates through a range of ledgers, finds all ShieldedPayment
 * transactions, extracts their Orchard bundles, and attempts to decrypt notes
 * using the provided full viewing key. It also checks whether notes have been
 * spent by looking up their nullifiers.
 *
 * If note_manager is provided, the scanner will store full Note objects in it
 * (for wallet use). Otherwise, it only returns metadata (for history queries).
 *
 * @param ledger The ledger view to scan
 * @param fvk_bytes Full viewing key bytes (96 bytes)
 * @param min_ledger Minimum ledger index to scan
 * @param max_ledger Maximum ledger index to scan
 * @param marker Optional starting point for pagination (cmx hash)
 * @param limit Maximum number of notes to return
 * @param note_manager Optional pointer to NoteManager for storing full notes
 *
 * @return Vector of OrchardNote objects owned by this viewing key
 */
std::vector<OrchardNote>
scanForOrchardNotes(
    ReadView const& ledger,
    Blob const& fvk_bytes,
    LedgerIndex min_ledger,
    LedgerIndex max_ledger,
    std::optional<uint256> marker,
    std::size_t limit,
    ::NoteManager* note_manager = nullptr);

/**
 * @brief Calculate total balance from a set of notes
 *
 * @param notes Vector of Orchard notes
 * @param include_spent If true, include spent notes in the balance
 *
 * @return Total balance in drops
 */
std::uint64_t
calculateOrchardBalance(
    std::vector<OrchardNote> const& notes,
    bool include_spent = false);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_ORCHARDSCANNER_H_INCLUDED
