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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ExclusionManager.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/jss.h>
#include <xrpld/consensus/ConsensusParms.h>

namespace ripple {

Json::Value
doExclusionInfo(RPC::JsonContext& context)
{
    auto const& params = context.params;
    Json::Value result;

    // Get the current ledger
    std::shared_ptr<ReadView const> ledger;
    auto res = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return res;

    JLOG(context.j.info()) << "ExclusionInfo: Checking ledger " << ledger->seq();

    // Check if PF_AccountExclusion feature is enabled
    if (!ledger->rules().enabled(featurePF_AccountExclusion))
    {
        RPC::inject_error(rpcNOT_ENABLED, result);
        result[jss::error_message] = "PF_AccountExclusion feature is not enabled";
        return result;
    }

    auto& app = context.app;
    auto& exclusionManager = app.getExclusionManager();
    auto const& validators = app.validators();

    // Check if a specific validator was requested
    if (params.isMember(jss::validator))
    {
        AccountID validatorAccount;

        auto const validator = params[jss::validator].asString();
        if (auto const account = parseBase58<AccountID>(validator))
        {
            validatorAccount = *account;
        }
        else
        {
            RPC::inject_error(rpcACT_MALFORMED, result);
            return result;
        }

        // Get the validator's account
        auto const accountSLE = ledger->read(keylet::account(validatorAccount));
        if (!accountSLE)
        {
            RPC::inject_error(rpcACT_NOT_FOUND, result);
            return result;
        }

        result[jss::validator] = toBase58(validatorAccount);
        Json::Value& exclusionList = (result[jss::exclusion_list] = Json::arrayValue);

        // Get the exclusion list for this validator
        if (accountSLE->isFieldPresent(sfExclusionList))
        {
            STArray const& list = accountSLE->getFieldArray(sfExclusionList);
            for (auto const& entry : list)
            {
                if (entry.isFieldPresent(sfAccount))
                {
                    auto const excludedAccount = entry.getAccountID(sfAccount);
                    Json::Value exclusionEntry;
                    exclusionEntry["address"] = toBase58(excludedAccount);

                    // Add reason information if available from ExclusionManager
                    auto exclusionInfo = exclusionManager.getExclusionInfo(excludedAccount);
                    if (exclusionInfo)
                    {
                        if (!exclusionInfo->reason.empty())
                            exclusionEntry["reason"] = exclusionInfo->reason;
                        if (!exclusionInfo->dateAdded.empty())
                            exclusionEntry["date_added"] = exclusionInfo->dateAdded;
                    }

                    exclusionList.append(exclusionEntry);
                }
            }
        }

        result[jss::exclusion_count] = exclusionList.size();
    }
    else
    {
        // Return information for all validators
        Json::Value& validatorsList = (result[jss::validators] = Json::objectValue);
        Json::Value& excludedAccounts = (result[jss::excluded_accounts] = Json::objectValue);

        std::unordered_map<AccountID, std::size_t> exclusionCounts;
        std::size_t totalValidators = 0;

        // Get list of trusted validators from the UNL
        auto trustedKeys = validators.getTrustedMasterKeys();

        for (auto const& pubKey : trustedKeys)
        {
            AccountID validatorAccount = calcAccountID(pubKey);
            totalValidators++;

            JLOG(context.j.info()) << "ExclusionInfo: Looking for validator with pubKey: "
                                   << toBase58(TokenType::NodePublic, pubKey)
                                   << " -> Account: " << toBase58(validatorAccount);

            // Get the validator's account
            auto const accountSLE = ledger->read(keylet::account(validatorAccount));
            if (!accountSLE)
            {
                JLOG(context.j.info()) << "ExclusionInfo: Account not found for "
                                       << toBase58(validatorAccount);
                continue;
            }
            else
            {
                JLOG(context.j.info()) << "ExclusionInfo: Found account for "
                                       << toBase58(validatorAccount);
            }

            Json::Value& validatorInfo = validatorsList[toBase58(validatorAccount)];
            Json::Value& exclusionList = (validatorInfo[jss::exclusion_list] = Json::arrayValue);

            // Get the exclusion list for this validator
            if (accountSLE->isFieldPresent(sfExclusionList))
            {
                STArray const& list = accountSLE->getFieldArray(sfExclusionList);
                for (auto const& entry : list)
                {
                    if (entry.isFieldPresent(sfAccount))
                    {
                        auto const excludedAccount = entry.getAccountID(sfAccount);
                        exclusionList.append(toBase58(excludedAccount));
                        exclusionCounts[excludedAccount]++;
                    }
                }
            }

            validatorInfo[jss::exclusion_count] = exclusionList.size();
        }

        // Calculate consensus threshold
        ConsensusParms consensusParms{};
        std::size_t const threshold =
            (totalValidators * consensusParms.minCONSENSUS_PCT + 99) / 100;

        result[jss::total_validators] = static_cast<Json::UInt>(totalValidators);
        result[jss::consensus_threshold] = static_cast<Json::UInt>(threshold);
        result[jss::consensus_percentage] = static_cast<Json::UInt>(consensusParms.minCONSENSUS_PCT);

        // For each excluded account, show how many validators exclude it
        for (auto const& [account, count] : exclusionCounts)
        {
            Json::Value& accountInfo = excludedAccounts[toBase58(account)];
            accountInfo[jss::exclusion_count] = static_cast<Json::UInt>(count);
            accountInfo["percentage"] =
                static_cast<Json::UInt>((count * 100) / totalValidators);
            accountInfo["meets_threshold"] = (count >= threshold);

            // Add reason information if available from ExclusionManager
            auto exclusionInfo = exclusionManager.getExclusionInfo(account);
            if (exclusionInfo)
            {
                if (!exclusionInfo->reason.empty())
                    accountInfo["reason"] = exclusionInfo->reason;
                if (!exclusionInfo->dateAdded.empty())
                    accountInfo["date_added"] = exclusionInfo->dateAdded;
            }
        }

        // Get stats from ExclusionManager
        auto stats = exclusionManager.getStats();
        Json::Value& managerStats = result["exclusion_manager_stats"];
        managerStats["total_validators_cached"] =
            static_cast<Json::UInt>(stats.totalValidators);
        managerStats["consensus_excluded_count"] =
            static_cast<Json::UInt>(stats.totalExcludedAddresses);
        managerStats["unique_exclusions"] =
            static_cast<Json::UInt>(stats.totalUniqueExclusions);
    }

    return result;
}

} // namespace ripple