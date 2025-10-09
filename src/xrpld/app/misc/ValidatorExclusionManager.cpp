#include <xrpld/app/misc/ValidatorExclusionManager.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ExclusionManager.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/core/Config.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STArray.h>

namespace ripple {

ValidatorExclusionManager::ValidatorExclusionManager(
    Application& app,
    Config const& config,
    beast::Journal journal)
    : app_(app)
    , config_(config)
    , j_(journal)
    , configuredExclusions_(config.VALIDATOR_EXCLUSIONS)
{
    // Initialize remote fetcher if sources are configured
    if (!config.VALIDATOR_EXCLUSIONS_SOURCES.empty())
    {
        remoteFetcher_ = std::make_unique<RemoteExclusionListFetcher>(
            app, config, journal);
        remoteFetcher_->start();

        // Connect remote fetcher to ExclusionManager for automatic updates
        app_.getExclusionManager().setRemoteFetcher(remoteFetcher_.get());
    }

    JLOG(j_.info()) << "ValidatorExclusionManager: Initialized with "
                    << configuredExclusions_.size()
                    << " configured exclusions and "
                    << config.VALIDATOR_EXCLUSIONS_SOURCES.size()
                    << " remote sources";
}

std::optional<std::pair<std::optional<AccountID>, std::optional<AccountID>>>
ValidatorExclusionManager::getExclusionChange(LedgerIndex ledgerSeq)
{
    std::lock_guard lock(mutex_);

    // If not initialized yet, return nothing
    if (!initialized_)
    {
        JLOG(j_.trace()) << "ValidatorExclusionManager: Not initialized yet";
        return std::nullopt;
    }

    // If remote fetcher is configured, check its status first
    if (remoteFetcher_)
    {
        // If initial fetch isn't complete or sources aren't accessible, no changes allowed
        if (!remoteFetcher_->isInitialFetchComplete())
        {
            JLOG(j_.debug()) << "ValidatorExclusionManager: Remote fetcher not ready, no changes allowed";
            return std::nullopt;
        }

        if (!remoteFetcher_->areAllSourcesAccessible())
        {
            JLOG(j_.debug()) << "ValidatorExclusionManager: Remote sources not accessible, no changes allowed";
            return std::nullopt;
        }

        // Check if remote list has been modified
        if (remoteFetcher_->hasBeenModified(true))  // Reset flag after checking
        {
            JLOG(j_.info()) << "ValidatorExclusionManager: Remote exclusion list modified, updating pending changes";

            // Update ExclusionManager with reason information from remote fetcher
            auto reasons = remoteFetcher_->getExclusionReasons();
            if (!reasons.empty())
            {
                std::unordered_map<AccountID, ExclusionManager::ExclusionInfo> exclusionInfoMap;
                for (auto const& [account, reasonPair] : reasons)
                {
                    ExclusionManager::ExclusionInfo info;
                    info.reason = reasonPair.first;
                    info.dateAdded = reasonPair.second;
                    exclusionInfoMap[account] = info;
                }
                app_.getExclusionManager().updateExclusionReasons(exclusionInfoMap);
            }

            // Read current exclusions from the ledger
            auto currentExclusions = getCurrentExclusionsFromLedger();

            updatePendingChanges(currentExclusions);
        }
    }

    // Check rate limiting - only allow changes every CHANGE_INTERVAL ledgers
    if (ledgerSeq < lastChangeLedger_ + CHANGE_INTERVAL)
    {
        JLOG(j_.trace()) << "ValidatorExclusionManager: Rate limited, next change at ledger "
                        << (lastChangeLedger_ + CHANGE_INTERVAL);
        return std::nullopt;
    }

    // Check if we have pending changes
    if (pendingChanges_.empty())
    {
        JLOG(j_.trace()) << "ValidatorExclusionManager: No pending changes";
        return std::nullopt;
    }

    // Get next change from queue and remove it
    auto const [isAdd, account] = pendingChanges_.front();
    pendingChanges_.pop();

    // Update last change ledger
    lastChangeLedger_ = ledgerSeq;

    JLOG(j_.info()) << "ValidatorExclusionManager: Providing "
                   << (isAdd ? "add" : "remove")
                   << " for " << toBase58(account)
                   << " at ledger " << ledgerSeq
                   << ", " << pendingChanges_.size() << " changes remaining";

    // Return the appropriate change
    if (isAdd)
        return std::make_pair(std::make_optional(account), std::nullopt);
    else
        return std::make_pair(std::nullopt, std::make_optional(account));
}

void
ValidatorExclusionManager::initialize(
    PublicKey const& validatorPubKey,
    ReadView const& view)
{
    std::lock_guard lock(mutex_);

    // Check if already initialized
    if (initialized_)
    {
        return;
    }

    // Check if feature is enabled
    if (!view.rules().enabled(featureAccountExclusion))
    {
        JLOG(j_.info()) << "ValidatorExclusionManager: AccountExclusion feature not enabled";
        return;
    }

    // Get validator's master key from the signing key
    // The validatorPubKey parameter is the signing key from app_.getValidationPublicKey()
    // We need to get the master key to derive the correct account, matching what ValidatorVote does
    auto masterKey = app_.validators().getTrustedKey(validatorPubKey);

    // If not trusted, try listed key
    if (!masterKey)
        masterKey = app_.validators().getListedKey(validatorPubKey);

    // Use master key if available, otherwise use signing key (same logic as RCLValidations)
    auto const keyForAccount = masterKey.value_or(validatorPubKey);

    // Get validator's account using the correct key
    AccountID validatorAccount = calcAccountID(keyForAccount);

    JLOG(j_.info()) << "ValidatorExclusionManager: Using "
                    << (masterKey ? "master key" : "signing key")
                    << " to derive account: " << toBase58(validatorAccount);

    // Store validator account for future use
    validatorAccount_ = validatorAccount;

    // Get validator's current exclusion list from ledger
    std::unordered_set<AccountID> currentExclusions;

    auto const accountSLE = view.read(keylet::account(validatorAccount));
    if (accountSLE && accountSLE->isFieldPresent(sfExclusionList))
    {
        auto const& exclusionList = accountSLE->getFieldArray(sfExclusionList);
        for (auto const& entry : exclusionList)
        {
            if (entry.isFieldPresent(sfAccount))
            {
                currentExclusions.insert(entry.getAccountID(sfAccount));
            }
        }
    }

    JLOG(j_.info()) << "ValidatorExclusionManager: Initialized with "
                    << "current exclusions: " << currentExclusions.size()
                    << ", configured: " << configuredExclusions_.size();

    // Update pending changes based on differences
    updatePendingChanges(currentExclusions);

    // Mark as initialized
    initialized_ = true;
}

void
ValidatorExclusionManager::updatePendingChanges(
    std::unordered_set<AccountID> const& currentExclusions)
{
    // Note: This function should be called with mutex_ already locked
    // Clear existing pending changes
    while (!pendingChanges_.empty())
        pendingChanges_.pop();

    // If remote fetcher is configured, we must wait for it to be ready
    if (remoteFetcher_)
    {
        // Check if initial fetch is complete and all sources are accessible
        if (!remoteFetcher_->isInitialFetchComplete())
        {
            JLOG(j_.info()) << "ValidatorExclusionManager: Remote fetcher not ready yet, "
                           << "no exclusion changes will be made";
            // Clear any pending changes and return without making any changes
            while (!pendingChanges_.empty())
                pendingChanges_.pop();
            return;
        }

        if (!remoteFetcher_->areAllSourcesAccessible())
        {
            JLOG(j_.warn()) << "ValidatorExclusionManager: Not all remote sources accessible, "
                           << "no exclusion changes will be made";
            // Clear any pending changes and return without making any changes
            while (!pendingChanges_.empty())
                pendingChanges_.pop();
            return;
        }
    }

    // Combine configured exclusions with remote exclusions
    std::unordered_set<AccountID> combinedExclusions = configuredExclusions_;

    if (remoteFetcher_)
    {
        // At this point we know the fetcher is ready and all sources are accessible
        auto remoteExclusions = remoteFetcher_->getCombinedExclusions();
        combinedExclusions.insert(remoteExclusions.begin(), remoteExclusions.end());

        JLOG(j_.debug()) << "ValidatorExclusionManager: Added "
                        << remoteExclusions.size() << " remote exclusions";
    }

    // Find accounts to add (in combined config but not in ledger)
    for (auto const& account : combinedExclusions)
    {
        if (currentExclusions.find(account) == currentExclusions.end())
        {
            pendingChanges_.push({true, account}); // true = add
            JLOG(j_.debug()) << "ValidatorExclusionManager: Queued add for "
                            << toBase58(account);
        }
    }

    // Find accounts to remove (in ledger but not in combined config)
    for (auto const& account : currentExclusions)
    {
        if (combinedExclusions.find(account) == combinedExclusions.end())
        {
            pendingChanges_.push({false, account}); // false = remove
            JLOG(j_.debug()) << "ValidatorExclusionManager: Queued remove for "
                            << toBase58(account);
        }
    }

    JLOG(j_.info()) << "ValidatorExclusionManager: "
                   << pendingChanges_.size() << " pending changes queued";
}

void
ValidatorExclusionManager::updateExclusionManagerReasons()
{
    // Only update if remote fetcher is configured
    if (!remoteFetcher_)
        return;

    // Get reason information from remote fetcher
    auto reasons = remoteFetcher_->getExclusionReasons();
    if (!reasons.empty())
    {
        std::unordered_map<AccountID, ExclusionManager::ExclusionInfo> exclusionInfoMap;
        for (auto const& [account, reasonPair] : reasons)
        {
            ExclusionManager::ExclusionInfo info;
            info.reason = reasonPair.first;
            info.dateAdded = reasonPair.second;
            exclusionInfoMap[account] = info;
        }
        app_.getExclusionManager().updateExclusionReasons(exclusionInfoMap);

        JLOG(j_.info()) << "ValidatorExclusionManager: Updated ExclusionManager with "
                       << exclusionInfoMap.size() << " exclusion reasons";
    }
}

std::unordered_set<AccountID>
ValidatorExclusionManager::getCurrentExclusionsFromLedger() const
{
    std::unordered_set<AccountID> exclusions;

    // Check if we have a validator account
    if (!validatorAccount_)
    {
        JLOG(j_.warn()) << "ValidatorExclusionManager: No validator account set, cannot read from ledger";
        return exclusions;
    }

    // Get the current ledger
    auto ledger = app_.getLedgerMaster().getClosedLedger();
    if (!ledger)
    {
        JLOG(j_.warn()) << "ValidatorExclusionManager: No closed ledger available";
        return exclusions;
    }

    // Read the validator's account from ledger
    auto const accountSLE = ledger->read(keylet::account(*validatorAccount_));
    if (!accountSLE)
    {
        JLOG(j_.debug()) << "ValidatorExclusionManager: Validator account not found in ledger";
        return exclusions;
    }

    // Read exclusion list if present
    if (accountSLE->isFieldPresent(sfExclusionList))
    {
        auto const& exclusionList = accountSLE->getFieldArray(sfExclusionList);
        for (auto const& entry : exclusionList)
        {
            if (entry.isFieldPresent(sfAccount))
            {
                exclusions.insert(entry.getAccountID(sfAccount));
            }
        }
    }

    JLOG(j_.debug()) << "ValidatorExclusionManager: Read " << exclusions.size()
                    << " exclusions from ledger";

    return exclusions;
}

} // namespace ripple