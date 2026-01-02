#include <test/jtx.h>
#include <test/jtx/Env.h>

#include <xrpld/app/misc/DynamicUNLManager.h>
#include <xrpld/app/misc/UNLHashWatcher.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/digest.h>

namespace ripple {

class DynamicUNLManager_test final : public beast::unit_test::suite
{
    // Helper to create a realistic UNL JSON with validators and scores
    static std::string
    makeUNLDataJson(
        std::vector<std::pair<std::string, int>> const& validatorsWithScores)
    {
        std::string json = R"({"validators":[)";
        bool first = true;
        for (auto const& [pubkey, score] : validatorsWithScores)
        {
            if (!first)
                json += ",";
            first = false;
            json += R"({"pubkey":")" + pubkey + R"(","score":)" +
                std::to_string(score) + "}";
        }
        json += R"(],"version":1})";
        return json;
    }

public:
    void
    testParsing()
    {
        testcase("Parsing");

        using namespace test::jtx;

        Env env(*this, supported_amendments() | featureDynamicUNL);
        auto& manager = env.app().getDynamicUNLManager();

        // Test valid JSON parsing
        {
            std::string const json =
                R"({"validators":[{"pubkey":"ED_TEST_1","score":90},{"pubkey":"ED_TEST_2","score":80}],"version":1})";

            auto result = manager.parseUNLData(json);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->validators.size() == 2);
            BEAST_EXPECT(result->version == 1);
            BEAST_EXPECT(result->validators[0].pubkey == "ED_TEST_1");
            BEAST_EXPECT(result->validators[0].score == 90);
            BEAST_EXPECT(result->validators[1].pubkey == "ED_TEST_2");
            BEAST_EXPECT(result->validators[1].score == 80);
        }

        // Test invalid JSON
        {
            auto result = manager.parseUNLData("not valid json {{{");
            BEAST_EXPECT(!result.has_value());
        }

        // Test missing validators field
        {
            auto result = manager.parseUNLData(R"({"version":1})");
            BEAST_EXPECT(!result.has_value());
        }

        // Test missing version field
        {
            auto result = manager.parseUNLData(
                R"({"validators":[{"pubkey":"ED_TEST","score":50}]})");
            BEAST_EXPECT(!result.has_value());
        }

        // Test empty validators array
        {
            auto result =
                manager.parseUNLData(R"({"validators":[],"version":1})");
            BEAST_EXPECT(!result.has_value());
        }

        // Test validator missing pubkey
        {
            auto result = manager.parseUNLData(
                R"({"validators":[{"score":50}],"version":1})");
            BEAST_EXPECT(!result.has_value());
        }

        // Test validator missing score
        {
            auto result = manager.parseUNLData(
                R"({"validators":[{"pubkey":"ED_TEST"}],"version":1})");
            BEAST_EXPECT(!result.has_value());
        }

