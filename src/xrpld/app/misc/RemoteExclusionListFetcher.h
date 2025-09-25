#ifndef RIPPLE_APP_MISC_REMOTEEXCLUSIONLISTFETCHER_H_INCLUDED
#define RIPPLE_APP_MISC_REMOTEEXCLUSIONLISTFETCHER_H_INCLUDED

#include <xrpld/app/misc/detail/Work.h>
#include <xrpld/core/Config.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/json/json_value.h>
#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace ripple {

class Application;

class RemoteExclusionListFetcher
{
public:
    struct ExclusionEntry
    {
        AccountID address;
        std::string reason;
        std::string dateAdded;
    };

    struct ExclusionList
    {
        std::string version;
        std::string timestamp;
        AccountID issuerAddress;
        std::vector<ExclusionEntry> blacklist;
        bool verified = false;
    };

    explicit RemoteExclusionListFetcher(
        Application& app,
        Config const& config,
        beast::Journal journal);

    ~RemoteExclusionListFetcher();

    void
    start();

    void
    stop();

    std::unordered_set<AccountID>
    getCombinedExclusions() const;

    bool
    isRunning() const;

    /**
     * Check if the exclusion list has been modified since last check
     * @param resetFlag If true, resets the modified flag after checking
     * @return true if list was modified since last check
     */
    bool
    hasBeenModified(bool resetFlag = false);

    /**
     * Check if all configured sources are accessible
     * @return true if all sources were successfully fetched in the last attempt
     */
    bool
    areAllSourcesAccessible() const;

    /**
     * Check if initial fetch has been completed
     * @return true if at least one fetch cycle has completed
     */
    bool
    isInitialFetchComplete() const;

    /**
     * Get the timestamp of the last successful update
     * @return Time point of last update
     */
    std::chrono::steady_clock::time_point
    getLastUpdateTime() const;

private:
    using clock_type = std::chrono::system_clock;
    using error_code = boost::system::error_code;

    struct SourceResult
    {
        bool success = false;
        std::optional<ExclusionList> list;
        std::string errorMessage;
    };

    Application& app_;
    Config const& config_;
    beast::Journal j_;

    boost::asio::basic_waitable_timer<clock_type> timer_;
    mutable std::mutex mutex_;

    std::unordered_map<std::string, ExclusionList> cachedLists_;
    std::unordered_set<AccountID> combinedExclusions_;

    // Track fetch results for each source
    std::vector<SourceResult> fetchResults_;

    bool running_ = false;
    std::atomic<bool> fetching_{false};
    std::atomic<bool> stopping_{false};

    // Work object for async HTTP requests
    std::weak_ptr<detail::Work> work_;

    // Track modifications and accessibility
    bool hasModifications_ = false;
    bool allSourcesAccessible_ = false;
    bool initialFetchComplete_ = false;
    std::chrono::steady_clock::time_point lastUpdateTime_;
    std::chrono::steady_clock::time_point lastSuccessfulFetchTime_;

    void
    scheduleNextFetch();

    void
    fetchAllLists();

    void
    fetchFromSource(
        Config::ValidatorExclusionSource const& source,
        std::size_t sourceIdx);

    void
    makeRequest(
        Config::ValidatorExclusionSource const& source,
        std::size_t sourceIdx);

    void
    onFetchComplete(
        boost::system::error_code const& ec,
        detail::response_type&& res,
        std::size_t sourceIdx);

    void
    onTextFetch(
        boost::system::error_code const& ec,
        std::string const& res,
        std::size_t sourceIdx);

    void
    onAllFetchesComplete();

    void
    checkAllFetchesComplete();

    std::optional<ExclusionList>
    parseExclusionList(std::string const& content);

    bool
    verifySignature(
        ExclusionList const& list,
        std::string const& pubkeyStr,
        std::string const& rawContent);

    bool
    verifyEd25519Signature(
        std::string const& message,
        std::string const& signatureHex,
        PublicKey const& pubkey);

    bool
    verifySecp256k1Signature(
        std::string const& message,
        std::string const& signatureHex,
        PublicKey const& pubkey);

    void
    updateCombinedExclusions();

    std::string
    calculateMessageHash(std::string const& content);
};

} // namespace ripple

#endif // RIPPLE_APP_MISC_REMOTEEXCLUSIONLISTFETCHER_H_INCLUDED