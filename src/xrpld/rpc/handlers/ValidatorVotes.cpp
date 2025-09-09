//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

// {
//   validation_public_key: <validator_public_key_string>,
//   ledger_hash : <ledger>         // optional
//   ledger_index : <ledger_index>  // optional
// }
//
// Returns:
// {
//   vote_count: <number>
// }

Json::Value
doValidatorVotes(RPC::JsonContext& context)
{
    auto& params = context.params;

    // Get the validator public key
    if (!params.isMember(jss::validation_public_key))
        return RPC::missing_field_error("validation_public_key");

    if (!params[jss::validation_public_key].isString())
        return RPC::invalid_field_error("validation_public_key");

    std::string const keyStr = params[jss::validation_public_key].asString();
    
    // Parse the public key
    auto const pubKeyBlob = strUnHex(keyStr);
    if (!pubKeyBlob || pubKeyBlob->empty())
    {
        Json::Value error;
        error[jss::error] = "invalidValidatorPublicKey";
        error[jss::error_message] = "Invalid validator public key format";
        return error;
    }

    std::optional<PublicKey> validatorKey;
    try 
    {
        validatorKey = PublicKey(makeSlice(*pubKeyBlob));
    }
    catch (...)
    {
        Json::Value error;
        error[jss::error] = "invalidValidatorPublicKey";
        error[jss::error_message] = "Invalid validator public key";
        return error;
    }
    
    if (!validatorKey)
    {
        Json::Value error;
        error[jss::error] = "invalidValidatorPublicKey";
        error[jss::error_message] = "Failed to construct validator public key";
        return error;
    }

    // Calculate the account ID from the public key
    AccountID const validatorAccount = calcAccountID(*validatorKey);

    // Get the ledger to query
    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    // Look up the ValidatorVoteStats object
    auto const k = keylet::validatorVoteStats(validatorAccount);
    auto const sle = ledger->read(k);

    Json::Value response;
    
    // Return only the vote count
    if (!sle)
    {
        // No votes recorded for this validator
        response["vote_count"] = 0;
    }
    else
    {
        // Get the vote count
        uint32_t voteCount = sle->getFieldU32(sfVoteCount);
        response["vote_count"] = voteCount;
    }
    
    return response;
}

}  // namespace ripple