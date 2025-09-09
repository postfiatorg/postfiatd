#include <xrpld/app/misc/ValidatorVoteTracker.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/core/Config.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpld/shamap/SHAMap.h>
#include <xrpld/ledger/ReadView.h>

namespace ripple {

ValidatorVoteTracker::ValidatorVoteTracker(
    Application& app,
    beast::Journal journal)
    : app_(app), j_(journal)
{
}

void
ValidatorVoteTracker::recordVote(
    PublicKey const& validatorKey,
    uint256 const& ledgerHash,
    LedgerIndex ledgerSeq,
    uint256 const& validationHash,
    NetClock::time_point voteTime)
{
    std::lock_guard lock(mutex_);
    
    // Record the vote with validation hash as proof
    Vote vote{validatorKey, ledgerHash, ledgerSeq, validationHash, voteTime};
    votesByLedger_[ledgerSeq].push_back(vote);
    
    JLOG(j_.debug()) << "ValidatorVoteTracker: Recorded vote from "
                     << toBase58(calcAccountID(validatorKey))
                     << " for ledger " << ledgerSeq
                     << " with validation hash " << validationHash;
}

void
ValidatorVoteTracker::doVoting(
    std::shared_ptr<ReadView const> const& lastClosedLedger,
    std::vector<std::shared_ptr<STValidation>> const& parentValidations,
    std::shared_ptr<SHAMap> const& initialPosition,
    beast::Journal journal)
{
    // Check if the amendment is enabled
    if (!isEnabled(lastClosedLedger->rules()))
    {
        JLOG(journal.trace()) << "ValidatorVoteTracker: Amendment not enabled";
        return;
    }
    
    std::lock_guard lock(mutex_);
    
    // Get the current ledger sequence
    LedgerIndex currentSeq = lastClosedLedger->seq();
    
    // Process votes for the last closed ledger (currentSeq)
    // These votes will be included in the next ledger being built (currentSeq + 1)
    LedgerIndex voteSeq = currentSeq;
    
    auto it = votesByLedger_.find(voteSeq);
    if (it == votesByLedger_.end() || it->second.empty())
    {
        JLOG(journal.trace()) << "ValidatorVoteTracker: No votes for ledger " << voteSeq;
        return;
    }
    
    // Get the set of validators we've already processed for this ledger
    auto& processed = processedValidators_[currentSeq];
    
    // Generate ValidatorVote pseudo-transactions for each unique validator
    std::set<PublicKey> uniqueValidators;
    for (auto const& vote : it->second)
    {
        // Skip if we've already processed this validator for this ledger
        if (processed.count(vote.validatorKey) > 0)
            continue;
            
        // Skip duplicate validators in this batch
        if (!uniqueValidators.insert(vote.validatorKey).second)
            continue;
        
        // Create the ValidatorVote pseudo-transaction
        STTx voteTx(
            ttVALIDATOR_VOTE,
            [this, &vote, seq = currentSeq + 1](auto& obj) {
                obj.setAccountID(sfAccount, AccountID());
                obj.setFieldU32(sfNetworkID, app_.config().NETWORK_ID);
                obj.setFieldU32(sfLedgerSequence, vote.ledgerSeq);
                obj.setFieldH256(sfLedgerHash, vote.ledgerHash);
                obj.setFieldVL(sfValidatorPublicKey, vote.validatorKey.slice());
                obj.setFieldH256(sfValidationHash, vote.validationHash);
                if (vote.voteTime != NetClock::time_point{})
                    obj.setFieldU32(sfCloseTime, vote.voteTime.time_since_epoch().count());
            });
        
        Serializer s;
        voteTx.add(s);
        
        // Add to the transaction set
        auto const txID = voteTx.getTransactionID();
        auto const result = initialPosition->addItem(
            SHAMapNodeType::tnTRANSACTION_NM,
            make_shamapitem(txID, s.slice()));
        
        // Mark this validator as processed for this ledger
        processed.insert(vote.validatorKey);
        
        JLOG(journal.info()) << "ValidatorVoteTracker: Generated vote transaction "
                            << txID
                            << " for validator " << toBase58(calcAccountID(vote.validatorKey))
                            << " in ledger " << currentSeq + 1
                            << " (addItem result: " << (result ? "success" : "failed") << ")";
    }
    
    // Clean up old data
    cleanup(currentSeq);
}

void
ValidatorVoteTracker::cleanup(LedgerIndex currentLedger)
{
    // Keep votes for the last 10 ledgers
    const LedgerIndex keepAfter = (currentLedger > 10) ? currentLedger - 10 : 0;
    
    // Clean up old votes
    auto voteIt = votesByLedger_.begin();
    while (voteIt != votesByLedger_.end())
    {
        if (voteIt->first < keepAfter)
            voteIt = votesByLedger_.erase(voteIt);
        else
            ++voteIt;
    }
    
    // Clean up processed validators tracking
    auto procIt = processedValidators_.begin();
    while (procIt != processedValidators_.end())
    {
        if (procIt->first < keepAfter)
            procIt = processedValidators_.erase(procIt);
        else
            ++procIt;
    }
}

bool
ValidatorVoteTracker::isEnabled(Rules const& rules) const
{
    return rules.enabled(featureValidatorVoteTracking);
}

}  // namespace ripple