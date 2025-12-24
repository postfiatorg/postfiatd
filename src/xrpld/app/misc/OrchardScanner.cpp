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

#include <xrpld/app/misc/OrchardScanner.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/OrchardBundle.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpld/ledger/ReadView.h>

#include <orchard-postfiat/src/ffi/bridge.rs.h>

namespace ripple {

std::vector<OrchardNote>
scanForOrchardNotes(
    ReadView const& ledger,
    Blob const& fvk_bytes,
    LedgerIndex min_ledger,
    LedgerIndex max_ledger,
    std::optional<uint256> marker,
    std::size_t limit,
    ::NoteManager* note_manager)
{
    std::vector<OrchardNote> result;
    // Map from nullifier to note index in result vector
    // This allows us to mark notes as spent when we encounter their nullifiers
    std::map<uint256, size_t> nullifierToNoteIdx;

    // Validate FVK length
    if (fvk_bytes.size() != 96)
    {
        // Invalid viewing key
        return result;
    }

    // Scan transactions in the current ledger
    // Note: This implementation scans only the provided ledger
    // For scanning multiple ledgers (min_ledger to max_ledger),
    // the caller would need to call this function for each ledger in the range

    // FIRST PASS: Collect all notes that belong to us with their nullifiers
    for (auto const& item : ledger.txs)
    {
        // Check if we've reached the limit
        if (result.size() >= limit)
            break;

        auto const& [sttx, stmeta] = item;
        if (!sttx)
            continue;

        // Check if this is a ShieldedPayment transaction
        if (sttx->getTxnType() != ttSHIELDED_PAYMENT)
            continue;

        // Get transaction hash
        uint256 tx_hash = sttx->getTransactionID();

        // Check if we should skip this based on marker
        if (marker && tx_hash <= *marker)
            continue;

        // Extract OrchardBundle if present
        if (!sttx->isFieldPresent(sfOrchardBundle))
            continue;

        auto const& bundle_blob = sttx->getFieldVL(sfOrchardBundle);
        if (bundle_blob.empty())
            continue;

        try
        {
            // Parse the bundle
            rust::Slice<const uint8_t> bundle_slice{bundle_blob.data(), bundle_blob.size()};
            auto bundle = ::orchard_bundle_parse(bundle_slice);

            // Get number of actions in bundle
            size_t num_actions = ::orchard_bundle_num_actions(*bundle);

            // Try to decrypt each action
            for (size_t action_idx = 0; action_idx < num_actions; ++action_idx)
            {
                // Check limit again
                if (result.size() >= limit)
                    break;

                try
                {
                    rust::Slice<const uint8_t> fvk_slice{fvk_bytes.data(), fvk_bytes.size()};

                    // Get note commitment first (needed for both paths)
                    auto cmx_blob = ::orchard_bundle_get_note_commitments(*bundle);
                    if (cmx_blob.size() < (action_idx + 1) * 32)
                        continue;

                    uint256 cmx;
                    std::memcpy(
                        cmx.data(),
                        cmx_blob.data() + (action_idx * 32),
                        32);

                    std::uint64_t amount = 0;

                    // If we have a NoteManager, decrypt and store the full Note
                    if (note_manager)
                    {
                        try
                        {
                            // Convert tx_hash to array
                            std::array<uint8_t, 32> tx_hash_array;
                            std::memcpy(tx_hash_array.data(), tx_hash.data(), 32);

                            // Decrypt and add to note manager
                            ::orchard_note_manager_decrypt_and_add_note(
                                *note_manager,
                                *bundle,
                                action_idx,
                                fvk_slice,
                                ledger.seq(),
                                tx_hash_array);

                            // Get amount from the note manager (we could also get it from the decryption result)
                            // For now, we'll use the test function to get amount for the result
                            amount = ::orchard_test_try_decrypt_note(*bundle, action_idx, fvk_slice);
                        }
                        catch (...)
                        {
                            // Failed to decrypt - not ours
                            continue;
                        }
                    }
                    else
                    {
                        // No NoteManager - just get the amount for history queries
                        try
                        {
                            amount = ::orchard_test_try_decrypt_note(*bundle, action_idx, fvk_slice);
                        }
                        catch (...)
                        {
                            // Failed to decrypt - not ours
                            continue;
                        }
                    }

                    // Compute the nullifier for this OUTPUT note
                    // Following Zcash's approach: we need this nullifier to later check
                    // if it's revealed in any transaction (which would mean the note is spent)
                    uint256 note_nullifier;
                    try {
                        rust::Slice<const uint8_t> fvk_slice{fvk_bytes.data(), fvk_bytes.size()};
                        auto nullifier_bytes = ::orchard_test_compute_note_nullifier(*bundle, action_idx, fvk_slice);

                        if (nullifier_bytes.size() == 32) {
                            std::memcpy(note_nullifier.data(), nullifier_bytes.data(), 32);
                        } else {
                            // Failed to compute nullifier, skip this note
                            continue;
                        }
                    } catch (...) {
                        // Failed to compute nullifier, skip this note
                        continue;
                    }

                    // Create OrchardNote metadata for result
                    // Initially marked as unspent; will be updated in second pass
                    OrchardNote note;
                    note.cmx = cmx;
                    note.amount = amount;
                    note.ledger_seq = ledger.seq();
                    note.tx_hash = tx_hash;
                    note.spent = false;

                    // Store the note and track its nullifier
                    size_t note_idx = result.size();
                    result.push_back(note);
                    nullifierToNoteIdx[note_nullifier] = note_idx;
                }
                catch (...)
                {
                    // Failed to process this note
                    continue;
                }
            }
        }
        catch (...)
        {
            // Failed to parse bundle or decrypt notes
            continue;
        }
    }

    // SECOND PASS: Check all revealed nullifiers across all transactions
    // to see if any of our notes have been spent
    for (auto const& item : ledger.txs)
    {
        auto const& [sttx, stmeta] = item;
        if (!sttx)
            continue;

        // Check if this is a ShieldedPayment transaction
        if (sttx->getTxnType() != ttSHIELDED_PAYMENT)
            continue;

        // Extract OrchardBundle if present
        if (!sttx->isFieldPresent(sfOrchardBundle))
            continue;

        auto const& bundle_blob = sttx->getFieldVL(sfOrchardBundle);
        if (bundle_blob.empty())
            continue;

        try
        {
            // Parse the bundle
            rust::Slice<const uint8_t> bundle_slice{bundle_blob.data(), bundle_blob.size()};
            auto bundle = ::orchard_bundle_parse(bundle_slice);

            // Check all revealed nullifiers in this bundle
            // Following Zcash's approach: if any revealed nullifier matches
            // a nullifier from our notes, that note has been spent
            auto nullifiers_blob = ::orchard_bundle_get_nullifiers(*bundle);
            size_t num_nullifiers = nullifiers_blob.size() / 32;

            for (size_t i = 0; i < num_nullifiers; ++i)
            {
                uint256 revealed_nullifier;
                std::memcpy(
                    revealed_nullifier.data(),
                    nullifiers_blob.data() + (i * 32),
                    32);

                // Check if this revealed nullifier matches any of our notes
                auto it = nullifierToNoteIdx.find(revealed_nullifier);
                if (it != nullifierToNoteIdx.end())
                {
                    // This nullifier matches one of our notes - mark it as spent
                    result[it->second].spent = true;
                }
            }
        }
        catch (...)
        {
            // Failed to parse bundle
            continue;
        }
    }

    return result;
}

std::uint64_t
calculateOrchardBalance(
    std::vector<OrchardNote> const& notes,
    bool include_spent)
{
    std::uint64_t total = 0;

    for (auto const& note : notes)
    {
        if (!note.spent || include_spent)
        {
            total += note.amount;
        }
    }

    return total;
}

}  // namespace ripple
