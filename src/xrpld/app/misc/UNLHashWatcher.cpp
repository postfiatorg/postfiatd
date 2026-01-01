#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/UNLHashWatcher.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

UNLHashWatcher::UNLHashWatcher(Application& app, beast::Journal journal)
    : app_(app), j_(journal)
{
    JLOG(j_.info()) << "UNLHashWatcher initialized";
}

void
UNLHashWatcher::configure(
    AccountID const& masterAccount,
    AccountID const& memoAccount)
{
    std::lock_guard lock(mutex_);
    masterAccount_ = masterAccount;
    memoAccount_ = memoAccount;

    JLOG(j_.info()) << "UNLHashWatcher configured with master="
                    << toBase58(masterAccount)
                    << " memo=" << toBase58(memoAccount);
}

bool
UNLHashWatcher::isConfigured() const
{
    std::lock_guard lock(mutex_);
    return masterAccount_.has_value() && memoAccount_.has_value();
}

bool
UNLHashWatcher::processTransaction(STTx const& tx)
{
    std::lock_guard lock(mutex_);

    // Must be configured
    if (!masterAccount_ || !memoAccount_)
    {
        return false;
    }

    // Check if this is a payment from masterAccount to memoAccount
    if (!tx.isFieldPresent(sfAccount) || !tx.isFieldPresent(sfDestination))
    {
        return false;
    }

    AccountID const sender = tx.getAccountID(sfAccount);
    AccountID const destination = tx.getAccountID(sfDestination);

    if (sender != *masterAccount_ || destination != *memoAccount_)
    {
        return false;
    }

    // Check for memos
    if (!tx.isFieldPresent(sfMemos))
    {
        JLOG(j_.trace()) << "UNLHashWatcher: Transaction has no memos";
        return false;
    }

    auto const& memos = tx.getFieldArray(sfMemos);
    if (memos.empty())
    {
        return false;
    }

    // Process the first memo (assuming UNL hash is in first memo)
    for (auto const& memo : memos)
    {
        if (!memo.isFieldPresent(sfMemoData))
            continue;

        // Get the memo data (hex-encoded)
        auto const memoDataHex = memo.getFieldVL(sfMemoData);
        std::string memoData(
            reinterpret_cast<char const*>(memoDataHex.data()),
            memoDataHex.size());

        // Try to parse as UNL hash update
        auto update = parseMemo(memoData);
        if (!update)
        {
            JLOG(j_.debug())
                << "UNLHashWatcher: Could not parse memo as UNL hash update";
            continue;
        }

        // Enforce monotonic sequence
        if (update->sequence <= highestSequence_)
        {
            JLOG(j_.warn()) << "UNLHashWatcher: Rejecting update with sequence "
                            << update->sequence
                            << " (highest seen: " << highestSequence_ << ")";
            return false;
        }

        // Store as pending update
        pendingUpdate_ = update;
        highestSequence_ = update->sequence;

        JLOG(j_.info()) << "UNLHashWatcher: Received new UNL hash update"
                        << " hash=" << update->hash
                        << " effectiveLedger=" << update->effectiveLedger
                        << " sequence=" << update->sequence;

        return true;
    }

    return false;
}

std::optional<uint256>
UNLHashWatcher::getCurrentHash() const
{
    std::lock_guard lock(mutex_);
    if (currentUpdate_)
        return currentUpdate_->hash;
    return std::nullopt;
}

std::optional<UNLHashWatcher::UNLHashUpdate>
UNLHashWatcher::getPendingUpdate() const
{
    std::lock_guard lock(mutex_);
    return pendingUpdate_;
}

bool
UNLHashWatcher::verifyHash(uint256 const& hash) const
{
    std::lock_guard lock(mutex_);

    // If no current hash is set, accept any hash (allows bootstrap)
    // TODO: Consider making this stricter once the system is operational
    if (!currentUpdate_)
    {
        JLOG(j_.debug())
            << "UNLHashWatcher: No current hash set, accepting fetched list";
        return true;
    }

    bool matches = (hash == currentUpdate_->hash);
    if (!matches)
    {
        JLOG(j_.warn()) << "UNLHashWatcher: Hash mismatch - expected "
                        << currentUpdate_->hash << " got " << hash;
    }
    return matches;
}

bool
UNLHashWatcher::shouldApplyPendingUpdate(LedgerIndex ledgerSeq) const
{
    std::lock_guard lock(mutex_);

    if (!pendingUpdate_)
        return false;

    // Only apply at or after the effective ledger
    return ledgerSeq >= pendingUpdate_->effectiveLedger;
}

void
UNLHashWatcher::applyPendingUpdate()
{
    std::lock_guard lock(mutex_);

    if (!pendingUpdate_)
    {
        JLOG(j_.warn()) << "UNLHashWatcher: applyPendingUpdate called with no "
                           "pending update";
        return;
    }

    currentUpdate_ = pendingUpdate_;
    pendingUpdate_.reset();

    JLOG(j_.info()) << "UNLHashWatcher: Applied pending UNL hash update"
                    << " hash=" << currentUpdate_->hash
                    << " sequence=" << currentUpdate_->sequence;
}

std::uint32_t
UNLHashWatcher::getHighestSequence() const
{
    std::lock_guard lock(mutex_);
    return highestSequence_;
}

std::optional<UNLHashWatcher::UNLHashUpdate>
UNLHashWatcher::parseMemo(std::string const& memoData) const
{
    // The memo data should be JSON
    Json::Value root;
    Json::Reader reader;

    if (!reader.parse(memoData, root))
    {
        JLOG(j_.trace()) << "UNLHashWatcher: Failed to parse memo as JSON";
        return std::nullopt;
    }

    // Required fields: hash, effectiveLedger, sequence
    if (!root.isObject() || !root.isMember("hash") ||
        !root.isMember("effectiveLedger") || !root.isMember("sequence"))
    {
        JLOG(j_.trace()) << "UNLHashWatcher: Memo missing required fields";
        return std::nullopt;
    }

    UNLHashUpdate update;

    // Parse hash (hex string)
    std::string hashStr = root["hash"].asString();
    if (hashStr.size() != 64)  // 256 bits = 64 hex chars
    {
        JLOG(j_.debug()) << "UNLHashWatcher: Invalid hash length: "
                         << hashStr.size();
        return std::nullopt;
    }

    auto hashBytes = strUnHex(hashStr);
    if (!hashBytes || hashBytes->size() != 32)
    {
        JLOG(j_.debug()) << "UNLHashWatcher: Failed to decode hash hex";
        return std::nullopt;
    }
    std::memcpy(update.hash.data(), hashBytes->data(), 32);

    // Parse effectiveLedger
    if (!root["effectiveLedger"].isUInt())
    {
        JLOG(j_.debug()) << "UNLHashWatcher: effectiveLedger is not a uint";
        return std::nullopt;
    }
    update.effectiveLedger = root["effectiveLedger"].asUInt();

    // Parse sequence
    if (!root["sequence"].isUInt())
    {
        JLOG(j_.debug()) << "UNLHashWatcher: sequence is not a uint";
        return std::nullopt;
    }
    update.sequence = root["sequence"].asUInt();

    // Parse optional version (default to 1)
    update.version = 1;
    if (root.isMember("version") && root["version"].isUInt())
    {
        update.version = root["version"].asUInt();
    }

    return update;
}

bool
UNLHashWatcher::isEnabled(Rules const& rules) const
{
    return rules.enabled(featureDynamicUNL);
}

}  // namespace ripple
