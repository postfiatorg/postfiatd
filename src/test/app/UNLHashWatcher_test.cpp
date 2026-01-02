#include <test/jtx.h>
#include <test/jtx/Env.h>

#include <xrpld/app/misc/UNLHashWatcher.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

class UNLHashWatcher_test final : public beast::unit_test::suite
{
    // Helper to create a valid UNL hash JSON memo
    static std::string
    makeValidMemoJson(
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
    void
    testConfiguration()
    {
        testcase("Configuration");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        // Initially not configured
        BEAST_EXPECT(!watcher.isConfigured());

        // Configure with accounts
        Account master{"master"};
        Account memo{"memo"};

        watcher.configure(master.id(), memo.id());

        // Now configured
        BEAST_EXPECT(watcher.isConfigured());

        // Reconfigure with different accounts
        Account master2{"master2"};
        Account memo2{"memo2"};

        watcher.configure(master2.id(), memo2.id());

        // Still configured
        BEAST_EXPECT(watcher.isConfigured());
    }

    void
    testHashVerificationBootstrap()
    {
        testcase("Hash Verification - Bootstrap Mode");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        // Without any current hash set, any hash should pass (bootstrap mode)
        uint256 testHash1;
        BEAST_EXPECT(testHash1.parseHex(makeTestHash(1)));

        uint256 testHash2;
        BEAST_EXPECT(testHash2.parseHex(makeTestHash(2)));

        // Both should pass since no current hash is set
        BEAST_EXPECT(watcher.verifyHash(testHash1));
        BEAST_EXPECT(watcher.verifyHash(testHash2));

        // No current hash
        BEAST_EXPECT(!watcher.getCurrentHash().has_value());
    }

    void
    testSequenceEnforcement()
    {
        testcase("Sequence Enforcement");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        // Initial sequence should be 0
        BEAST_EXPECT(watcher.getHighestSequence() == 0);

        // After processing valid updates, sequence should increase
        // (Full test would require creating mock transactions)
    }

    void
    testPendingUpdateBasic()
    {
        testcase("Pending Update - Basic");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        // Initially no pending update
        BEAST_EXPECT(!watcher.getPendingUpdate().has_value());

        // Should not apply at any ledger without pending update
        BEAST_EXPECT(!watcher.shouldApplyPendingUpdate(256));
        BEAST_EXPECT(!watcher.shouldApplyPendingUpdate(512));
        BEAST_EXPECT(!watcher.shouldApplyPendingUpdate(0));
        BEAST_EXPECT(!watcher.shouldApplyPendingUpdate(UINT32_MAX));
    }

    void
    testApplyPendingUpdateWithoutPending()
    {
        testcase("Apply Pending Update - No Pending");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        // Calling applyPendingUpdate without a pending update should be safe
        // (just logs a warning)
        watcher.applyPendingUpdate();

        // Still no current hash
        BEAST_EXPECT(!watcher.getCurrentHash().has_value());
    }

    void
    testProcessTransactionNotConfigured()
    {
        testcase("Process Transaction - Not Configured");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        // Create a simple payment transaction
        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Create a payment with memo
        auto const memoJson = makeValidMemoJson(makeTestHash(1), 256, 1);
        env(pay(alice, bob, XRP(100)), test::jtx::memo(memoJson, "", ""));
        env.close();

        // Get the last transaction
        auto const txn = env.tx();
        BEAST_EXPECT(txn);

        // Should return false since watcher is not configured
        BEAST_EXPECT(!watcher.processTransaction(*txn));
    }

    void
    testProcessTransactionWrongSender()
    {
        testcase("Process Transaction - Wrong Sender");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        Account carol("carol");
        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        // Configure watcher to expect alice -> bob
        watcher.configure(alice.id(), bob.id());
        BEAST_EXPECT(watcher.isConfigured());

        // Create a payment from carol -> bob (wrong sender)
        auto const memoJson = makeValidMemoJson(makeTestHash(1), 256, 1);
        env(pay(carol, bob, XRP(100)), test::jtx::memo(memoJson, "", ""));
        env.close();

        auto const txn = env.tx();
        BEAST_EXPECT(txn);

        // Should return false since sender doesn't match
        BEAST_EXPECT(!watcher.processTransaction(*txn));
        BEAST_EXPECT(watcher.getHighestSequence() == 0);
    }

    void
    testProcessTransactionWrongDestination()
    {
        testcase("Process Transaction - Wrong Destination");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        Account carol("carol");
        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        // Configure watcher to expect alice -> bob
        watcher.configure(alice.id(), bob.id());

        // Create a payment from alice -> carol (wrong destination)
        auto const memoJson = makeValidMemoJson(makeTestHash(1), 256, 1);
        env(pay(alice, carol, XRP(100)), test::jtx::memo(memoJson, "", ""));
        env.close();

        auto const txn = env.tx();
        BEAST_EXPECT(txn);

        // Should return false since destination doesn't match
        BEAST_EXPECT(!watcher.processTransaction(*txn));
        BEAST_EXPECT(watcher.getHighestSequence() == 0);
    }

    void
    testProcessTransactionNoMemo()
    {
        testcase("Process Transaction - No Memo");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // Create a payment without memo
        env(pay(alice, bob, XRP(100)));
        env.close();

        auto const txn = env.tx();
        BEAST_EXPECT(txn);

        // Should return false since no memo
        BEAST_EXPECT(!watcher.processTransaction(*txn));
        BEAST_EXPECT(watcher.getHighestSequence() == 0);
    }

    void
    testProcessTransactionInvalidMemoJson()
    {
        testcase("Process Transaction - Invalid Memo JSON");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // Create a payment with invalid JSON memo
        env(pay(alice, bob, XRP(100)),
            test::jtx::memo("not valid json {{{", "", ""));
        env.close();

        auto const txn = env.tx();
        BEAST_EXPECT(txn);

        // Should return false since memo is not valid JSON
        BEAST_EXPECT(!watcher.processTransaction(*txn));
        BEAST_EXPECT(watcher.getHighestSequence() == 0);
    }

    void
    testProcessTransactionMissingFields()
    {
        testcase("Process Transaction - Missing Fields in Memo");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // Missing hash
        {
            env(pay(alice, bob, XRP(1)),
                test::jtx::memo(
                    R"({"effectiveLedger":256,"sequence":1})", "", ""));
            env.close();
            auto const txn = env.tx();
            BEAST_EXPECT(!watcher.processTransaction(*txn));
        }

        // Missing effectiveLedger
        {
            std::string memo =
                R"({"hash":")" + makeTestHash(1) + R"(","sequence":1})";
            env(pay(alice, bob, XRP(1)), test::jtx::memo(memo, "", ""));
            env.close();
            auto const txn = env.tx();
            BEAST_EXPECT(!watcher.processTransaction(*txn));
        }

        // Missing sequence
        {
            std::string memo = R"({"hash":")" + makeTestHash(1) +
                R"(","effectiveLedger":256})";
            env(pay(alice, bob, XRP(1)), test::jtx::memo(memo, "", ""));
            env.close();
            auto const txn = env.tx();
            BEAST_EXPECT(!watcher.processTransaction(*txn));
        }

        // Sequence should still be 0
        BEAST_EXPECT(watcher.getHighestSequence() == 0);
    }

    void
    testProcessTransactionInvalidHashLength()
    {
        testcase("Process Transaction - Invalid Hash Length");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // Hash too short (only 32 chars instead of 64)
        {
            std::string memo =
                R"({"hash":"01234567890123456789012345678901","effectiveLedger":256,"sequence":1})";
            env(pay(alice, bob, XRP(1)), test::jtx::memo(memo, "", ""));
            env.close();
            auto const txn = env.tx();
            BEAST_EXPECT(!watcher.processTransaction(*txn));
        }

        // Hash too long (128 chars)
        {
            std::string longHash = makeTestHash(1) + makeTestHash(2);
            std::string memo = R"({"hash":")" + longHash +
                R"(","effectiveLedger":256,"sequence":1})";
            env(pay(alice, bob, XRP(1)), test::jtx::memo(memo, "", ""));
            env.close();
            auto const txn = env.tx();
            BEAST_EXPECT(!watcher.processTransaction(*txn));
        }

        BEAST_EXPECT(watcher.getHighestSequence() == 0);
    }

    void
    testProcessTransactionInvalidHashHex()
    {
        testcase("Process Transaction - Invalid Hash Hex");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // Hash with invalid hex characters (G, H, etc.)
        std::string invalidHash =
            "GHIJKLMNGHIJKLMNGHIJKLMNGHIJKLMNGHIJKLMNGHIJKLMNGHIJKLMNGHIJKLMN";
        std::string memo = R"({"hash":")" + invalidHash +
            R"(","effectiveLedger":256,"sequence":1})";
        env(pay(alice, bob, XRP(1)), test::jtx::memo(memo, "", ""));
        env.close();

        auto const txn = env.tx();
        BEAST_EXPECT(!watcher.processTransaction(*txn));
        BEAST_EXPECT(watcher.getHighestSequence() == 0);
    }

    void
    testProcessTransactionValidUpdate()
    {
        testcase("Process Transaction - Valid Update");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // Create a valid UNL hash update memo
        std::string hash1 = makeTestHash(1);
        auto const memoJson = makeValidMemoJson(hash1, 256, 1);
        env(pay(alice, bob, XRP(1)), test::jtx::memo(memoJson, "", ""));
        env.close();

        auto const txn = env.tx();
        BEAST_EXPECT(txn);

        // Should succeed
        BEAST_EXPECT(watcher.processTransaction(*txn));

        // Sequence should be updated
        BEAST_EXPECT(watcher.getHighestSequence() == 1);

        // Should have a pending update
        auto pending = watcher.getPendingUpdate();
        BEAST_EXPECT(pending.has_value());
        BEAST_EXPECT(pending->effectiveLedger == 256);
        BEAST_EXPECT(pending->sequence == 1);

        // Current hash should still be empty (update is pending)
        BEAST_EXPECT(!watcher.getCurrentHash().has_value());
    }

    void
    testSequenceReplayProtection()
    {
        testcase("Sequence Replay Protection");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // First update with sequence 5
        {
            auto const memoJson = makeValidMemoJson(makeTestHash(1), 256, 5);
            env(pay(alice, bob, XRP(1)), test::jtx::memo(memoJson, "", ""));
            env.close();
            auto const txn = env.tx();
            BEAST_EXPECT(watcher.processTransaction(*txn));
            BEAST_EXPECT(watcher.getHighestSequence() == 5);
        }

        // Try to replay with same sequence (should fail)
        {
            auto const memoJson = makeValidMemoJson(makeTestHash(2), 512, 5);
            env(pay(alice, bob, XRP(1)), test::jtx::memo(memoJson, "", ""));
            env.close();
            auto const txn = env.tx();
            BEAST_EXPECT(!watcher.processTransaction(*txn));
            BEAST_EXPECT(watcher.getHighestSequence() == 5);
        }

        // Try with lower sequence (should fail)
        {
            auto const memoJson = makeValidMemoJson(makeTestHash(3), 512, 3);
            env(pay(alice, bob, XRP(1)), test::jtx::memo(memoJson, "", ""));
            env.close();
            auto const txn = env.tx();
            BEAST_EXPECT(!watcher.processTransaction(*txn));
            BEAST_EXPECT(watcher.getHighestSequence() == 5);
        }

        // Higher sequence should work
        {
            auto const memoJson = makeValidMemoJson(makeTestHash(4), 768, 10);
            env(pay(alice, bob, XRP(1)), test::jtx::memo(memoJson, "", ""));
            env.close();
            auto const txn = env.tx();
            BEAST_EXPECT(watcher.processTransaction(*txn));
            BEAST_EXPECT(watcher.getHighestSequence() == 10);
        }
    }

    void
    testEffectiveLedgerLogic()
    {
        testcase("Effective Ledger Logic");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // Create update effective at ledger 100
        auto const memoJson = makeValidMemoJson(makeTestHash(1), 100, 1);
        env(pay(alice, bob, XRP(1)), test::jtx::memo(memoJson, "", ""));
        env.close();

        auto const txn = env.tx();
        BEAST_EXPECT(watcher.processTransaction(*txn));

        // Should not apply before effective ledger
        BEAST_EXPECT(!watcher.shouldApplyPendingUpdate(50));
        BEAST_EXPECT(!watcher.shouldApplyPendingUpdate(99));

        // Should apply at or after effective ledger
        BEAST_EXPECT(watcher.shouldApplyPendingUpdate(100));
        BEAST_EXPECT(watcher.shouldApplyPendingUpdate(101));
        BEAST_EXPECT(watcher.shouldApplyPendingUpdate(1000));
    }

    void
    testApplyPendingUpdate()
    {
        testcase("Apply Pending Update");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // Create and process a valid update
        std::string hashStr = makeTestHash(42);
        auto const memoJson = makeValidMemoJson(hashStr, 100, 1);
        env(pay(alice, bob, XRP(1)), test::jtx::memo(memoJson, "", ""));
        env.close();

        auto const txn = env.tx();
        BEAST_EXPECT(watcher.processTransaction(*txn));

        // Pending update should exist
        BEAST_EXPECT(watcher.getPendingUpdate().has_value());

        // Current hash should be empty
        BEAST_EXPECT(!watcher.getCurrentHash().has_value());

        // Apply the pending update
        watcher.applyPendingUpdate();

        // Now pending should be cleared
        BEAST_EXPECT(!watcher.getPendingUpdate().has_value());

        // Current hash should be set
        auto currentHash = watcher.getCurrentHash();
        BEAST_EXPECT(currentHash.has_value());

        // Verify the hash value
        uint256 expectedHash;
        BEAST_EXPECT(expectedHash.parseHex(hashStr));
        BEAST_EXPECT(*currentHash == expectedHash);
    }

    void
    testHashVerificationAfterUpdate()
    {
        testcase("Hash Verification After Update");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // Create and apply an update
        std::string hashStr = makeTestHash(99);
        auto const memoJson = makeValidMemoJson(hashStr, 100, 1);
        env(pay(alice, bob, XRP(1)), test::jtx::memo(memoJson, "", ""));
        env.close();

        auto const txn = env.tx();
        BEAST_EXPECT(watcher.processTransaction(*txn));
        watcher.applyPendingUpdate();

        // The correct hash should verify
        uint256 correctHash;
        BEAST_EXPECT(correctHash.parseHex(hashStr));
        BEAST_EXPECT(watcher.verifyHash(correctHash));

        // A different hash should fail
        uint256 wrongHash;
        BEAST_EXPECT(wrongHash.parseHex(makeTestHash(100)));
        BEAST_EXPECT(!watcher.verifyHash(wrongHash));
    }

    void
    testVersionField()
    {
        testcase("Version Field");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // Test with explicit version
        {
            auto const memoJson = makeValidMemoJson(makeTestHash(1), 256, 1, 2);
            env(pay(alice, bob, XRP(1)), test::jtx::memo(memoJson, "", ""));
            env.close();
            auto const txn = env.tx();
            BEAST_EXPECT(watcher.processTransaction(*txn));

            auto pending = watcher.getPendingUpdate();
            BEAST_EXPECT(pending.has_value());
            BEAST_EXPECT(pending->version == 2);
        }
    }

    void
    testMultipleSequentialUpdates()
    {
        testcase("Multiple Sequential Updates");

        using namespace test::jtx;
        Env env(*this);

        auto& watcher = env.app().getUNLHashWatcher();

        Account alice("alice");
        Account bob("bob");
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Configure watcher
        watcher.configure(alice.id(), bob.id());

        // Process multiple updates in sequence
        for (std::uint32_t seq = 1; seq <= 5; ++seq)
        {
            auto const memoJson =
                makeValidMemoJson(makeTestHash(seq), 100 * seq, seq);
            env(pay(alice, bob, XRP(1)), test::jtx::memo(memoJson, "", ""));
            env.close();
            auto const txn = env.tx();
            BEAST_EXPECT(watcher.processTransaction(*txn));
            BEAST_EXPECT(watcher.getHighestSequence() == seq);

            auto pending = watcher.getPendingUpdate();
            BEAST_EXPECT(pending.has_value());
            BEAST_EXPECT(pending->sequence == seq);
            BEAST_EXPECT(pending->effectiveLedger == 100 * seq);
        }
    }

    void
    run() override
    {
        testConfiguration();
        testHashVerificationBootstrap();
        testSequenceEnforcement();
        testPendingUpdateBasic();
        testApplyPendingUpdateWithoutPending();
        testProcessTransactionNotConfigured();
        testProcessTransactionWrongSender();
        testProcessTransactionWrongDestination();
        testProcessTransactionNoMemo();
        testProcessTransactionInvalidMemoJson();
        testProcessTransactionMissingFields();
        testProcessTransactionInvalidHashLength();
        testProcessTransactionInvalidHashHex();
        testProcessTransactionValidUpdate();
        testSequenceReplayProtection();
        testEffectiveLedgerLogic();
        testApplyPendingUpdate();
        testHashVerificationAfterUpdate();
        testVersionField();
        testMultipleSequentialUpdates();
    }
};

BEAST_DEFINE_TESTSUITE(UNLHashWatcher, app, ripple);

}  // namespace ripple
