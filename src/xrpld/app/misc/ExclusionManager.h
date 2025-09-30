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

#ifndef RIPPLE_APP_MISC_EXCLUSIONMANAGER_H_INCLUDED
#define RIPPLE_APP_MISC_EXCLUSIONMANAGER_H_INCLUDED

#include <xrpl/protocol/AccountID.h>
#include <xrpl/basics/Log.h>
#include <xrpld/consensus/ConsensusParms.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace ripple {

class ReadView;
class Application;

/**
 * ExclusionManager maintains an in-memory cache of validator exclusion lists
 * and efficiently determines which addresses are excluded by consensus.
 *
 * An address is considered excluded if at least minCONSENSUS_PCT (67%) of
 * UNL validators have that address on their exclusion list.
 *
 * Note: The cache is built based on the current UNL from the validator list.
 * Only validators in the UNL can contribute to the consensus exclusion,
 * though the SetAccount transaction currently uses ValidatorVoteStats as
 * a proxy for validator status due to transaction context limitations.
 */
class ExclusionManager
{
public:
    explicit ExclusionManager(Application& app);
    ~ExclusionManager() = default;

    /**
     * Check if an account is excluded by consensus
     * This is a fast O(1) lookup in the pre-calculated cache
     */
    bool isExcluded(AccountID const& account) const;

    /**
     * Update the exclusion list for a validator
     * Called when a validator modifies their exclusion list via AccountSet
     */
    void updateValidatorExclusions(
        AccountID const& validator,
        std::unordered_set<AccountID> const& exclusions);

    /**
     * Remove a validator from tracking
     * Called if a validator is removed from UNL or becomes inactive
     */
    void removeValidator(AccountID const& validator);

    /**
     * Rebuild the entire cache from the ledger
     * Called on startup or when the UNL changes significantly
     */
    void rebuildCache(ReadView const& view);

    /**
     * Check if the cache has been initialized from ledger
     */
    bool isInitialized() const;

    /**
     * Get statistics about the exclusion system
     */
    struct Stats
    {
        std::size_t totalValidators;
        std::size_t totalExcludedAddresses;
        std::size_t totalUniqueExclusions;
    };
    Stats getStats() const;

    /**
     * Structure to store exclusion information including reason
     */
    struct ExclusionInfo
    {
        std::string reason;
        std::string dateAdded;
        std::size_t voteCount = 0;  // Number of validators excluding this address
    };

    /**
     * Get exclusion reason and metadata for an address
     * Returns nullopt if address is not excluded or no reason is available
     */
    std::optional<ExclusionInfo> getExclusionInfo(AccountID const& account) const;

    /**
     * Update exclusion reasons from remote fetcher
     * Called when remote exclusion lists are fetched
     */
    void updateExclusionReasons(
        std::unordered_map<AccountID, ExclusionInfo> const& reasons);

private:
    Application& app_;
    beast::Journal j_;

    mutable std::mutex mutable_;

    // Map from validator account to their exclusion list
    std::unordered_map<AccountID, std::unordered_set<AccountID>> validatorExclusions_;

    // Map from potentially excluded account to count of validators excluding it
    std::unordered_map<AccountID, std::size_t> exclusionCounts_;

    // Pre-calculated set of accounts that meet the consensus threshold for exclusion
    std::unordered_set<AccountID> consensusExcluded_;

    // Map from excluded account to exclusion info (reason, date, etc)
    std::unordered_map<AccountID, ExclusionInfo> exclusionInfoMap_;

    // Total number of active validators
    std::size_t totalValidators_ = 0;

    // Flag to track if cache has been initialized from ledger
    bool initialized_ = false;

    /**
     * Recalculate which addresses meet the consensus threshold
     * Called internally after any change to validator exclusion lists
     */
    void recalculateConsensusExclusions();

    /**
     * Calculate the minimum number of validators needed for consensus
     */
    std::size_t getConsensusThreshold() const;
};

} // namespace ripple

#endif // RIPPLE_APP_MISC_EXCLUSIONMANAGER_H_INCLUDED