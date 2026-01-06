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

    if (!masterAccount_ || !memoAccount_)
        return false;

    if (!tx.isFieldPresent(sfAccount) || !tx.isFieldPresent(sfDestination))
        return false;

    AccountID const sender = tx.getAccountID(sfAccount);
    AccountID const destination = tx.getAccountID(sfDestination);

    if (sender != *masterAccount_ || destination != *memoAccount_)
        return false;

    if (!tx.isFieldPresent(sfMemos))
        return false;

    auto const& memos = tx.getFieldArray(sfMemos);
    if (memos.empty())
        return false;

    for (auto const& memo : memos)
    {
        if (!memo.isFieldPresent(sfMemoData))
            continue;

        auto const memoDataBlob = memo.getFieldVL(sfMemoData);
        std::string memoData(
            reinterpret_cast<char const*>(memoDataBlob.data()),
            memoDataBlob.size());

        auto update = parseMemo(memoData);
        if (!update)
            continue;

        // Idempotent: if this is the same update we already have, accept it
        if (pendingUpdate_ && update->sequence == pendingUpdate_->sequence &&
            update->hash == pendingUpdate_->hash &&
            update->effectiveLedger == pendingUpdate_->effectiveLedger)
        {
            return true;
        }

        // Enforce monotonic sequence
        if (update->sequence <= highestSequence_)
        {
            JLOG(j_.warn()) << "UNLHashWatcher: Rejecting update with sequence "
                            << update->sequence
                            << " (highest seen: " << highestSequence_ << ")";
            return false;
        }

        pendingUpdate_ = update;
        highestSequence_ = update->sequence;

        JLOG(j_.info()) << "UNLHashWatcher: Received UNL hash update"
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
    Json::Value root;
    Json::Reader reader;

    if (!reader.parse(memoData, root))
    {
        JLOG(j_.trace()) << "UNLHashWatcher: Failed to parse memo as JSON";
        return std::nullopt;
    }

    if (!root.isObject() || !root.isMember("hash") ||
        !root.isMember("effectiveLedger") || !root.isMember("sequence"))
    {
        JLOG(j_.trace()) << "UNLHashWatcher: Memo missing required fields";
        return std::nullopt;
    }

    UNLHashUpdate update;

    std::string hashStr = root["hash"].asString();
    if (hashStr.size() != 64)
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

    if (!root["effectiveLedger"].isIntegral())
    {
        JLOG(j_.debug()) << "UNLHashWatcher: effectiveLedger is not an integer";
        return std::nullopt;
    }
    auto const effectiveLedgerVal = root["effectiveLedger"].asInt();
    if (effectiveLedgerVal < 0)
    {
        JLOG(j_.debug()) << "UNLHashWatcher: effectiveLedger is negative";
        return std::nullopt;
    }
    update.effectiveLedger = static_cast<LedgerIndex>(effectiveLedgerVal);

    if (!root["sequence"].isIntegral())
    {
        JLOG(j_.debug()) << "UNLHashWatcher: sequence is not an integer";
        return std::nullopt;
    }
    auto const sequenceVal = root["sequence"].asInt();
    if (sequenceVal < 0)
    {
        JLOG(j_.debug()) << "UNLHashWatcher: sequence is negative";
        return std::nullopt;
    }
    update.sequence = static_cast<std::uint32_t>(sequenceVal);

    update.version = 1;
    if (root.isMember("version") && root["version"].isIntegral())
    {
        auto const versionVal = root["version"].asInt();
        if (versionVal > 0)
        {
            update.version = static_cast<std::uint32_t>(versionVal);
        }
    }

    return update;
}

bool
UNLHashWatcher::isEnabled(Rules const& rules) const
{
    return rules.enabled(featureDynamicUNL);
}

}  // namespace ripple
