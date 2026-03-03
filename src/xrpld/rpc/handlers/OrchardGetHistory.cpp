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
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/OrchardScanner.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

#include <map>

namespace ripple {

// Get Orchard transaction history for an address
// {
//   full_viewing_key: <hex-string>         // 96 bytes (192 hex chars)
//   ledger_index_min: <optional-uint>      // Default -1 (earliest)
//   ledger_index_max: <optional-uint>      // Default -1 (latest)
//   limit: <optional-uint>                 // Max transactions to return (default 50)
//   marker: <optional-object>              // For pagination
//   include_raw: <optional-bool>           // Include raw transaction data (default false)
// }
Json::Value
doOrchardGetHistory(RPC::JsonContext& context)
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

    // Parse ledger range
    LedgerIndex min_ledger = 0;
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

    // Validate range
    if (min_ledger > max_ledger)
    {
        result[jss::error] = "invalidParams";
        result[jss::error_message] = "ledger_index_min must be <= ledger_index_max";
        return result;
    }

    // Parse limit
    std::size_t limit = 50;  // Default limit
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

        // Cap limit at 200
        if (limit > 200)
            limit = 200;
    }

    // Parse include_raw flag
    bool include_raw = false;
    if (context.params.isMember(jss::include_raw))
    {
        if (!context.params[jss::include_raw].isBool())
            return RPC::expected_field_error(jss::include_raw, "boolean");
        include_raw = context.params[jss::include_raw].asBool();
    }

    // Parse marker (optional - for pagination)
    std::optional<uint256> marker;
    if (context.params.isMember(jss::marker))
    {
        if (!context.params[jss::marker].isObject())
            return RPC::expected_field_error(jss::marker, "object");

        // Marker contains ledger index and transaction index
        auto const& marker_obj = context.params[jss::marker];
        if (!marker_obj.isMember(jss::ledger) || !marker_obj.isMember(jss::seq))
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "marker must contain 'ledger' and 'seq' fields";
            return result;
        }
    }

    try
    {
        // Use the OrchardScanner to find notes in the current ledger
        auto notes = scanForOrchardNotes(*ledger, *fvk_blob, min_ledger, max_ledger, marker, limit);

        Json::Value transactions(Json::arrayValue);

        // Group notes by transaction
        std::map<uint256, std::vector<OrchardNote>> notes_by_tx;
        for (auto const& note : notes)
        {
            notes_by_tx[note.tx_hash].push_back(note);
        }

        // Build transaction array
        for (auto const& [tx_hash, tx_notes] : notes_by_tx)
        {
            Json::Value tx_info;
            tx_info[jss::hash] = to_string(tx_hash);

            // Use the ledger sequence from the first note
            if (!tx_notes.empty())
            {
                tx_info[jss::ledger_index] = tx_notes[0].ledger_seq;
            }

            // Add notes array
            Json::Value notes_array(Json::arrayValue);
            std::uint64_t total_amount = 0;

            for (auto const& note : tx_notes)
            {
                Json::Value note_info;
                note_info["note_commitment"] = to_string(note.cmx);
                note_info[jss::amount] = std::to_string(note.amount);
                note_info["spent"] = note.spent;

                notes_array.append(note_info);
                total_amount += note.amount;
            }

            tx_info["notes"] = notes_array;
            tx_info["note_count"] = static_cast<Json::UInt>(tx_notes.size());
            tx_info["total_amount"] = std::to_string(total_amount);

            if (include_raw)
            {
                // TODO: Include raw transaction data
                tx_info[jss::warning] = "include_raw not yet fully implemented";
            }

            transactions.append(tx_info);
        }

        result[jss::transactions] = transactions;
        result[jss::count] = static_cast<Json::UInt>(transactions.size());

        Json::Value ledger_range;
        ledger_range[jss::min] = min_ledger;
        ledger_range[jss::max] = max_ledger;
        result[jss::ledger_range] = ledger_range;

        result[jss::warning] = "Currently scanning only the current ledger. "
                                "For multi-ledger scanning, call this RPC for each ledger in range.";

        return result;
    }
    catch (std::exception const& e)
    {
        result[jss::error] = "internal";
        result[jss::error_message] = std::string("Failed to get history: ") + e.what();
        return result;
    }
}

}  // namespace ripple
