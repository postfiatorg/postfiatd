#include <xrpld/app/misc/ValidatorExclusionManager.h>
#include <xrpld/app/main/Application.h>
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
    JLOG(j_.info()) << "ValidatorExclusionManager: Initialized with "
                    << configuredExclusions_.size()
                    << " configured exclusions";
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

    // Get validator's account
    AccountID validatorAccount = calcAccountID(validatorPubKey);

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

    // Find accounts to add (in config but not in ledger)
    for (auto const& account : configuredExclusions_)
    {
        if (currentExclusions.find(account) == currentExclusions.end())
        {
            pendingChanges_.push({true, account}); // true = add
            JLOG(j_.debug()) << "ValidatorExclusionManager: Queued add for "
                            << toBase58(account);
        }
    }

    // Find accounts to remove (in ledger but not in config)
    for (auto const& account : currentExclusions)
    {
        if (configuredExclusions_.find(account) == configuredExclusions_.end())
        {
            pendingChanges_.push({false, account}); // false = remove
            JLOG(j_.debug()) << "ValidatorExclusionManager: Queued remove for "
                            << toBase58(account);
        }
    }

    JLOG(j_.info()) << "ValidatorExclusionManager: "
                   << pendingChanges_.size() << " pending changes queued";
}

} // namespace ripple