#ifndef RIPPLE_APP_MISC_UNLHASHWATCHER_H_INCLUDED
#define RIPPLE_APP_MISC_UNLHASHWATCHER_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Protocol.h>

#include <mutex>
#include <optional>

namespace ripple {

class Application;
class STTx;
class Rules;

/**
 * @brief Watches for on-chain UNL hash publications from a trusted publisher.
 *
 * This class monitors validated transactions for memos containing UNL hash
 * publications. The memo must be sent from a configured master account to
 * a configured memo account. The memo contains:
 * - hash: The sha512Half hash of the UNL JSON
 * - effectiveLedger: The flag ledger when the UNL should take effect
 * - sequence: Monotonic counter to prevent replay attacks
 *
 * Nodes verify that fetched UNL lists match the on-chain hash before applying.
 */
class UNLHashWatcher
{
public:
    /**
     * @brief Structure representing a published UNL hash update
     */
    struct UNLHashUpdate
    {
        uint256 hash;                 // sha512Half of the UNL JSON
        LedgerIndex effectiveLedger;  // Flag ledger when this takes effect
        std::uint32_t sequence;       // Monotonic sequence number
        std::uint32_t version;        // Format version
    };

private:
    Application& app_;
    beast::Journal j_;
    mutable std::mutex mutex_;

    // Configured publisher accounts
    std::optional<AccountID> masterAccount_;  // Account that sends the tx
    std::optional<AccountID> memoAccount_;    // Destination account for memo

    // Current authoritative hash
    std::optional<UNLHashUpdate> currentUpdate_;

    // Pending update (waiting for effective ledger)
    std::optional<UNLHashUpdate> pendingUpdate_;

    // Track the highest sequence seen to prevent replays
    std::uint32_t highestSequence_ = 0;

public:
    explicit UNLHashWatcher(Application& app, beast::Journal journal);
    ~UNLHashWatcher() = default;

    /**
     * @brief Configure the publisher accounts from config
     * @param masterAccount The account that publishes UNL hashes
     * @param memoAccount The destination account for the memo transaction
     */
    void
    configure(AccountID const& masterAccount, AccountID const& memoAccount);

    /**
     * @brief Check if the watcher is configured with valid accounts
     */
    bool
    isConfigured() const;

    /**
     * @brief Process a validated transaction to check for UNL hash updates
     * @param tx The transaction to check
     * @return true if a valid UNL hash update was found and stored
     */
    bool
    processTransaction(STTx const& tx);

    /**
     * @brief Get the current authoritative UNL hash
     * @return The current hash or nullopt if none available
     */
    std::optional<uint256>
    getCurrentHash() const;

    /**
     * @brief Get the pending UNL update (if any)
     * @return The pending update or nullopt if none
     */
    std::optional<UNLHashUpdate>
    getPendingUpdate() const;

    /**
     * @brief Check if a given hash matches the current authoritative hash
     * @param hash The hash to verify
     * @return true if the hash matches
     */
    bool
    verifyHash(uint256 const& hash) const;

    /**
     * @brief Check if a pending update should be applied at the given ledger
     * @param ledgerSeq The current ledger sequence
     * @return true if there's a pending update ready to apply
     */
    bool
    shouldApplyPendingUpdate(LedgerIndex ledgerSeq) const;

    /**
     * @brief Apply the pending update as the current update
     * Called at flag ledgers when shouldApplyPendingUpdate returns true
     */
    void
    applyPendingUpdate();

    /**
     * @brief Get the highest sequence number seen
     */
    std::uint32_t
    getHighestSequence() const;

private:
    /**
     * @brief Parse a UNL hash update from a memo
     * @param memoData The raw memo data (hex-decoded)
     * @return The parsed update or nullopt if invalid
     */
    std::optional<UNLHashUpdate>
    parseMemo(std::string const& memoData) const;

    /**
     * @brief Check if the DynamicUNL amendment is enabled
     */
    bool
    isEnabled(Rules const& rules) const;
};

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_UNLHASHWATCHER_H_INCLUDED
