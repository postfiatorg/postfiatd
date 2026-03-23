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

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

#include <orchard-postfiat/src/ffi/bridge.rs.h>

namespace ripple {

// Get the current Orchard Merkle tree anchor
// This anchor is needed for building Orchard bundles that spend notes
// {
//   ledger_index: <optional-uint>          // Default: current ledger
//   ledger_hash: <optional-hash>           // Alternative to ledger_index
// }
Json::Value
doOrchardGetAnchor(RPC::JsonContext& context)
{
    Json::Value result;

    // Get ledger
    std::shared_ptr<ReadView const> ledger;
    auto ledgerResult = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return ledgerResult;

    try
    {
        // For now, we'll return the empty anchor that's used for t->z transactions
        // In a full implementation, this would:
        // 1. Read the OrchardAnchor ledger object for this ledger
        // 2. Return the current Merkle root
        // 3. Include information about the tree size and last update

        auto anchor = ::orchard_test_get_empty_anchor();

        result[jss::anchor] = strHex(std::string_view(
            reinterpret_cast<const char*>(anchor.data()), anchor.size()));

        result[jss::ledger_index] = ledger->seq();
        result[jss::ledger_hash] = to_string(ledger->info().hash);

        // Additional metadata
        result[jss::tree_size] = 0;  // Placeholder - would be actual tree size
        result[jss::is_empty] = true;  // Currently using empty anchor

        result[jss::warning] = "Currently returning empty anchor. Full anchor tracking "
                               "from OrchardAnchor ledger objects not yet implemented. "
                               "This anchor is only valid for transparent-to-shielded payments.";

        return result;
    }
    catch (std::exception const& e)
    {
        result[jss::error] = "internal";
        result[jss::error_message] = std::string("Failed to get anchor: ") + e.what();
        return result;
    }
}

}  // namespace ripple
