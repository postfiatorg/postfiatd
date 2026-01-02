#include <test/jtx.h>
#include <test/jtx/Env.h>

#include <xrpld/app/misc/DynamicUNLManager.h>
#include <xrpld/app/misc/UNLHashWatcher.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <vector>

namespace ripple {
namespace test {

/**
 * Integration tests for the Dynamic UNL feature.
 *
 * This tests the full flow:
 * 1. Publisher sends a payment with UNL hash in memo
 * 2. Transaction is validated in a ledger
 * 3. UNLHashWatcher receives the transaction via BuildLedger
 * 4. Pending update is stored
 * 5. At flag ledger, pending update becomes current
 * 6. ValidatorSite can verify fetched UNL hashes
 *
 */
class DynamicUNL_test : public beast::unit_test::suite
{
    // Helper to create a valid UNL hash JSON memo
    static std::string
    makeUNLHashMemo(
        std::string const& hash,
        std::uint32_t effectiveLedger,
        std::uint32_t sequence,
        std::optional<std::uint32_t> version = std::nullopt)
    {
        std::string json = R"({"hash":")" + hash + R"(","effectiveLedger":)" +
            std::to_string(effectiveLedger) + R"(,"sequence":)" +
            std::to_string(sequence);
        if (version)
        {
            json += R"(,"version":)" + std::to_string(*version);
        }
        json += "}";
        return json;
    }

