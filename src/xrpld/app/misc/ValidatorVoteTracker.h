#ifndef PFT_APP_MISC_VALIDATORVOTETRACKER_H_INCLUDED
#define PFT_APP_MISC_VALIDATORVOTETRACKER_H_INCLUDED

#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/beast/utility/Journal.h>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>

namespace ripple {

class Application;
class ReadView;
class SHAMap;
class Rules;

/**
 * Tracks validator votes for ledgers and generates ValidatorVote pseudo-transactions
 */
class ValidatorVoteTracker
{
public:
    struct Vote
    {
        PublicKey validatorKey;
        uint256 ledgerHash;
        LedgerIndex ledgerSeq;
        uint256 validationHash;  // Hash of the validation message as proof
        NetClock::time_point voteTime;
        std::optional<AccountID> exclusionAdd;
        std::optional<AccountID> exclusionRemove;
    };

private:
    Application& app_;
    beast::Journal j_;
    mutable std::mutex mutex_;
    
    // Track votes by ledger sequence
    std::unordered_map<LedgerIndex, std::vector<Vote>> votesByLedger_;
    
    // Track which validators we've already created transactions for in each ledger
    std::unordered_map<LedgerIndex, std::set<PublicKey>> processedValidators_;

public:
    explicit ValidatorVoteTracker(Application& app, beast::Journal journal);
    ~ValidatorVoteTracker() = default;

    /**
     * Record a vote from a validator for a specific ledger
     */
    void
    recordVote(
        PublicKey const& validatorKey,
        uint256 const& ledgerHash,
        LedgerIndex ledgerSeq,
        uint256 const& validationHash,
        NetClock::time_point voteTime,
        std::optional<AccountID> const& exclusionAdd = std::nullopt,
        std::optional<AccountID> const& exclusionRemove = std::nullopt);

    /**
     * Generate ValidatorVote pseudo-transactions for inclusion in the next ledger
     */
    void
    doVoting(
        std::shared_ptr<ReadView const> const& lastClosedLedger,
        std::vector<std::shared_ptr<STValidation>> const& parentValidations,
        std::shared_ptr<SHAMap> const& initialPosition,
        beast::Journal journal);

    /**
     * Clean up old vote records to prevent memory growth
     */
    void
    cleanup(LedgerIndex currentLedger);

private:
    /**
     * Check if ValidatorVoteTracking amendment is enabled
     */
    bool
    isEnabled(Rules const& rules) const;
};

}  // namespace ripple

#endif