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
#include <xrpld/app/misc/OrchardWallet.h>
#include <xrpld/app/main/Application.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

// {
//   // No parameters required - uses server-side wallet
// }
Json::Value
doOrchardWalletBalance(RPC::JsonContext& context)
{
    Json::Value result;

    // Get global OrchardWallet instance from Application
    auto& wallet = context.app.getOrchardWallet();

    // Return wallet balance and state
    result[jss::balance] = std::to_string(wallet.getBalance());
    result[jss::note_count] = static_cast<unsigned>(wallet.getNoteCount(false));  // unspent only
    result["spent_note_count"] = static_cast<unsigned>(wallet.getNoteCount(true)) - static_cast<unsigned>(wallet.getNoteCount(false));
    result["last_checkpoint"] = wallet.getLastCheckpoint();
    result["tracked_keys"] = static_cast<unsigned>(wallet.getIncomingViewingKeyCount());

    return result;
}

}  // namespace ripple
