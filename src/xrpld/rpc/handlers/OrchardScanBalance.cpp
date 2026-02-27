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

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/app/misc/OrchardScanner.h>
#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

#include <orchard-postfiat/src/ffi/bridge.rs.h>

namespace ripple {

// {
//   full_viewing_key: <hex-string>         // 96 bytes (192 hex chars)
//   ledger_index_min: <optional-uint>      // Default -1 (earliest)
//   ledger_index_max: <optional-uint>      // Default -1 (latest)
//   limit: <optional-uint>                 // Max notes to return
//   marker: <optional-hex>                 // For pagination
// }
Json::Value
doOrchardScanBalance(RPC::JsonContext& context)
{
    Json::Value result;

    // Full viewing key is required
    if (!context.params.isMember(jss::full_viewing_key))
        return RPC::missing_field_error(jss::full_viewing_key);

    if (!context.params[jss::full_viewing_key].isString())
        return RPC::expected_field_error(jss::full_viewing_key, "string");

    // Parse FVK from hex
    auto fvk_hex = context.params[jss::full_viewing_key].asString();
    auto fvk_blob = strUnHex(fvk_hex);

    if (!fvk_blob || fvk_blob->size() != 96)
    {
        result[jss::error] = "invalidParams";
        result[jss::error_message] = "full_viewing_key must be 96 bytes (192 hex characters)";
        return result;
    }

    // Get ledger
    std::shared_ptr<ReadView const> ledger;
    auto ledgerResult = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return ledgerResult;

    // Parse ledger range (simplified for now - defaults to current ledger only)
    LedgerIndex min_ledger = ledger->seq();
    LedgerIndex max_ledger = ledger->seq();

    if (context.params.isMember(jss::ledger_index_min))
    {
        // Following the pattern from AccountTx - accept Int or UInt
        if (!context.params[jss::ledger_index_min].isIntegral())
            return RPC::expected_field_error(jss::ledger_index_min, "unsigned integer");
        if (context.params[jss::ledger_index_min].asInt() < 0)
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "ledger_index_min must be non-negative";
            return result;
        }
        min_ledger = context.params[jss::ledger_index_min].asUInt();
    }

