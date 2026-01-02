#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/DynamicUNLManager.h>
#include <xrpld/app/misc/UNLHashWatcher.h>

#include <xrpl/json/json_reader.h>

#include <algorithm>

namespace ripple {

DynamicUNLManager::DynamicUNLManager(Application& app, beast::Journal journal)
    : app_(app), j_(journal)
{
}

std::optional<DynamicUNLManager::DynamicUNLData>
DynamicUNLManager::parseUNLData(std::string const& jsonStr) const
{
    Json::Value root;
    Json::Reader reader;

    if (!reader.parse(jsonStr, root))
    {
        JLOG(j_.warn()) << "DynamicUNLManager: JSON parse error";
        return std::nullopt;
    }

    if (!root.isObject() || !root.isMember("validators") ||
        !root["validators"].isArray() || !root.isMember("version") ||
        !root["version"].isIntegral())
    {
        JLOG(j_.warn()) << "DynamicUNLManager: Invalid JSON structure";
        return std::nullopt;
    }

    DynamicUNLData data;
    data.version = root["version"].asUInt();

    Json::Value const& validators = root["validators"];
    auto const count = validators.size();
    data.validators.reserve(count);

    for (unsigned int i = 0; i < count; ++i)
    {
        Json::Value const& v = validators[i];

        if (!v.isObject() || !v.isMember("pubkey") || !v["pubkey"].isString() ||
            !v.isMember("score") || !v["score"].isIntegral())
        {
            continue;
        }

        ValidatorScore vs;
        vs.pubkey = v["pubkey"].asString();
        vs.score = v["score"].asUInt();

        if (vs.pubkey.empty())
            continue;

        data.validators.push_back(std::move(vs));
    }

    if (data.validators.empty())
    {
        JLOG(j_.warn()) << "DynamicUNLManager: No valid validators";
        return std::nullopt;
    }

    JLOG(j_.debug()) << "DynamicUNLManager: Parsed " << data.validators.size()
                     << " validators";

    return data;
}

std::vector<DynamicUNLManager::ValidatorScore>
DynamicUNLManager::selectTopValidators(DynamicUNLData const& data) const
{
    std::vector<ValidatorScore> sorted = data.validators;
    std::sort(sorted.begin(), sorted.end());

    std::size_t const n =
        std::min(static_cast<std::size_t>(MAX_UNL_VALIDATORS), sorted.size());

    return std::vector<ValidatorScore>(sorted.begin(), sorted.begin() + n);
}

std::optional<std::vector<DynamicUNLManager::ValidatorScore>>
DynamicUNLManager::processFetchedUNL(
    std::string const& rawJson,
    uint256 const& computedHash)
{
    auto& watcher = app_.getUNLHashWatcher();

    if (watcher.isConfigured() && !watcher.verifyHash(computedHash))
    {
        JLOG(j_.warn()) << "DynamicUNLManager: Hash mismatch";
        return std::nullopt;
    }

    auto data = parseUNLData(rawJson);
    if (!data)
        return std::nullopt;

    return selectTopValidators(*data);
}

}  // namespace ripple
