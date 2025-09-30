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

#ifndef RIPPLE_APP_MISC_VALIDATOREXCLUSIONMANAGER_H_INCLUDED
#define RIPPLE_APP_MISC_VALIDATOREXCLUSIONMANAGER_H_INCLUDED

#include <xrpld/app/misc/RemoteExclusionListFetcher.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/basics/Log.h>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_set>

namespace ripple {

class Application;
class Config;
class ReadView;

/**
 * Manages validator's own exclusion list changes with rate limiting.
 * This class handles the configured exclusion list from the config file
 * and generates add/remove operations for validations with rate limiting.
 */
class ValidatorExclusionManager
{
public:
    explicit ValidatorExclusionManager(
        Application& app,
        Config const& config,
        beast::Journal journal);

    /**
     * Initialize the manager with ledger state on first full ledger
     * Compares config with current ledger state and queues changes
     */
    void initialize(
        PublicKey const& validatorPubKey,
        ReadView const& view);

    /**
     * Check if validator should include exclusion changes in validation.
     * Rate limited to every 10th ledger with single add/remove.
     * Removes the change from pending queue after returning it.
     *
     * @param ledgerSeq Current ledger sequence
     * @return Optional pair of (exclusionAdd, exclusionRemove) or nullopt if no changes
     */
    std::optional<std::pair<std::optional<AccountID>, std::optional<AccountID>>>
    getExclusionChange(LedgerIndex ledgerSeq);

    /**
     * Update ExclusionManager with reason information from remote fetcher
     * Called during initialization and when remote lists are updated
     */
    void updateExclusionManagerReasons();

private:
    Application& app_;
    Config const& config_;
    beast::Journal j_;
    mutable std::mutex mutex_;

    // Configured exclusions from config file
    std::unordered_set<AccountID> configuredExclusions_;

    // Remote exclusion list fetcher
    std::unique_ptr<RemoteExclusionListFetcher> remoteFetcher_;

    // Pending operations queue
    std::queue<std::pair<bool, AccountID>> pendingChanges_; // true=add, false=remove

    // Last ledger where we made a change
    LedgerIndex lastChangeLedger_ = 0;

    // Minimum ledgers between changes (rate limiting)
    static constexpr LedgerIndex CHANGE_INTERVAL = 10;

    // Flag to track if we've been initialized
    bool initialized_ = false;

    /**
     * Compare configured vs actual exclusions and queue changes
     */
    void updatePendingChanges(
        std::unordered_set<AccountID> const& currentExclusions);
};

} // namespace ripple

#endif // RIPPLE_APP_MISC_VALIDATOREXCLUSIONMANAGER_H_INCLUDED