    if (context.params.isMember(jss::ledger_index_max))
    {
        // Following the pattern from AccountTx - accept Int or UInt
        if (!context.params[jss::ledger_index_max].isIntegral())
            return RPC::expected_field_error(jss::ledger_index_max, "unsigned integer");
        if (context.params[jss::ledger_index_max].asInt() < 0)
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "ledger_index_max must be non-negative";
            return result;
        }
        max_ledger = context.params[jss::ledger_index_max].asUInt();
    }

    // Parse limit
    std::size_t limit = 200;  // Default limit
    if (context.params.isMember(jss::limit))
    {
        // Following the pattern from AccountTx - accept Int or UInt
        if (!context.params[jss::limit].isIntegral())
            return RPC::expected_field_error(jss::limit, "unsigned integer");
        if (context.params[jss::limit].asInt() < 0)
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "limit must be non-negative";
            return result;
        }
        limit = context.params[jss::limit].asUInt();

        // Cap limit at 1000
        if (limit > 1000)
            limit = 1000;
    }

    // Parse marker (optional)
    std::optional<uint256> marker;
    if (context.params.isMember(jss::marker))
    {
        if (!context.params[jss::marker].isString())
            return RPC::expected_field_error(jss::marker, "string");

        auto marker_hex = context.params[jss::marker].asString();
        uint256 marker_val;
        if (!marker_val.parseHex(marker_hex))
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "Invalid marker format";
            return result;
        }
        marker = marker_val;
    }

    try
    {
        // Scan ledgers in the specified range for notes
        std::vector<OrchardNote> notes;
        std::map<uint256, size_t> nullifierToNoteIdx;

        // FIRST PASS: Collect all notes and their nullifiers across all ledgers
        for (LedgerIndex seq = min_ledger; seq <= max_ledger && notes.size() < limit; ++seq)
        {
            // Get the ledger at this sequence
            auto scan_ledger = context.ledgerMaster.getLedgerBySeq(seq);
            if (!scan_ledger)
                continue;  // Ledger not available, skip

            // Scan this ledger for notes
            auto ledger_notes = scanForOrchardNotes(
                *scan_ledger,
                *fvk_blob,
                seq,  // min_ledger for this call
                seq,  // max_ledger for this call
                marker,
                limit - notes.size());  // Remaining limit

            // Add found notes to result and track their nullifiers
            for (auto& note : ledger_notes)
            {
                // Compute nullifier for this note so we can check if it's spent later
                // We need to find the transaction and bundle again to compute the nullifier
                for (auto const& item : scan_ledger->txs)
                {
                    auto const& [sttx, stmeta] = item;
                    if (!sttx || sttx->getTransactionID() != note.tx_hash)
                        continue;

                    if (!sttx->isFieldPresent(sfOrchardBundle))
                        continue;

                    auto const& bundle_blob = sttx->getFieldVL(sfOrchardBundle);
                    if (bundle_blob.empty())
                        continue;

                    try
                    {
                        rust::Slice<const uint8_t> bundle_slice{bundle_blob.data(), bundle_blob.size()};
                        auto bundle = ::orchard_bundle_parse(bundle_slice);
                        size_t num_actions = ::orchard_bundle_num_actions(*bundle);

                        // Find the action that matches this note's cmx
                        auto cmx_blob = ::orchard_bundle_get_note_commitments(*bundle);
                        for (size_t action_idx = 0; action_idx < num_actions; ++action_idx)
                        {
                            if (cmx_blob.size() < (action_idx + 1) * 32)
                                continue;

                            uint256 cmx;
                            std::memcpy(cmx.data(), cmx_blob.data() + (action_idx * 32), 32);

                            if (cmx == note.cmx)
                            {
                                // Found the matching action, compute its nullifier
                                rust::Slice<const uint8_t> fvk_slice{fvk_blob->data(), fvk_blob->size()};
                                auto nullifier_bytes = ::orchard_test_compute_note_nullifier(*bundle, action_idx, fvk_slice);

                                if (nullifier_bytes.size() == 32)
                                {
                                    uint256 note_nullifier;
                                    std::memcpy(note_nullifier.data(), nullifier_bytes.data(), 32);
                                    size_t note_idx = notes.size();
                                    nullifierToNoteIdx[note_nullifier] = note_idx;
                                }
                                break;
                            }
                        }
                    }
                    catch (...) {}
                    break;
                }

                notes.push_back(note);
            }
        }

        // SECOND PASS: Check all revealed nullifiers across all ledgers in the range
        for (LedgerIndex seq = min_ledger; seq <= max_ledger; ++seq)
        {
            auto scan_ledger = context.ledgerMaster.getLedgerBySeq(seq);
            if (!scan_ledger)
                continue;

            for (auto const& item : scan_ledger->txs)
            {
                auto const& [sttx, stmeta] = item;
                if (!sttx || sttx->getTxnType() != ttSHIELDED_PAYMENT)
                    continue;

                if (!sttx->isFieldPresent(sfOrchardBundle))
                    continue;

                auto const& bundle_blob = sttx->getFieldVL(sfOrchardBundle);
                if (bundle_blob.empty())
                    continue;

                try
                {
                    rust::Slice<const uint8_t> bundle_slice{bundle_blob.data(), bundle_blob.size()};
                    auto bundle = ::orchard_bundle_parse(bundle_slice);

                    auto nullifiers_blob = ::orchard_bundle_get_nullifiers(*bundle);
                    size_t num_nullifiers = nullifiers_blob.size() / 32;

                    for (size_t i = 0; i < num_nullifiers; ++i)
                    {
                        uint256 revealed_nullifier;
                        std::memcpy(revealed_nullifier.data(), nullifiers_blob.data() + (i * 32), 32);

                        auto it = nullifierToNoteIdx.find(revealed_nullifier);
                        if (it != nullifierToNoteIdx.end())
                        {
                            notes[it->second].spent = true;
                        }
                    }
                }
                catch (...) {}
            }
        }

        // Calculate balance
        auto total_balance = calculateOrchardBalance(notes, false);  // Only unspent
        auto total_with_spent = calculateOrchardBalance(notes, true);

        // Build response
        Json::Value notes_array(Json::arrayValue);
        std::size_t spent_count = 0;

        for (auto const& note : notes)
        {
            Json::Value note_obj;
            note_obj[jss::cmx] = to_string(note.cmx);
            note_obj[jss::amount] = std::to_string(note.amount);
            note_obj[jss::amount_xrp] = std::to_string(note.amount / 1000000.0);
            note_obj[jss::ledger_index] = note.ledger_seq;
            note_obj[jss::tx_hash] = to_string(note.tx_hash);
            note_obj[jss::spent] = note.spent;

            if (note.spent)
                spent_count++;

            notes_array.append(note_obj);
        }

        result[jss::notes] = notes_array;
        result[jss::total_balance] = std::to_string(total_balance);
        result[jss::total_balance_xrp] = std::to_string(total_balance / 1000000.0);
        result[jss::note_count] = static_cast<Json::UInt>(notes.size());
        result[jss::spent_count] = static_cast<Json::UInt>(spent_count);

        Json::Value ledger_range;
        ledger_range[jss::min] = min_ledger;
        ledger_range[jss::max] = max_ledger;
        result[jss::ledger_range] = ledger_range;

        // TODO: Add marker for pagination when notes.size() >= limit

        return result;
    }
    catch (std::exception const& e)
    {
        result[jss::error] = "internal";
        result[jss::error_message] = std::string("Failed to scan balance: ") + e.what();
        return result;
    }
}

}  // namespace ripple
