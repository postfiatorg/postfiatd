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

            // Add found notes to result
            notes.insert(notes.end(), ledger_notes.begin(), ledger_notes.end());
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
