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

#include <xrpld/app/misc/ExclusionManager.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/misc/RemoteExclusionListFetcher.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/PublicKey.h>

namespace ripple {

ExclusionManager::ExclusionManager(Application& app)
    : app_(app)
    , j_(app.journal("ExclusionManager"))
{
    JLOG(j_.info()) << "ExclusionManager initialized";
}

bool
ExclusionManager::isExcluded(AccountID const& account) const
{
    std::lock_guard lock(mutable_);
    // If not initialized yet, nothing is excluded
    if (!initialized_)
        return false;
    return consensusExcluded_.find(account) != consensusExcluded_.end();
}

bool
ExclusionManager::isInitialized() const
{
    std::lock_guard lock(mutable_);
    return initialized_;
}

void
ExclusionManager::updateValidatorExclusions(
    AccountID const& validator,
    std::unordered_set<AccountID> const& exclusions)
{
    std::lock_guard lock(mutable_);

    // Get the old exclusion list for this validator (if any)
    auto oldExclusionsIt = validatorExclusions_.find(validator);
    std::unordered_set<AccountID> oldExclusions;

    if (oldExclusionsIt != validatorExclusions_.end())
    {
        oldExclusions = std::move(oldExclusionsIt->second);
    }

    // Update to new exclusion list
    if (exclusions.empty())
    {
        validatorExclusions_.erase(validator);
    }
    else
    {
        validatorExclusions_[validator] = exclusions;
    }

    // Update the total validator count to match the actual map size
    totalValidators_ = validatorExclusions_.size();

    // Update exclusion counts
    // First, decrement counts for addresses that were in old list
    for (auto const& account : oldExclusions)
    {
        auto it = exclusionCounts_.find(account);
        if (it != exclusionCounts_.end())
        {
            if (it->second > 1)
            {
                it->second--;
            }
            else
            {
                exclusionCounts_.erase(it);
            }
        }
    }

    // Then, increment counts for addresses in new list
    for (auto const& account : exclusions)
    {
        exclusionCounts_[account]++;
    }

    // Recalculate which addresses meet consensus threshold
    recalculateConsensusExclusions();

    JLOG(j_.debug()) << "Updated exclusions for validator " << toBase58(validator)
                     << " - " << exclusions.size() << " addresses excluded";
}

void
ExclusionManager::removeValidator(AccountID const& validator)
{
    std::lock_guard lock(mutable_);

    auto it = validatorExclusions_.find(validator);
    if (it == validatorExclusions_.end())
        return;

    // Decrement counts for all addresses this validator was excluding
    for (auto const& account : it->second)
    {
        auto countIt = exclusionCounts_.find(account);
        if (countIt != exclusionCounts_.end())
        {
            if (countIt->second > 1)
            {
                countIt->second--;
            }
            else
            {
                exclusionCounts_.erase(countIt);
            }
        }
    }

    validatorExclusions_.erase(it);

    // Update the total validator count to match the actual map size
    totalValidators_ = validatorExclusions_.size();

    recalculateConsensusExclusions();

    JLOG(j_.debug()) << "Removed validator " << toBase58(validator)
                     << " from exclusion tracking";
}

void
ExclusionManager::rebuildCache(ReadView const& view)
{
    if (!view.rules().enabled(featureAccountExclusion))
    {
        JLOG(j_.debug()) << "AccountExclusion feature not enabled, skipping cache rebuild";
        return;
    }

    std::lock_guard lock(mutable_);

    // Clear existing data
    validatorExclusions_.clear();
    exclusionCounts_.clear();
    consensusExcluded_.clear();
    totalValidators_ = 0;

    JLOG(j_.info()) << "Rebuilding exclusion cache from ledger";

    std::size_t validatorsProcessed = 0;
    std::size_t totalExclusions = 0;

    // Get list of trusted validators from the app's validator list
    // These are the validators whose vote counts for consensus
    auto const& validators = app_.validators();
    auto trustedKeys = validators.getTrustedMasterKeys();

    // For each trusted UNL validator, check if they have an exclusion list in their account
    for (auto const& pubKey : trustedKeys)
    {
        // Convert public key to account ID
        AccountID validatorAccount = calcAccountID(pubKey);

        // Get the validator's account to check their exclusion list
        auto const accountSLE = view.read(keylet::account(validatorAccount));
        if (!accountSLE)
        {
            // This validator doesn't have an account yet, skip
            JLOG(j_.debug()) << "UNL validator has no account: "
                             << toBase58(validatorAccount);
            continue;
        }

        // This is a UNL validator, count them
        totalValidators_++;
        validatorsProcessed++;

        // Check if this validator has an exclusion list
        if (accountSLE->isFieldPresent(sfExclusionList))
        {
            std::unordered_set<AccountID> exclusions;
            STArray const& exclusionList = accountSLE->getFieldArray(sfExclusionList);

            for (auto const& entry : exclusionList)
            {
                if (entry.isFieldPresent(sfAccount))
                {
                    auto const excludedAccount = entry.getAccountID(sfAccount);
                    exclusions.insert(excludedAccount);
                    exclusionCounts_[excludedAccount]++;
                    totalExclusions++;
                }
            }

            if (!exclusions.empty())
            {
                validatorExclusions_[validatorAccount] = std::move(exclusions);
            }
        }
    }

    // Update the total validator count to match the actual map size
    totalValidators_ = validatorExclusions_.size();

    // Calculate which addresses meet consensus threshold
    recalculateConsensusExclusions();

    // Mark as initialized
    initialized_ = true;

    JLOG(j_.info()) << "Exclusion cache rebuilt: "
                    << validatorsProcessed << " validators, "
                    << totalExclusions << " total exclusions, "
                    << consensusExcluded_.size() << " addresses meet consensus threshold";
}

void
ExclusionManager::recalculateConsensusExclusions()
{
    consensusExcluded_.clear();

    if (totalValidators_ == 0)
        return;

    std::size_t const threshold = getConsensusThreshold();

    for (auto const& [account, count] : exclusionCounts_)
    {
        if (count >= threshold)
        {
            consensusExcluded_.insert(account);
        }
    }

    JLOG(j_.trace()) << "Consensus exclusions recalculated: "
                     << consensusExcluded_.size() << " addresses excluded "
                     << "(threshold: " << threshold << "/" << totalValidators_ << ")";
}

std::size_t
ExclusionManager::getConsensusThreshold() const
{
    if (totalValidators_ == 0)
        return 1;

    // Use minCONSENSUS_PCT (67%) to determine threshold
    ConsensusParms consensusParms{};
    std::size_t const threshold =
        (totalValidators_ * consensusParms.minCONSENSUS_PCT + 99) / 100;

    return std::max(std::size_t(1), threshold);
}

ExclusionManager::Stats
ExclusionManager::getStats() const
{
    std::lock_guard lock(mutable_);

    Stats stats;
    stats.totalValidators = totalValidators_;
    stats.totalExcludedAddresses = consensusExcluded_.size();

    // Count total unique exclusions across all validators
    std::unordered_set<AccountID> allExclusions;
    for (auto const& [validator, exclusions] : validatorExclusions_)
    {
        allExclusions.insert(exclusions.begin(), exclusions.end());
    }
    stats.totalUniqueExclusions = allExclusions.size();

    return stats;
}

std::optional<ExclusionManager::ExclusionInfo>
ExclusionManager::getExclusionInfo(AccountID const& account) const
{
    std::lock_guard lock(mutable_);

    // Check if remote fetcher has updates and apply them
    if (remoteFetcher_ && remoteFetcher_->hasBeenModified(false))
    {
        // Get updated reasons from remote fetcher
        auto reasons = remoteFetcher_->getExclusionReasons();
        if (!reasons.empty())
        {
            // Update the exclusion info map
            // Note: We need to cast away const here since we're updating cache
            // This is safe because the method is logically const (query operation)
            auto* mutableThis = const_cast<ExclusionManager*>(this);
            for (auto const& [acct, reasonPair] : reasons)
            {
                ExclusionInfo info;
                info.reason = reasonPair.first;
                info.dateAdded = reasonPair.second;
                mutableThis->exclusionInfoMap_[acct] = info;
            }

            JLOG(j_.debug()) << "ExclusionManager: Updated reasons from remote fetcher for "
                            << reasons.size() << " addresses";
        }
    }

    auto it = exclusionInfoMap_.find(account);
    if (it != exclusionInfoMap_.end())
    {
        // Update vote count from current exclusion counts
        ExclusionInfo info = it->second;
        auto countIt = exclusionCounts_.find(account);
        if (countIt != exclusionCounts_.end())
        {
            info.voteCount = countIt->second;
        }
        return info;
    }

    // If no info stored but account is in exclusion counts, return basic info
    auto countIt = exclusionCounts_.find(account);
    if (countIt != exclusionCounts_.end())
    {
        ExclusionInfo info;
        info.voteCount = countIt->second;
        return info;
    }

    return std::nullopt;
}

void
ExclusionManager::updateExclusionReasons(
    std::unordered_map<AccountID, ExclusionInfo> const& reasons)
{
    std::lock_guard lock(mutable_);

    // Merge new reasons with existing map
    for (auto const& [account, info] : reasons)
    {
        exclusionInfoMap_[account] = info;
    }

    JLOG(j_.debug()) << "Updated exclusion reasons for " << reasons.size() << " addresses";
}

void
ExclusionManager::setRemoteFetcher(RemoteExclusionListFetcher* fetcher)
{
    std::lock_guard lock(mutable_);
    remoteFetcher_ = fetcher;

    JLOG(j_.debug()) << "Remote fetcher " << (fetcher ? "connected" : "disconnected");
}

} // namespace ripple