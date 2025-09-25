#include <xrpld/app/misc/RemoteExclusionListFetcher.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/detail/WorkFile.h>
#include <xrpld/app/misc/detail/WorkPlain.h>
#include <xrpld/app/misc/detail/WorkSSL.h>
#include <xrpl/basics/base64.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/Sign.h>
#include <algorithm>

namespace ripple {

RemoteExclusionListFetcher::RemoteExclusionListFetcher(
    Application& app,
    Config const& config,
    beast::Journal journal)
    : app_(app)
    , config_(config)
    , j_(journal)
    , timer_{app.getIOService()}
{
    fetchResults_.resize(config.VALIDATOR_EXCLUSIONS_SOURCES.size());

    JLOG(j_.info()) << "RemoteExclusionListFetcher: Initialized with "
                    << config.VALIDATOR_EXCLUSIONS_SOURCES.size()
                    << " remote sources";
}

RemoteExclusionListFetcher::~RemoteExclusionListFetcher()
{
    stop();
}

void
RemoteExclusionListFetcher::start()
{
    std::lock_guard lock(mutex_);

    if (running_)
        return;

    if (config_.VALIDATOR_EXCLUSIONS_SOURCES.empty())
    {
        JLOG(j_.info()) << "RemoteExclusionListFetcher: No remote sources configured";
        return;
    }

    running_ = true;
    stopping_ = false;

    // Fetch immediately on start
    fetchAllLists();

    // Schedule periodic fetches
    scheduleNextFetch();

    JLOG(j_.info()) << "RemoteExclusionListFetcher: Started with interval "
                    << config_.VALIDATOR_EXCLUSIONS_INTERVAL.count() << " seconds";
}

void
RemoteExclusionListFetcher::stop()
{
    {
        std::lock_guard lock(mutex_);
        if (!running_)
            return;
        running_ = false;
        stopping_ = true;
    }

    timer_.cancel();

    // Cancel any ongoing work
    if (auto sp = work_.lock())
        sp->cancel();

    // Wait for any fetching to complete
    while (fetching_)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    JLOG(j_.info()) << "RemoteExclusionListFetcher: Stopped";
}

void
RemoteExclusionListFetcher::scheduleNextFetch()
{
    if (!running_ || stopping_)
        return;

    timer_.expires_after(config_.VALIDATOR_EXCLUSIONS_INTERVAL);
    timer_.async_wait([this](error_code const& ec) {
        if (ec || !running_ || stopping_)
            return;

        fetchAllLists();
        scheduleNextFetch();
    });
}

void
RemoteExclusionListFetcher::fetchAllLists()
{
    if (fetching_.exchange(true))
    {
        JLOG(j_.debug()) << "RemoteExclusionListFetcher: Already fetching";
        return;
    }

    JLOG(j_.debug()) << "RemoteExclusionListFetcher: Fetching from all sources";

    // Reset fetch results
    fetchResults_.clear();
    fetchResults_.resize(config_.VALIDATOR_EXCLUSIONS_SOURCES.size());

    // Start fetching from all sources
    for (std::size_t i = 0; i < config_.VALIDATOR_EXCLUSIONS_SOURCES.size(); ++i)
    {
        fetchFromSource(config_.VALIDATOR_EXCLUSIONS_SOURCES[i], i);
    }
}

void
RemoteExclusionListFetcher::fetchFromSource(
    Config::ValidatorExclusionSource const& source,
    std::size_t sourceIdx)
{
    try
    {
        makeRequest(source, sourceIdx);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "RemoteExclusionListFetcher: Exception starting fetch from "
                        << source.url << ": " << e.what();

        fetchResults_[sourceIdx].success = false;
        fetchResults_[sourceIdx].errorMessage = e.what();

        // Check if all fetches are complete
        bool allComplete = true;
        for (auto const& result : fetchResults_)
        {
            if (!result.success && result.errorMessage.empty())
            {
                allComplete = false;
                break;
            }
        }

        if (allComplete)
        {
            onAllFetchesComplete();
        }
    }
}

void
RemoteExclusionListFetcher::makeRequest(
    Config::ValidatorExclusionSource const& source,
    std::size_t sourceIdx)
{
    parsedURL pUrl;
    if (!parseUrl(pUrl, source.url))
    {
        throw std::runtime_error("Invalid URL: " + source.url);
    }

    std::shared_ptr<detail::Work> sp;

    auto onFetch = [this, sourceIdx](
                       error_code const& ec,
                       boost::asio::ip::tcp::endpoint const& endpoint,
                       detail::response_type&& res) {
        // We don't use the endpoint, but it's required by the callback signature
        onFetchComplete(ec, std::move(res), sourceIdx);
    };

    auto onFileFetch = [this, sourceIdx](
                           error_code const& ec,
                           std::string const& res) {
        this->onTextFetch(ec, res, sourceIdx);
    };

    JLOG(j_.debug()) << "RemoteExclusionListFetcher: Starting request for " << source.url;

    if (pUrl.scheme == "https")
    {
        if (pUrl.domain.empty())
            throw std::runtime_error("https URI must contain a hostname");

        if (!pUrl.port)
            pUrl.port = 443;

        sp = std::make_shared<detail::WorkSSL>(
            pUrl.domain,
            pUrl.path.empty() ? "/" : pUrl.path,
            std::to_string(*pUrl.port),
            app_.getIOService(),
            j_,
            app_.config(),
            boost::asio::ip::tcp::endpoint{},  // No cached endpoint
            false,  // Not using cached endpoint
            onFetch);
    }
    else if (pUrl.scheme == "http")
    {
        if (pUrl.domain.empty())
            throw std::runtime_error("http URI must contain a hostname");

        if (!pUrl.port)
            pUrl.port = 80;

        sp = std::make_shared<detail::WorkPlain>(
            pUrl.domain,
            pUrl.path.empty() ? "/" : pUrl.path,
            std::to_string(*pUrl.port),
            app_.getIOService(),
            boost::asio::ip::tcp::endpoint{},  // No cached endpoint
            false,  // Not using cached endpoint
            onFetch);
    }
    else if (pUrl.scheme == "file")
    {
        if (!pUrl.domain.empty())
            throw std::runtime_error("file URI cannot contain a hostname");

#if BOOST_OS_WINDOWS
        // Paths on Windows need the leading / removed
        if (pUrl.path[0] == '/')
            pUrl.path = pUrl.path.substr(1);
#endif

        if (pUrl.path.empty())
            throw std::runtime_error("file URI must contain a path");

        sp = std::make_shared<detail::WorkFile>(
            pUrl.path,
            app_.getIOService(),
            onFileFetch);
    }
    else
    {
        throw std::runtime_error("Unsupported scheme: " + pUrl.scheme);
    }

    work_ = sp;
    sp->run();
}

void
RemoteExclusionListFetcher::onFetchComplete(
    error_code const& ec,
    detail::response_type&& res,
    std::size_t sourceIdx)
{
    auto const& source = config_.VALIDATOR_EXCLUSIONS_SOURCES[sourceIdx];

    if (ec)
    {
        JLOG(j_.warn()) << "RemoteExclusionListFetcher: Error fetching from "
                       << source.url << ": " << ec.message();

        fetchResults_[sourceIdx].success = false;
        fetchResults_[sourceIdx].errorMessage = ec.message();
    }
    else
    {
        using namespace boost::beast::http;

        if (res.result() == status::ok)
        {
            onTextFetch(ec, res.body(), sourceIdx);
            return;  // onTextFetch will handle completion
        }
        else
        {
            JLOG(j_.warn()) << "RemoteExclusionListFetcher: Bad HTTP status from "
                           << source.url << ": " << res.result_int();

            fetchResults_[sourceIdx].success = false;
            fetchResults_[sourceIdx].errorMessage = "HTTP " + std::to_string(res.result_int());
        }
    }

    // Check if all fetches are complete
    checkAllFetchesComplete();
}

void
RemoteExclusionListFetcher::onTextFetch(
    error_code const& ec,
    std::string const& res,
    std::size_t sourceIdx)
{
    auto const& source = config_.VALIDATOR_EXCLUSIONS_SOURCES[sourceIdx];

    if (ec)
    {
        JLOG(j_.warn()) << "RemoteExclusionListFetcher: Error reading from "
                       << source.url << ": " << ec.message();

        fetchResults_[sourceIdx].success = false;
        fetchResults_[sourceIdx].errorMessage = ec.message();
    }
    else
    {
        // Parse and verify the response
        auto list = parseExclusionList(res);
        if (list && verifySignature(*list, source.pubkey, res))
        {
            list->verified = true;
            fetchResults_[sourceIdx].success = true;
            fetchResults_[sourceIdx].list = list;

            JLOG(j_.info()) << "RemoteExclusionListFetcher: Successfully fetched and verified from "
                           << source.url;
        }
        else
        {
            JLOG(j_.error()) << "RemoteExclusionListFetcher: Failed to parse or verify from "
                            << source.url;

            fetchResults_[sourceIdx].success = false;
            fetchResults_[sourceIdx].errorMessage = "Parse/verify failed";
        }
    }

    checkAllFetchesComplete();
}

void
RemoteExclusionListFetcher::checkAllFetchesComplete()
{
    // Check if all fetches are complete
    for (std::size_t i = 0; i < fetchResults_.size(); ++i)
    {
        if (!fetchResults_[i].success && fetchResults_[i].errorMessage.empty())
        {
            // This fetch hasn't completed yet
            return;
        }
    }

    // All fetches are complete
    onAllFetchesComplete();
}

void
RemoteExclusionListFetcher::onAllFetchesComplete()
{
    JLOG(j_.debug()) << "RemoteExclusionListFetcher: All fetches complete";

    std::unordered_map<std::string, ExclusionList> newLists;
    bool allSuccessful = true;
    size_t successCount = 0;

    for (std::size_t i = 0; i < fetchResults_.size(); ++i)
    {
        auto const& source = config_.VALIDATOR_EXCLUSIONS_SOURCES[i];
        auto const& result = fetchResults_[i];

        if (result.success && result.list)
        {
            newLists[source.url] = *result.list;
            successCount++;
        }
        else
        {
            allSuccessful = false;
        }
    }

    // Update cached lists and combined exclusions
    {
        std::lock_guard lock(mutex_);

        // Only update if ALL sources were successfully fetched
        // or if this is not the initial fetch and we had some successes
        if (allSuccessful)
        {
            cachedLists_ = std::move(newLists);
            allSourcesAccessible_ = true;
            lastSuccessfulFetchTime_ = std::chrono::steady_clock::now();
            updateCombinedExclusions();
            
            JLOG(j_.info()) << "RemoteExclusionListFetcher: All sources accessible, updating exclusions";
        }
        else if (initialFetchComplete_ && successCount > 0)
        {
            // After initial fetch, we can continue with partial updates
            // but we mark that not all sources are accessible
            JLOG(j_.warn()) << "RemoteExclusionListFetcher: Only " << successCount << "/"
                           << config_.VALIDATOR_EXCLUSIONS_SOURCES.size()
                           << " sources accessible, keeping existing exclusions";
            allSourcesAccessible_ = false;
        }
        else if (!initialFetchComplete_)
        {
            // On initial fetch, if we can't reach all sources, don't use any exclusions
            JLOG(j_.error()) << "RemoteExclusionListFetcher: Initial fetch failed - not all sources accessible. "
                            << "No remote exclusions will be used.";
            cachedLists_.clear();
            combinedExclusions_.clear();
            allSourcesAccessible_ = false;
        }

        initialFetchComplete_ = true;
    }

    fetching_ = false;
}

std::optional<RemoteExclusionListFetcher::ExclusionList>
RemoteExclusionListFetcher::parseExclusionList(std::string const& content)
{
    try
    {
        Json::Value json;
        Json::Reader reader;

        if (!reader.parse(content, json))
        {
            JLOG(j_.error()) << "RemoteExclusionListFetcher: Failed to parse JSON";
            return std::nullopt;
        }

        ExclusionList list;

        // Parse basic fields
        if (json.isMember("version"))
            list.version = json["version"].asString();

        if (json.isMember("timestamp"))
            list.timestamp = json["timestamp"].asString();

        if (json.isMember("issuer_address"))
        {
            auto issuer = parseBase58<AccountID>(json["issuer_address"].asString());
            if (!issuer)
            {
                JLOG(j_.error()) << "RemoteExclusionListFetcher: Invalid issuer address";
                return std::nullopt;
            }
            list.issuerAddress = *issuer;
        }

        // Parse exclusions entries
        if (json.isMember("exclusions") && json["exclusions"].isArray())
        {
            for (auto const& entry : json["exclusions"])
            {
                ExclusionEntry exclusion;

                if (entry.isMember("address"))
                {
                    auto addr = parseBase58<AccountID>(entry["address"].asString());
                    if (!addr)
                    {
                        JLOG(j_.warn()) << "RemoteExclusionListFetcher: Skipping invalid address in exclusions";
                        continue;
                    }
                    exclusion.address = *addr;
                }

                if (entry.isMember("reason"))
                    exclusion.reason = entry["reason"].asString();

                if (entry.isMember("date_added"))
                    exclusion.dateAdded = entry["date_added"].asString();

                list.blacklist.push_back(std::move(exclusion));
            }
        }

        return list;
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "RemoteExclusionListFetcher: Parse error: " << e.what();
    }

    return std::nullopt;
}

bool
RemoteExclusionListFetcher::verifySignature(
    ExclusionList const& list,
    std::string const& pubkeyStr,
    std::string const& rawContent)
{
    try
    {
        Json::Value json;
        Json::Reader reader;

        if (!reader.parse(rawContent, json))
            return false;

        if (!json.isMember("signature"))
            return false;

        auto const& sig = json["signature"];

        std::string algorithm = sig.isMember("algorithm") ? sig["algorithm"].asString() : "";
        std::string sigPubKey = sig.isMember("public_key") ? sig["public_key"].asString() : "";
        std::string signature = sig.isMember("signature") ? sig["signature"].asString() : "";

        // Verify the public key matches what's configured
        if (sigPubKey != pubkeyStr)
        {
            JLOG(j_.error()) << "RemoteExclusionListFetcher: Public key mismatch";
            return false;
        }

        // Create a canonical message to verify by concatenating sorted addresses
        // This avoids JSON serialization issues
        std::vector<std::string> addresses;
        addresses.reserve(list.blacklist.size());

        for (auto const& entry : list.blacklist)
        {
            addresses.push_back(toBase58(entry.address));
        }

        // Sort addresses to ensure consistent ordering
        std::sort(addresses.begin(), addresses.end());

        // Create message as concatenated sorted addresses
        std::string message;
        for (auto const& addr : addresses)
        {
            message += addr;
            message+="\n";
        }

        // Add version and timestamp for additional verification
        message = "v1:" + list.version + ":" + list.timestamp + ":" +
                  toBase58(list.issuerAddress) + ":" + message;

        // Parse the public key
        auto pubkey = parseBase58<PublicKey>(TokenType::NodePublic, pubkeyStr);
        if (!pubkey)
        {
            JLOG(j_.error()) << "RemoteExclusionListFetcher: Invalid public key format";
            return false;
        }

        // Verify based on algorithm
        if (algorithm == "ed25519")
        {
            return verifyEd25519Signature(message, signature, *pubkey);
        }
        else if (algorithm == "secp256k1")
        {
            return verifySecp256k1Signature(message, signature, *pubkey);
        }
        else
        {
            JLOG(j_.error()) << "RemoteExclusionListFetcher: Unsupported algorithm: " << algorithm;
            return false;
        }
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "RemoteExclusionListFetcher: Signature verification error: " << e.what();
    }

    return false;
}



bool
RemoteExclusionListFetcher::verifyEd25519Signature(
    std::string const& message,
    std::string const& signatureHex,
    PublicKey const& pubkey)
{
    try
    {
        // Decode hex signature
        auto sigDecoded = strUnHex(signatureHex);
        if (!sigDecoded || sigDecoded->size() != 64)
        {
            JLOG(j_.error()) << "RemoteExclusionListFetcher: Invalid Ed25519 signature size";
            return false;
        }

        // Verify the signature using verify
        return verify(pubkey, makeSlice(message), makeSlice(*sigDecoded), true);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "RemoteExclusionListFetcher: Ed25519 verification error: " << e.what();
    }

    return false;
}

bool
RemoteExclusionListFetcher::verifySecp256k1Signature(
    std::string const& message,
    std::string const& signatureHex,
    PublicKey const& pubkey)
{
    try
    {
        // Decode hex signature
        auto sigDecoded = strUnHex(signatureHex);
        if (!sigDecoded)
        {
            JLOG(j_.error()) << "RemoteExclusionListFetcher: Failed to decode secp256k1 signature";
            return false;
        }

        // Hash the message
        auto const messageHash = sha512Half(makeSlice(message));

        // Verify the signature using verifyDigest
        return verifyDigest(pubkey, messageHash, makeSlice(*sigDecoded), false);
    }
    catch (std::exception const& e)
    {
        JLOG(j_.error()) << "RemoteExclusionListFetcher: Secp256k1 verification error: " << e.what();
    }

    return false;
}

void
RemoteExclusionListFetcher::updateCombinedExclusions()
{
    std::unordered_set<AccountID> newCombined;

    for (auto const& [url, list] : cachedLists_)
    {
        if (list.verified)
        {
            for (auto const& entry : list.blacklist)
            {
                newCombined.insert(entry.address);
            }
        }
    }

    // Check if the combined list has changed
    if (newCombined != combinedExclusions_)
    {
        combinedExclusions_ = std::move(newCombined);
        // Only set modifications flag if all sources are accessible
        // This prevents partial updates from triggering changes
        if (allSourcesAccessible_)
        {
            hasModifications_ = true;
            lastUpdateTime_ = std::chrono::steady_clock::now();

            JLOG(j_.info()) << "RemoteExclusionListFetcher: Combined exclusions CHANGED, "
                            << combinedExclusions_.size() << " total addresses from "
                            << cachedLists_.size() << " verified sources";
        }
        else
        {
            JLOG(j_.warn()) << "RemoteExclusionListFetcher: Exclusions changed but not all sources accessible, "
                           << "not marking as modified";
        }
    }
    else
    {
        JLOG(j_.debug()) << "RemoteExclusionListFetcher: Combined exclusions unchanged, "
                        << combinedExclusions_.size() << " total addresses from "
                        << cachedLists_.size() << " verified sources";
    }
}

std::unordered_set<AccountID>
RemoteExclusionListFetcher::getCombinedExclusions() const
{
    std::lock_guard lock(mutex_);
    return combinedExclusions_;
}

bool
RemoteExclusionListFetcher::isRunning() const
{
    std::lock_guard lock(mutex_);
    return running_;
}

bool
RemoteExclusionListFetcher::hasBeenModified(bool resetFlag)
{
    std::lock_guard lock(mutex_);
    bool modified = hasModifications_;
    if (resetFlag)
    {
        hasModifications_ = false;
    }
    return modified;
}

bool
RemoteExclusionListFetcher::areAllSourcesAccessible() const
{
    std::lock_guard lock(mutex_);
    return allSourcesAccessible_;
}

bool
RemoteExclusionListFetcher::isInitialFetchComplete() const
{
    std::lock_guard lock(mutex_);
    return initialFetchComplete_;
}

std::chrono::steady_clock::time_point
RemoteExclusionListFetcher::getLastUpdateTime() const
{
    std::lock_guard lock(mutex_);
    return lastUpdateTime_;
}

} // namespace ripple