    // Helper to create a test hash (64 hex chars = 256 bits)
    static std::string
    makeTestHash(int seed = 1)
    {
        std::string hash;
        for (int i = 0; i < 64; ++i)
        {
            hash += "0123456789ABCDEF"[(i + seed) % 16];
        }
        return hash;
    }

public:
    // Helper to create a realistic UNL JSON with validators and scores
    // This matches the format expected by DynamicUNLManager::parseUNLData
    static std::string
    makeUNLDataJson(
        std::vector<std::pair<std::string, int>> const& validatorsWithScores)
    {
        // Format:
        // {
        //   "validators": [
        //     {"pubkey": "ED...", "score": 95},
        //     {"pubkey": "ED...", "score": 90},
        //     ...
        //   ],
        //   "version": 1
        // }
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

    void
    testEndToEndFlow()
    {
        testcase("End-to-End Flow");

        // This test demonstrates the complete Dynamic UNL flow:
        // 1. Publisher creates UNL data with 5 validators and their scores
        // 2. Only top 3 (maxValidators=3) should be selected for UNL
        // 3. Publisher computes hash of UNL data and publishes on-chain
        // 4. Node verifies fetched UNL data matches the on-chain hash
        // 5. Node selects top N validators by score

        using namespace jtx;

        // Create environment with DynamicUNL amendment enabled
        Env env(*this, supported_amendments() | featureDynamicUNL);

        // Create publisher (master) and memo destination accounts
        Account const publisher{"publisher"};
        Account const memoAccount{"memoAccount"};

        env.fund(XRP(10000), publisher, memoAccount);
        env.close();

        auto& watcher = env.app().getUNLHashWatcher();

        // Configure the watcher with publisher accounts
        watcher.configure(publisher.id(), memoAccount.id());
        BEAST_EXPECT(watcher.isConfigured());

        // Initially no pending or current update
        BEAST_EXPECT(!watcher.getPendingUpdate().has_value());
        BEAST_EXPECT(!watcher.getCurrentHash().has_value());
        BEAST_EXPECT(watcher.getHighestSequence() == 0);

        // ============================================================
        // STEP 1: Publisher creates UNL data with validators and scores
        // ============================================================
        // 5 validators with different scores
        // Note: In production, pubkeys would be real Ed25519 public keys
        std::vector<std::pair<std::string, int>> validatorsWithScores = {
            {"ED_VALIDATOR_A_PUBKEY_PLACEHOLDER", 85},   // 4th place
            {"ED_VALIDATOR_B_PUBKEY_PLACEHOLDER", 92},   // 2nd place
            {"ED_VALIDATOR_C_PUBKEY_PLACEHOLDER", 78},   // 5th place
            {"ED_VALIDATOR_D_PUBKEY_PLACEHOLDER", 95},   // 1st place (highest)
            {"ED_VALIDATOR_E_PUBKEY_PLACEHOLDER", 88}};  // 3rd place

        // Create the UNL data JSON
        // Note: maxValidators is now a constant (MAX_UNL_VALIDATORS = 35)
        // defined in DynamicUNLManager.h, not part of the JSON
        std::string const unlDataJson = makeUNLDataJson(validatorsWithScores);

        // ============================================================
        // STEP 2: Compute hash and publish on-chain
        // ============================================================
        uint256 const unlHash = sha512Half(makeSlice(unlDataJson));
        std::string const hashStr = to_string(unlHash);

        std::uint32_t const effectiveLedger = 256;  // Flag ledger
        std::uint32_t const sequence = 1;

        auto const memoJson =
            makeUNLHashMemo(hashStr, effectiveLedger, sequence);

        // Publisher sends payment to memoAccount with the UNL hash memo
        env(pay(publisher, memoAccount, XRP(1)), memo(memoJson, "", ""));
        env.close();

        // ============================================================
        // STEP 3: Verify hash was received and stored
        // ============================================================
        BEAST_EXPECT(watcher.getPendingUpdate().has_value());
        BEAST_EXPECT(watcher.getHighestSequence() == 1);

        auto pending = watcher.getPendingUpdate();
        BEAST_EXPECT(pending->sequence == sequence);
        BEAST_EXPECT(pending->effectiveLedger == effectiveLedger);

        // Apply the pending update (simulating flag ledger)
        watcher.applyPendingUpdate();
        BEAST_EXPECT(watcher.getCurrentHash().has_value());

        // ============================================================
        // STEP 4: Simulate ValidatorSite fetching and verifying
        // ============================================================
        // Node fetches UNL data from external source
        // Node computes hash and verifies against on-chain hash
        uint256 const fetchedHash = sha512Half(makeSlice(unlDataJson));
        BEAST_EXPECT(watcher.verifyHash(fetchedHash));

        // ============================================================
        // STEP 5: Use DynamicUNLManager to parse and select validators
        // ============================================================
        // Get DynamicUNLManager from the application
        auto& dynamicManager = env.app().getDynamicUNLManager();

        // Parse the UNL JSON using the real implementation
        auto parsedData = dynamicManager.parseUNLData(unlDataJson);
        BEAST_EXPECT(parsedData.has_value());
        BEAST_EXPECT(parsedData->validators.size() == 5);
        BEAST_EXPECT(parsedData->version == 1);

        // Select top validators using the real implementation
        auto const selectedValidators =
            dynamicManager.selectTopValidators(*parsedData);

        // Should have 5 validators (less than MAX_UNL_VALIDATORS)
        BEAST_EXPECT(selectedValidators.size() == 5);

        // Verify validators are sorted by score (highest first)
        // Scores: D=95, B=92, E=88, A=85, C=78
        BEAST_EXPECT(
            selectedValidators[0].pubkey ==
            "ED_VALIDATOR_D_PUBKEY_PLACEHOLDER");  // 95
        BEAST_EXPECT(selectedValidators[0].score == 95);
        BEAST_EXPECT(
            selectedValidators[1].pubkey ==
            "ED_VALIDATOR_B_PUBKEY_PLACEHOLDER");  // 92
        BEAST_EXPECT(selectedValidators[1].score == 92);
        BEAST_EXPECT(
            selectedValidators[2].pubkey ==
            "ED_VALIDATOR_E_PUBKEY_PLACEHOLDER");  // 88
        BEAST_EXPECT(selectedValidators[2].score == 88);
        BEAST_EXPECT(
            selectedValidators[3].pubkey ==
            "ED_VALIDATOR_A_PUBKEY_PLACEHOLDER");  // 85
        BEAST_EXPECT(selectedValidators[3].score == 85);
        BEAST_EXPECT(
            selectedValidators[4].pubkey ==
            "ED_VALIDATOR_C_PUBKEY_PLACEHOLDER");  // 78
        BEAST_EXPECT(selectedValidators[4].score == 78);
    }

    void
    testValidatorSiteIntegration()
    {
        testcase("ValidatorSite Integration - Hash Verification");

        // This test demonstrates the full flow using DynamicUNLManager:
        // 1. Publisher creates UNL JSON with validators and scores
        // 2. Publisher computes sha512Half of the JSON
        // 3. Publisher publishes hash on-chain via memo
        // 4. Node fetches UNL from external source
        // 5. DynamicUNLManager verifies hash and selects top validators
        //
        // If hashes match: UNL is accepted
        // If hashes differ: UNL is rejected (prevents tampering)

        using namespace jtx;

        Env env(*this, supported_amendments() | featureDynamicUNL);

        Account const publisher{"publisher"};
        Account const memoAccount{"memoAccount"};

        env.fund(XRP(10000), publisher, memoAccount);
        env.close();

        auto& watcher = env.app().getUNLHashWatcher();
        watcher.configure(publisher.id(), memoAccount.id());

        auto& dynamicManager = env.app().getDynamicUNLManager();

        // Create UNL data in the proper Dynamic UNL format
        std::vector<std::pair<std::string, int>> validators = {
            {"ED_VALIDATOR_1", 95},
            {"ED_VALIDATOR_2", 88},
            {"ED_VALIDATOR_3", 75}};

        std::string const unlDataJson = makeUNLDataJson(validators);

        // Compute the hash
        uint256 const realHash = sha512Half(makeSlice(unlDataJson));
        std::string const hashStr = to_string(realHash);

        // Publisher publishes this hash on-chain
        {
            auto const memoJson = makeUNLHashMemo(hashStr, 256, 1);
            env(pay(publisher, memoAccount, XRP(1)), memo(memoJson, "", ""));
            env.close();
        }

        BEAST_EXPECT(watcher.getPendingUpdate().has_value());

        // Apply the pending update (simulating flag ledger)
        watcher.applyPendingUpdate();
        BEAST_EXPECT(watcher.getCurrentHash().has_value());

        // Case 1: Use DynamicUNLManager to process the fetched UNL
        // (No tampering - hash matches)
        {
            auto result =
                dynamicManager.processFetchedUNL(unlDataJson, realHash);
            BEAST_EXPECT(result.has_value());
            BEAST_EXPECT(result->size() == 3);
            // Validators should be sorted by score
            BEAST_EXPECT((*result)[0].pubkey == "ED_VALIDATOR_1");
            BEAST_EXPECT((*result)[0].score == 95);
            BEAST_EXPECT((*result)[1].pubkey == "ED_VALIDATOR_2");
            BEAST_EXPECT((*result)[1].score == 88);
            BEAST_EXPECT((*result)[2].pubkey == "ED_VALIDATOR_3");
            BEAST_EXPECT((*result)[2].score == 75);
        }

        // Case 2: Tampered data - hash mismatch
        // (Attacker modified the UNL - should be rejected)
        {
            std::vector<std::pair<std::string, int>> tamperedValidators = {
                {"ED_ATTACKER", 100}};
            std::string const tamperedData =
                makeUNLDataJson(tamperedValidators);
            uint256 const tamperedHash = sha512Half(makeSlice(tamperedData));

            // The tampered hash won't match the on-chain hash
            BEAST_EXPECT(!watcher.verifyHash(tamperedHash));

            // processFetchedUNL should also fail because hash doesn't match
            auto result =
                dynamicManager.processFetchedUNL(tamperedData, tamperedHash);
            BEAST_EXPECT(!result.has_value());
        }
    }

    void
    testWithoutAmendment()
    {
        testcase("Without DynamicUNL Amendment");

        using namespace jtx;

        // Create environment WITHOUT DynamicUNL amendment
        Env env(*this, supported_amendments() - featureDynamicUNL);

        Account const publisher{"publisher"};
        Account const memoAccount{"memoAccount"};

        env.fund(XRP(10000), publisher, memoAccount);
        env.close();

        auto& watcher = env.app().getUNLHashWatcher();
        watcher.configure(publisher.id(), memoAccount.id());

        // Send UNL hash update
        {
            auto const memoJson = makeUNLHashMemo(makeTestHash(1), 256, 1);
            env(pay(publisher, memoAccount, XRP(1)), memo(memoJson, "", ""));
            env.close();
        }

        // Without the amendment, BuildLedger won't call processTransaction
        // So the watcher should NOT have received the update
        // Note: The watcher itself still works, but the integration isn't
        // active This test verifies the amendment gate in BuildLedger.cpp
        BEAST_EXPECT(!watcher.getPendingUpdate().has_value());
        BEAST_EXPECT(watcher.getHighestSequence() == 0);
    }

    void
    testFlagLedgerAutoApplication()
    {
        testcase("Flag Ledger Auto Application");

        // This test verifies that BuildLedger automatically applies
        // pending UNL hash updates at flag ledgers (every 256 ledgers).
        // See BuildLedger.cpp lines 57-67.

        using namespace jtx;

        Env env(*this, supported_amendments() | featureDynamicUNL);

        Account const publisher{"publisher"};
        Account const memoAccount{"memoAccount"};

        env.fund(XRP(10000), publisher, memoAccount);
        env.close();

        auto& watcher = env.app().getUNLHashWatcher();
        watcher.configure(publisher.id(), memoAccount.id());

        // Get current ledger sequence
        auto const startSeq = env.closed()->info().seq;

        // Calculate the next flag ledger (multiples of 256)
        // Flag ledgers: 0, 256, 512, ...
        constexpr LedgerIndex FLAG_INTERVAL = 256;
        LedgerIndex const nextFlagLedger =
            ((startSeq / FLAG_INTERVAL) + 1) * FLAG_INTERVAL;

        // Set effectiveLedger to a value that will be valid at the flag ledger
        // Using the current ledger + 1 ensures it's immediately effective
        LedgerIndex const effectiveLedger = startSeq + 1;

        // Create UNL data and compute hash
        std::vector<std::pair<std::string, int>> validators = {
            {"ED_VALIDATOR_1", 95}, {"ED_VALIDATOR_2", 88}};

        std::string const unlDataJson = makeUNLDataJson(validators);
        uint256 const unlHash = sha512Half(makeSlice(unlDataJson));
        std::string const hashStr = to_string(unlHash);

        // Send the UNL hash update
        {
            auto const memoJson = makeUNLHashMemo(hashStr, effectiveLedger, 1);
            env(pay(publisher, memoAccount, XRP(1)), memo(memoJson, "", ""));
            env.close();
        }

        // Verify pending update was received
        BEAST_EXPECT(watcher.getPendingUpdate().has_value());
        BEAST_EXPECT(!watcher.getCurrentHash().has_value());

        // Verify shouldApplyPendingUpdate logic
        auto const currentSeq = env.closed()->info().seq;
        BEAST_EXPECT(watcher.shouldApplyPendingUpdate(currentSeq));

        // Advance to the next flag ledger
        // Note: This may take some time for large gaps
        while (env.closed()->info().seq < nextFlagLedger)
        {
            env.close();
        }

        // Verify we're at a flag ledger
        BEAST_EXPECT(env.closed()->info().seq % FLAG_INTERVAL == 0);

        // At this point, BuildLedger should have automatically applied
        // the pending update when it built the flag ledger
        // The pending update should now be the current update
        BEAST_EXPECT(watcher.getCurrentHash().has_value());
        BEAST_EXPECT(!watcher.getPendingUpdate().has_value());

        // Verify the hash matches
        BEAST_EXPECT(*watcher.getCurrentHash() == unlHash);
        BEAST_EXPECT(watcher.verifyHash(unlHash));
    }

    void
    run() override
    {
        testEndToEndFlow();
        testValidatorSiteIntegration();
        testWithoutAmendment();
        testFlagLedgerAutoApplication();
    }
};

BEAST_DEFINE_TESTSUITE(DynamicUNL, app, ripple);

}  // namespace test
}  // namespace ripple
