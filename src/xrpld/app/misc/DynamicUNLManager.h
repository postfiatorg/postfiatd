#ifndef RIPPLE_APP_MISC_DYNAMICUNLMANAGER_H_INCLUDED
#define RIPPLE_APP_MISC_DYNAMICUNLMANAGER_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ripple {

class Application;

// TODO: Research optimal UNL size for network security, decentralization and
// performance.
static constexpr std::uint32_t MAX_UNL_VALIDATORS = 35;

class DynamicUNLManager
{
public:
    struct ValidatorScore
    {
        std::string pubkey;
        std::uint32_t score;

        bool
        operator<(ValidatorScore const& other) const
        {
            return score > other.score;
        }
    };

    struct DynamicUNLData
    {
        std::vector<ValidatorScore> validators;
        std::uint32_t version;
    };

private:
    Application& app_;
    beast::Journal j_;

public:
    explicit DynamicUNLManager(Application& app, beast::Journal journal);
    ~DynamicUNLManager() = default;

    std::optional<DynamicUNLData>
    parseUNLData(std::string const& jsonStr) const;

    std::vector<ValidatorScore>
    selectTopValidators(DynamicUNLData const& data) const;

    std::optional<std::vector<ValidatorScore>>
    processFetchedUNL(std::string const& rawJson, uint256 const& computedHash);

    static constexpr std::uint32_t
    maxValidators()
    {
        return MAX_UNL_VALIDATORS;
    }
};

}  // namespace ripple

#endif