        // Test large score values (no clamping - scores can be any uint32)
        {
            std::string const json =
                R"({"validators":[{"pubkey":"ED_TEST","score":150}],"version":1})";

            auto result = manager.parseUNLData(json);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->validators[0].score == 150);
        }
    }

    void
    testSelection()
    {
        testcase("Selection");

        using namespace test::jtx;

        Env env(*this, supported_amendments() | featureDynamicUNL);
        auto& manager = env.app().getDynamicUNLManager();

        // Test selection of top validators by score
        {
            DynamicUNLManager::DynamicUNLData data;
            data.version = 1;
            data.validators = {
                {"ED_LOW", 10},
                {"ED_HIGH", 99},
                {"ED_MED", 50},
                {"ED_MEDHIGH", 75},
                {"ED_LOWEST", 5}};

            auto selected = manager.selectTopValidators(data);

            // All 5 validators should be selected (less than
            // MAX_UNL_VALIDATORS)
            BEAST_EXPECT(selected.size() == 5);

            // Should be sorted by score descending
            BEAST_EXPECT(selected[0].pubkey == "ED_HIGH");
            BEAST_EXPECT(selected[0].score == 99);
            BEAST_EXPECT(selected[1].pubkey == "ED_MEDHIGH");
            BEAST_EXPECT(selected[1].score == 75);
            BEAST_EXPECT(selected[2].pubkey == "ED_MED");
            BEAST_EXPECT(selected[2].score == 50);
            BEAST_EXPECT(selected[3].pubkey == "ED_LOW");
            BEAST_EXPECT(selected[3].score == 10);
            BEAST_EXPECT(selected[4].pubkey == "ED_LOWEST");
            BEAST_EXPECT(selected[4].score == 5);
        }

        // Verify MAX_UNL_VALIDATORS constant is accessible
        {
            BEAST_EXPECT(DynamicUNLManager::maxValidators() == 35);
            BEAST_EXPECT(MAX_UNL_VALIDATORS == 35);
        }
    }

    void
    testSelectExactlyMaxValidators()
    {
        testcase("Select Exactly Max Validators");

        using namespace test::jtx;

        Env env(*this, supported_amendments() | featureDynamicUNL);
        auto& manager = env.app().getDynamicUNLManager();

        // Create exactly MAX_UNL_VALIDATORS (35) validators
        DynamicUNLManager::DynamicUNLData data;
        data.version = 1;

        for (std::uint32_t i = 0; i < MAX_UNL_VALIDATORS; ++i)
        {
            data.validators.push_back(
                {"ED_VALIDATOR_" + std::to_string(i), 100 - i});
        }

        BEAST_EXPECT(data.validators.size() == MAX_UNL_VALIDATORS);

        auto selected = manager.selectTopValidators(data);

        // All 35 validators should be selected
        BEAST_EXPECT(selected.size() == MAX_UNL_VALIDATORS);

        // Should be sorted by score descending
        BEAST_EXPECT(selected[0].pubkey == "ED_VALIDATOR_0");
        BEAST_EXPECT(selected[0].score == 100);
        BEAST_EXPECT(selected[34].pubkey == "ED_VALIDATOR_34");
        BEAST_EXPECT(selected[34].score == 66);
    }

    void
    testSelectMoreThanMaxValidators()
    {
        testcase("Select More Than Max Validators");

        using namespace test::jtx;

        Env env(*this, supported_amendments() | featureDynamicUNL);
        auto& manager = env.app().getDynamicUNLManager();

        // Create 40 validators (more than MAX_UNL_VALIDATORS = 35)
        DynamicUNLManager::DynamicUNLData data;
        data.version = 1;

        for (std::uint32_t i = 0; i < 40; ++i)
        {
            data.validators.push_back(
                {"ED_VALIDATOR_" + std::to_string(i), 100 - i});
        }

        BEAST_EXPECT(data.validators.size() == 40);

        auto selected = manager.selectTopValidators(data);

        // Only top 35 should be selected
        BEAST_EXPECT(selected.size() == MAX_UNL_VALIDATORS);

        // Should have the top 35 validators (scores 100 down to 66)
        BEAST_EXPECT(selected[0].pubkey == "ED_VALIDATOR_0");
        BEAST_EXPECT(selected[0].score == 100);
        BEAST_EXPECT(selected[34].pubkey == "ED_VALIDATOR_34");
        BEAST_EXPECT(selected[34].score == 66);

        // Validator 35-39 (scores 65-61) should NOT be included
        for (auto const& v : selected)
        {
            BEAST_EXPECT(v.score >= 66);
        }
    }

    void
    testSelectWithTiedScores()
    {
        testcase("Select With Tied Scores");

        using namespace test::jtx;

        Env env(*this, supported_amendments() | featureDynamicUNL);
        auto& manager = env.app().getDynamicUNLManager();

        // Create validators with tied scores
        DynamicUNLManager::DynamicUNLData data;
        data.version = 1;
        data.validators = {
            {"ED_A", 50},
            {"ED_B", 50},
            {"ED_C", 100},
            {"ED_D", 50},
            {"ED_E", 75}};

        auto selected = manager.selectTopValidators(data);

        BEAST_EXPECT(selected.size() == 5);

        // Top two are clearly ordered
        BEAST_EXPECT(selected[0].pubkey == "ED_C");
        BEAST_EXPECT(selected[0].score == 100);
        BEAST_EXPECT(selected[1].pubkey == "ED_E");
        BEAST_EXPECT(selected[1].score == 75);

        // The three tied validators (A, B, D with score 50) should all appear
        // in positions 2, 3, 4 (order may vary based on sort stability)
        std::set<std::string> tiedPubkeys;
        for (size_t i = 2; i < 5; ++i)
        {
            BEAST_EXPECT(selected[i].score == 50);
            tiedPubkeys.insert(selected[i].pubkey);
        }
        BEAST_EXPECT(tiedPubkeys.count("ED_A") == 1);
        BEAST_EXPECT(tiedPubkeys.count("ED_B") == 1);
        BEAST_EXPECT(tiedPubkeys.count("ED_D") == 1);
    }

    void
    testProcessFetchedUNLNotConfigured()
    {
        testcase("Process Fetched UNL - Not Configured");

        using namespace test::jtx;

        Env env(*this, supported_amendments() | featureDynamicUNL);
        auto& manager = env.app().getDynamicUNLManager();
        auto& watcher = env.app().getUNLHashWatcher();

        // Watcher is NOT configured
        BEAST_EXPECT(!watcher.isConfigured());

        // Create valid UNL data
        std::vector<std::pair<std::string, int>> validators = {
            {"ED_VALIDATOR_1", 95}, {"ED_VALIDATOR_2", 88}};

        std::string const unlDataJson = makeUNLDataJson(validators);
        uint256 const hash = sha512Half(makeSlice(unlDataJson));

        // Should succeed even though watcher is not configured
        // (hash verification is skipped when not configured)
        auto result = manager.processFetchedUNL(unlDataJson, hash);
        BEAST_EXPECT(result.has_value());
        BEAST_EXPECT(result->size() == 2);
        BEAST_EXPECT((*result)[0].pubkey == "ED_VALIDATOR_1");
        BEAST_EXPECT((*result)[0].score == 95);
    }

    void
    testProcessFetchedUNLInvalidJson()
    {
        testcase("Process Fetched UNL - Invalid JSON");

        using namespace test::jtx;

        Env env(*this, supported_amendments() | featureDynamicUNL);
        auto& manager = env.app().getDynamicUNLManager();

        // Even with a valid hash, invalid JSON should fail
        std::string const invalidJson = "not valid json {{{";
        uint256 const hash = sha512Half(makeSlice(invalidJson));

        auto result = manager.processFetchedUNL(invalidJson, hash);
        BEAST_EXPECT(!result.has_value());
    }

    void
    testProcessFetchedUNLHashMismatch()
    {
        testcase("Process Fetched UNL - Hash Mismatch");

        using namespace test::jtx;

        Env env(*this, supported_amendments() | featureDynamicUNL);
        auto& manager = env.app().getDynamicUNLManager();
        auto& watcher = env.app().getUNLHashWatcher();

        Account const publisher{"publisher"};
        Account const memoAccount{"memoAccount"};

        env.fund(XRP(10000), publisher, memoAccount);
        env.close();

        // Configure watcher
        watcher.configure(publisher.id(), memoAccount.id());
        BEAST_EXPECT(watcher.isConfigured());

        // Create and apply a hash via the watcher
        std::vector<std::pair<std::string, int>> validators = {
            {"ED_VALIDATOR_1", 95}};
        std::string const unlDataJson = makeUNLDataJson(validators);
        uint256 const correctHash = sha512Half(makeSlice(unlDataJson));

        // Manually set up a current hash by creating a pending update
        // We need to send a transaction to do this properly
        std::string hashStr = to_string(correctHash);
        std::string memoJson = R"({"hash":")" + hashStr +
            R"(","effectiveLedger":100,"sequence":1})";
        env(pay(publisher, memoAccount, XRP(1)), memo(memoJson, "", ""));
        env.close();

        // Apply the pending update
        watcher.applyPendingUpdate();
        BEAST_EXPECT(watcher.getCurrentHash().has_value());

        // Now try to process with a wrong hash
        std::vector<std::pair<std::string, int>> tamperedValidators = {
            {"ED_ATTACKER", 100}};
        std::string const tamperedJson = makeUNLDataJson(tamperedValidators);
        uint256 const wrongHash = sha512Half(makeSlice(tamperedJson));

        // Should fail because hash doesn't match
        auto result = manager.processFetchedUNL(tamperedJson, wrongHash);
        BEAST_EXPECT(!result.has_value());
    }

    void
    run() override
    {
        testParsing();
        testSelection();
        testSelectExactlyMaxValidators();
        testSelectMoreThanMaxValidators();
        testSelectWithTiedScores();
        testProcessFetchedUNLNotConfigured();
        testProcessFetchedUNLInvalidJson();
        testProcessFetchedUNLHashMismatch();
    }
};

BEAST_DEFINE_TESTSUITE(DynamicUNLManager, app, ripple);

}  // namespace ripple
