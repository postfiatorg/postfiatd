//------------------------------------------------------------------------------
/*
    This file is part of postfiatd
    Copyright (c) 2024 PostFiat Developers

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/handlers/Handlers.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class OrchardFullFlow_test : public beast::unit_test::suite
{
public:
    // Helper to check if result contains an error
    bool
    contains_error(Json::Value const& result)
    {
        return result.isMember(jss::error) ||
               result.isMember(jss::error_message);
    }

    void
    testOrchardFullFlowWithStaticKeys()
    {
        testcase("orchard_full_flow_with_static_keys");

        using namespace jtx;
        // Enable OrchardPrivacy feature for ShieldedPayment transactions
        // Use kWarning threshold to see JLOG(j_.warn()) messages from ShieldedPayment::doApply
        Env env(*this, envconfig(), supported_amendments() | featureOrchardPrivacy, nullptr, beast::severities::kWarning);

        // Use the EXACT same keys as the Python script
        // FIRST account (receives in t→z at address 62B5..., spends in z→z using SK D871...)
        std::string const firstFvk = "935993F09041BB701B12FF053AD9D9F4F9051C8BDD70B39D62F4765B4E71F812BA38D514E952E84654C55F276BAC3986B5BCC4B2A21E253FA16EFF658394AD1B0924F4F8C86C42C9ECC983F135BA3C9D4D9493F51DCCE7ACDCF5A6C7E26D1923";
        std::string const firstSk = "D8710D7D8D4717F313C1B1F49CA82AA5FA64B7AAD0D51BC671B8EB0E06E3DC99";
        std::string const firstAddr = "62B565D0A77917E0CBE360A30D081259A35DB3186AC533278632BFA6003E09576EC955B72EC71FDA33E39D";

        // SECOND account (receives in z→z at address C195...)
        std::string const secondFvk = "32962E110407C099A57D577C7750BE7078A4729F08ED908727181DC0D4A2BD27DA4ED74993A50179C45BD63FDD0CC381058BB0DE6847B17FA3FF4B459C06562E925A5F23BA4DCC23A279220F887F46F31BEAA0CD63E6DD87790F4C4071627F08";
        std::string const secondSk = "E47A50F38ACBADB0B839AE3A089E9988665B4E13819B64A4A4D3B3F5CB7FA0B3";
        std::string const secondAddr = "C19558DB8066177BF73AAD65280FC53378A082A3FA5CEE57218D3A5F846E24201CC6978222B9AE2B4F1D95";

        // Step 1: Add FIRST viewing key to wallet (BEFORE any transactions)
        {
            Json::Value params;
            params[jss::full_viewing_key] = secondFvk;
            auto const result =
                env.rpc("json", "orchard_wallet_add_key", to_string(params));

            BEAST_EXPECT(!contains_error(result[jss::result]));
            BEAST_EXPECT(result[jss::result]["tracked_keys"].asUInt() == 1);

            log << "Step 1: First viewing key added, anchor: "
                << (result[jss::result].isMember("anchor") ? result[jss::result]["anchor"].asString() : "NONE")
                << std::endl;
        }

        // Step 2: Add SECOND viewing key to wallet (BEFORE any transactions)
        {
            Json::Value params;
            params[jss::full_viewing_key] = firstFvk;
            auto const result =
                env.rpc("json", "orchard_wallet_add_key", to_string(params));

            BEAST_EXPECT(!contains_error(result[jss::result]));
            BEAST_EXPECT(result[jss::result]["tracked_keys"].asUInt() == 2);

            log << "Step 2: Second viewing key added, anchor: "
                << (result[jss::result].isMember("anchor") ? result[jss::result]["anchor"].asString() : "NONE")
                << std::endl;
        }

        // Step 3: Create transparent account to fund the shielded address
        Account alice("alice");
        env.fund(XRP(10000), alice);
        env.close();

        // Step 4: Send t→z (transparent to shielded) to fund FIRST account's shielded address
        std::string const fundAmount = "1000000000";  // 1000 XRP in drops

        Json::Value tToZParams;
        tToZParams[jss::payment_type] = "t_to_z";
        tToZParams[jss::source_account] = alice.human();
        tToZParams[jss::recipient] = firstAddr;
        tToZParams[jss::amount] = fundAmount;

        auto const tToZResult =
            env.rpc("json", "orchard_prepare_payment", to_string(tToZParams));

        BEAST_EXPECT(!contains_error(tToZResult[jss::result]));
        BEAST_EXPECT(tToZResult[jss::result][jss::payment_type].asString() == "t_to_z");

        log << "Step 3: t→z payment prepared" << std::endl;

        // Step 5: Submit the t→z transaction
        auto const& txJson = tToZResult[jss::result][jss::tx_json];

        Json::Value submitParams;
        submitParams[jss::tx_json] = txJson;
        submitParams[jss::secret] = alice.name();

        auto const submitResult =
            env.rpc("json", "submit", to_string(submitParams));

        BEAST_EXPECT(!contains_error(submitResult[jss::result]));
        BEAST_EXPECT(submitResult[jss::result]["engine_result"].asString() == "tesSUCCESS");

        log << "Step 4: t→z transaction submitted with tesSUCCESS" << std::endl;

        // Step 6: Close ledger to process the transaction
        env.close();

        log << "Step 5: Ledger closed" << std::endl;

        // Step 7: Scan balance for FIRST viewing key (after t→z)
        {
            Json::Value scanParams;
            scanParams[jss::full_viewing_key] = firstFvk;
            scanParams[jss::ledger_index_min] = 1;
            scanParams[jss::ledger_index_max] = env.current()->seq();

            auto const scanResult =
                env.rpc("json", "orchard_scan_balance", to_string(scanParams));

            BEAST_EXPECT(!contains_error(scanResult[jss::result]));

            log << "Step 6: First key balance after t→z: "
                << scanResult[jss::result]["total_balance_xrp"].asString() << " XRP, "
                << scanResult[jss::result]["note_count"].asUInt() << " notes"
                << std::endl;
        }

        // Step 8: Prepare z→z payment (FIRST account spends, SECOND account receives)
        std::string const sendAmount = "500000000";  // 500 XRP in drops

        Json::Value zToZParams;
        zToZParams[jss::payment_type] = "z_to_z";
        zToZParams[jss::spending_key] = firstSk;  // FIRST account's spending key
        zToZParams[jss::recipient] = secondAddr;  // SECOND account's address
        zToZParams[jss::amount] = sendAmount;

        auto const zToZResult =
            env.rpc("json", "orchard_prepare_payment", to_string(zToZParams));

        log << "Step 8: Preparing z→z payment" << std::endl;

        // Step 8b: Prepare ANOTHER z→z payment with same spending key (for double-spend test later)
        // Use the second account's address as recipient (doesn't matter where it goes, we just want a conflicting spend)
        Json::Value doubleSpendParams;
        doubleSpendParams[jss::payment_type] = "z_to_z";
        doubleSpendParams[jss::spending_key] = firstSk;  // SAME spending key!
        doubleSpendParams[jss::recipient] = secondAddr;  // Use valid address
        doubleSpendParams[jss::amount] = "100000000";  // Different amount (100 XRP)

        auto const doubleSpendResult =
            env.rpc("json", "orchard_prepare_payment", to_string(doubleSpendParams));

        if (contains_error(doubleSpendResult[jss::result]))
        {
            log << "Step 8b ERROR: Failed to prepare double-spend transaction: "
                << doubleSpendResult[jss::result]["error_message"].asString() << std::endl;
        }
        else
        {
            log << "Step 8b: Prepared double-spend z→z payment (will test later)" << std::endl;
        }

        // Step 9: Verify z→z transaction was prepared successfully
        if (contains_error(zToZResult[jss::result]))
        {
            // Log error for debugging
            log << "z→z preparation FAILED: "
                << (zToZResult[jss::result].isMember(jss::error_message) ?
                    zToZResult[jss::result][jss::error_message].asString() : "unknown error")
                << std::endl;

            // This SHOULD succeed with our anchor fix!
            BEAST_EXPECT(false);  // Fail the test
        }
        else
        {
            BEAST_EXPECT(zToZResult[jss::result].isMember(jss::tx_json));
            BEAST_EXPECT(zToZResult[jss::result][jss::payment_type].asString() == "z_to_z");

            log << "Step 8: z→z payment prepared successfully, bundle size: "
                << zToZResult[jss::result][jss::bundle_size].asUInt() << " bytes"
                << std::endl;

            // Step 10: Submit the z→z transaction
            auto const& zToZTxJson = zToZResult[jss::result][jss::tx_json];

            Json::Value zToZSubmitParams;
            zToZSubmitParams[jss::tx_json] = zToZTxJson;

            auto const zToZSubmitResult =
                env.rpc("json", "submit", to_string(zToZSubmitParams));

            // Log the full result for debugging
            if (contains_error(zToZSubmitResult[jss::result]))
            {
                log << "Step 9: z→z submission ERROR: "
                    << (zToZSubmitResult[jss::result].isMember(jss::error) ?
                        zToZSubmitResult[jss::result][jss::error].asString() : "unknown")
                    << ", message: "
                    << (zToZSubmitResult[jss::result].isMember(jss::error_message) ?
                        zToZSubmitResult[jss::result][jss::error_message].asString() : "none")
                    << std::endl;
            }

            // THIS is the critical check - the z→z transaction should succeed
            BEAST_EXPECT(!contains_error(zToZSubmitResult[jss::result]));

            std::string const engineResult = zToZSubmitResult[jss::result]["engine_result"].asString();
            log << "Step 9: z→z transaction result: " << engineResult << std::endl;

            // The anchor should be valid!
            BEAST_EXPECT(engineResult == "tesSUCCESS");

            // Close ledger to process z→z transaction
            env.close();

            log << "Step 10: Ledger closed" << std::endl;

            // Step 11: Scan balance for SECOND account (recipient, after z→z)
            {
                Json::Value scanParams;
                scanParams[jss::full_viewing_key] = secondFvk;
                scanParams[jss::ledger_index_min] = 1;
                scanParams[jss::ledger_index_max] = env.current()->seq();

                auto const scanResult =
                    env.rpc("json", "orchard_scan_balance", to_string(scanParams));

                BEAST_EXPECT(!contains_error(scanResult[jss::result]));

                log << "Step 11: Recipient balance after z→z: "
                    << scanResult[jss::result]["total_balance_xrp"].asString() << " XRP, "
                    << scanResult[jss::result]["note_count"].asUInt() << " notes"
                    << std::endl;
            }

            // Step 12: Create z→t transaction (second account unshields 200 XRP to alice)
            std::string const zToTAmount = "200000000";  // 200 XRP in drops

            Json::Value zToTParams;
            zToTParams[jss::payment_type] = "z_to_t";
            zToTParams[jss::spending_key] = secondSk;
            zToTParams[jss::amount] = zToTAmount;
            zToTParams[jss::destination_account] = alice.human();

            auto const zToTResult =
                env.rpc("json", "orchard_prepare_payment", to_string(zToTParams));

            if (contains_error(zToTResult[jss::result]))
            {
                log << "Step 12: z→t preparation FAILED: "
                    << (zToTResult[jss::result].isMember(jss::error_message) ?
                        zToTResult[jss::result][jss::error_message].asString() : "unknown error")
                    << std::endl;

                BEAST_EXPECT(false);
            }
            else
            {
                BEAST_EXPECT(zToTResult[jss::result].isMember(jss::tx_json));
                BEAST_EXPECT(zToTResult[jss::result][jss::payment_type].asString() == "z_to_t");

                log << "Step 12: z→t payment prepared, bundle size: "
                    << zToTResult[jss::result][jss::bundle_size].asUInt() << " bytes"
                    << std::endl;

                // Step 13: Submit the z→t transaction
                auto const& zToTTxJson = zToTResult[jss::result][jss::tx_json];

                // Verify the transaction has the correct fields
                BEAST_EXPECT(zToTTxJson[jss::Destination].asString() == alice.human());
                BEAST_EXPECT(zToTTxJson[jss::Amount].asString() == zToTAmount);

                // Get alice's balance before the transaction
                auto const aliceBalanceBefore = env.balance(alice);

                Json::Value zToTSubmitParams;
                zToTSubmitParams[jss::tx_json] = zToTTxJson;

                auto const zToTSubmitResult =
                    env.rpc("json", "submit", to_string(zToTSubmitParams));

                if (contains_error(zToTSubmitResult[jss::result]))
                {
                    log << "Step 13: z→t submission ERROR: "
                        << (zToTSubmitResult[jss::result].isMember(jss::error) ?
                            zToTSubmitResult[jss::result][jss::error].asString() : "unknown")
                        << ", message: "
                        << (zToTSubmitResult[jss::result].isMember(jss::error_message) ?
                            zToTSubmitResult[jss::result][jss::error_message].asString() : "none")
                        << std::endl;
                }

                BEAST_EXPECT(!contains_error(zToTSubmitResult[jss::result]));

                std::string const engineResult = zToTSubmitResult[jss::result]["engine_result"].asString();
                log << "Step 13: z→t transaction result: " << engineResult << std::endl;

                BEAST_EXPECT(engineResult == "tesSUCCESS");

                // Close ledger to process z→t transaction
                env.close();

                log << "Step 14: Ledger closed" << std::endl;

                // Step 15: Verify alice's balance increased by the unshielded amount
                auto const aliceBalanceAfter = env.balance(alice);
                auto const expectedIncrease = XRP(200);  // 200 XRP

                log << "Step 15: Alice balance before: " << aliceBalanceBefore << ", after: " << aliceBalanceAfter
                    << ", expected increase: " << expectedIncrease << std::endl;

                BEAST_EXPECT(aliceBalanceAfter == aliceBalanceBefore + expectedIncrease);

                // Step 16: Scan balance for second account (should show one spent note)
                {
                    Json::Value scanParams;
                    scanParams[jss::full_viewing_key] = secondFvk;
                    scanParams[jss::ledger_index_min] = 1;
                    scanParams[jss::ledger_index_max] = env.current()->seq();

                    auto const scanResult =
                        env.rpc("json", "orchard_scan_balance", to_string(scanParams));

                    BEAST_EXPECT(!contains_error(scanResult[jss::result]));

                    log << "Step 16: Second account balance after z→t: "
                        << scanResult[jss::result]["total_balance_xrp"].asString() << " XRP, "
                        << scanResult[jss::result]["note_count"].asUInt() << " notes, "
                        << scanResult[jss::result]["spent_count"].asUInt() << " spent"
                        << std::endl;

                    // Should have at least 1 spent note (from z→t spend)
                    BEAST_EXPECT(scanResult[jss::result]["spent_count"].asUInt() >= 1);
                }

                // Step 17: Attempt double-spend (should FAIL)
                // We prepared this transaction earlier using the same note that was already spent
                log << "\n=== Step 17: Testing double-spend detection ===" << std::endl;

                if (!contains_error(doubleSpendResult[jss::result]))
                {
                    log << "Step 17: Submitting the double-spend transaction prepared in Step 8b..." << std::endl;

                    auto const& doubleSpendTxJson = doubleSpendResult[jss::result][jss::tx_json];

                    Json::Value doubleSpendSubmitParams;
                    doubleSpendSubmitParams[jss::tx_json] = doubleSpendTxJson;

                    auto const doubleSpendSubmitResult =
                        env.rpc("json", "submit", to_string(doubleSpendSubmitParams));

                    // This SHOULD fail because the nullifier was already revealed in step 9
                    std::string const doubleSpendEngineResult =
                        doubleSpendSubmitResult[jss::result]["engine_result"].asString();

                    log << "Step 17: Double-spend attempt result: " << doubleSpendEngineResult << std::endl;

                    // Verify the double-spend was rejected
                    // Expected: temINVALID or similar error (nullifier already revealed)
                    BEAST_EXPECT(doubleSpendEngineResult != "tesSUCCESS");

                    if (doubleSpendEngineResult == "tesSUCCESS")
                    {
                        log << "ERROR: Double-spend was NOT detected! This is a critical security bug!" << std::endl;
                    }
                    else
                    {
                        log << "SUCCESS: Double-spend correctly rejected with " << doubleSpendEngineResult << std::endl;
                    }
                }
                else
                {
                    log << "Step 17: SKIPPED - Could not prepare double-spend transaction in Step 8b" << std::endl;
                    log << "Error was: " << doubleSpendResult[jss::result]["error_message"].asString() << std::endl;
                }
            }
        }
    }

    void
    testOrchardDoubleTransactions()
    {
        testcase("orchard_double_transactions");

        using namespace jtx;
        // Enable OrchardPrivacy feature for ShieldedPayment transactions
        Env env(*this, envconfig(), supported_amendments() | featureOrchardPrivacy, nullptr, beast::severities::kWarning);

        // Use the EXACT same keys as the Python script
        // FIRST account
        std::string const firstFvk = "935993F09041BB701B12FF053AD9D9F4F9051C8BDD70B39D62F4765B4E71F812BA38D514E952E84654C55F276BAC3986B5BCC4B2A21E253FA16EFF658394AD1B0924F4F8C86C42C9ECC983F135BA3C9D4D9493F51DCCE7ACDCF5A6C7E26D1923";
        std::string const firstSk = "D8710D7D8D4717F313C1B1F49CA82AA5FA64B7AAD0D51BC671B8EB0E06E3DC99";
        std::string const firstAddr = "62B565D0A77917E0CBE360A30D081259A35DB3186AC533278632BFA6003E09576EC955B72EC71FDA33E39D";

        // SECOND account
        std::string const secondFvk = "32962E110407C099A57D577C7750BE7078A4729F08ED908727181DC0D4A2BD27DA4ED74993A50179C45BD63FDD0CC381058BB0DE6847B17FA3FF4B459C06562E925A5F23BA4DCC23A279220F887F46F31BEAA0CD63E6DD87790F4C4071627F08";
        std::string const secondSk = "E47A50F38ACBADB0B839AE3A089E9988665B4E13819B64A4A4D3B3F5CB7FA0B3";
        std::string const secondAddr = "C19558DB8066177BF73AAD65280FC53378A082A3FA5CEE57218D3A5F846E24201CC6978222B9AE2B4F1D95";

        // Helper lambda to submit a transaction
        auto submitTx = [&](std::string const& type, std::string const& amount,
                           std::string const& from, std::string const& to,
                           std::string const& sk = "", std::string const& secret = "") {
            Json::Value params;
            params[jss::payment_type] = type;
            params[jss::amount] = amount;

            if (type == "t_to_z") {
                params[jss::source_account] = from;
                params[jss::recipient] = to;
            } else if (type == "z_to_z") {
                params[jss::spending_key] = sk;
                params[jss::recipient] = to;
            } else if (type == "z_to_t") {
                params[jss::spending_key] = sk;
                params[jss::destination_account] = to;
            }

            auto const result = env.rpc("json", "orchard_prepare_payment", to_string(params));
            BEAST_EXPECT(!contains_error(result[jss::result]));

            Json::Value submitParams;
            submitParams[jss::tx_json] = result[jss::result][jss::tx_json];
            if (!secret.empty()) {
                submitParams[jss::secret] = secret;
            }

            auto const submitResult = env.rpc("json", "submit", to_string(submitParams));
            std::string const engineResult = submitResult[jss::result]["engine_result"].asString();
            log << "  Transaction result: " << engineResult << std::endl;

            return engineResult == "tesSUCCESS";
        };

        // Helper lambda to scan and verify balance
        auto scanBalance = [&](std::string const& fvk, double expectedMin, double expectedMax, std::string const& label) {
            Json::Value scanParams;
            scanParams[jss::full_viewing_key] = fvk;
            scanParams[jss::ledger_index_min] = 1;
            scanParams[jss::ledger_index_max] = env.current()->seq();

            auto const scanResult = env.rpc("json", "orchard_scan_balance", to_string(scanParams));
            BEAST_EXPECT(!contains_error(scanResult[jss::result]));

            std::string const balance = scanResult[jss::result]["total_balance_xrp"].asString();
            double balanceValue = std::stod(balance);

            log << label << ": " << balance << " XRP" << std::endl;
            BEAST_EXPECT(balanceValue >= expectedMin && balanceValue <= expectedMax);

            return balanceValue;
        };

        // Step 1: Add viewing keys
        {
            Json::Value params;
            params[jss::full_viewing_key] = secondFvk;
            auto const result = env.rpc("json", "orchard_wallet_add_key", to_string(params));
            BEAST_EXPECT(!contains_error(result[jss::result]));
            log << "Step 1: First viewing key added" << std::endl;
        }

        {
            Json::Value params;
            params[jss::full_viewing_key] = firstFvk;
            auto const result = env.rpc("json", "orchard_wallet_add_key", to_string(params));
            BEAST_EXPECT(!contains_error(result[jss::result]));
            log << "Step 2: Second viewing key added" << std::endl;
        }

        // Step 3: Create transparent account
        Account alice("alice");
        env.fund(XRP(10000), alice);
        env.close();

        // ========== LEDGER 1: 4 transactions (2 t→z, 2 t→z) ==========
        log << "\n=== LEDGER 1: 4 t→z transactions ===" << std::endl;

        log << "Ledger 1, Tx 1: t→z 1000 XRP to first account" << std::endl;
        bool tx1 = submitTx("t_to_z", "1000000000", alice.human(), firstAddr, "", alice.name());
        BEAST_EXPECT(tx1);

        log << "Ledger 1, Tx 2: t→z 800 XRP to second account" << std::endl;
        bool tx2 = submitTx("t_to_z", "800000000", alice.human(), secondAddr, "", alice.name());
        BEAST_EXPECT(tx2);

        log << "Ledger 1, Tx 3: t→z 500 XRP to first account" << std::endl;
        bool tx3 = submitTx("t_to_z", "500000000", alice.human(), firstAddr, "", alice.name());
        BEAST_EXPECT(tx3);

        log << "Ledger 1, Tx 4: t→z 300 XRP to second account" << std::endl;
        bool tx4 = submitTx("t_to_z", "300000000", alice.human(), secondAddr, "", alice.name());
        BEAST_EXPECT(tx4);

        env.close();
        log << "Ledger 1 closed" << std::endl;

        // Verify balances after Ledger 1
        // First: 1000 + 500 = 1500 XRP
        // Second: 800 + 300 = 1100 XRP
        double bal1 = scanBalance(firstFvk, 1499.9, 1500.1, "After Ledger 1, First account");
        double bal2 = scanBalance(secondFvk, 1099.9, 1100.1, "After Ledger 1, Second account");

        // ========== LEDGER 2: 4 transactions (2 t→z, 1 z→z, 1 z→t) ==========
        log << "\n=== LEDGER 2: 4 transactions (2 t→z, 1 z→z, 1 z→t) ===" << std::endl;

        log << "Ledger 2, Tx 1: t→z 200 XRP to first account" << std::endl;
        bool tx5 = submitTx("t_to_z", "200000000", alice.human(), firstAddr, "", alice.name());
        BEAST_EXPECT(tx5);

        log << "Ledger 2, Tx 2: t→z 150 XRP to second account" << std::endl;
        bool tx6 = submitTx("t_to_z", "150000000", alice.human(), secondAddr, "", alice.name());
        BEAST_EXPECT(tx6);

        log << "Ledger 2, Tx 3: z→z 400 XRP from first to second" << std::endl;
        bool tx7 = submitTx("z_to_z", "400000000", "", secondAddr, firstSk);
        BEAST_EXPECT(tx7);

        log << "Ledger 2, Tx 4: z→t 100 XRP from second to alice" << std::endl;
        bool tx8 = submitTx("z_to_t", "100000000", "", alice.human(), secondSk);
        BEAST_EXPECT(tx8);

        env.close();
        log << "Ledger 2 closed" << std::endl;

        // Verify balances after Ledger 2
        // First: 1500 + 200 - 400 - fees = ~1300 XRP
        // Second: 1100 + 150 + 400 - 100 - fees = ~1550 XRP
        bal1 = scanBalance(firstFvk, 1299.0, 1301.0, "After Ledger 2, First account");
        bal2 = scanBalance(secondFvk, 1549.0, 1551.0, "After Ledger 2, Second account");

        // ========== LEDGER 3: 4 transactions (2 t→z, 1 z→z, 1 z→t) ==========
        log << "\n=== LEDGER 3: 4 transactions (2 t→z, 1 z→z, 1 z→t) ===" << std::endl;

        log << "Ledger 3, Tx 1: t→z 350 XRP to first account" << std::endl;
        bool tx9 = submitTx("t_to_z", "350000000", alice.human(), firstAddr, "", alice.name());
        BEAST_EXPECT(tx9);

        log << "Ledger 3, Tx 2: t→z 450 XRP to second account" << std::endl;
        bool tx10 = submitTx("t_to_z", "450000000", alice.human(), secondAddr, "", alice.name());
        BEAST_EXPECT(tx10);

        log << "Ledger 3, Tx 3: z→z 300 XRP from second to first" << std::endl;
        bool tx11 = submitTx("z_to_z", "300000000", "", firstAddr, secondSk);
        BEAST_EXPECT(tx11);

        log << "Ledger 3, Tx 4: z→t 200 XRP from first to alice" << std::endl;
        bool tx12 = submitTx("z_to_t", "200000000", "", alice.human(), firstSk);
        BEAST_EXPECT(tx12);

        env.close();
        log << "Ledger 3 closed" << std::endl;

        // Verify final balances after Ledger 3
        // First: ~1300 + 350 + 300 - 200 - fees = ~1750 XRP
        // Second: ~1550 + 450 - 300 - fees = ~1700 XRP
        bal1 = scanBalance(firstFvk, 1749.0, 1751.0, "After Ledger 3, First account (FINAL)");
        bal2 = scanBalance(secondFvk, 1699.0, 1701.0, "After Ledger 3, Second account (FINAL)");

        log << "\n=== Test Summary ===" << std::endl;
        log << "Successfully processed 12 transactions across 3 ledgers:" << std::endl;
        log << "  Ledger 1: 4 transactions (4 t→z)" << std::endl;
        log << "  Ledger 2: 4 transactions (2 t→z, 1 z→z, 1 z→t)" << std::endl;
        log << "  Ledger 3: 4 transactions (2 t→z, 1 z→z, 1 z→t)" << std::endl;
        log << "Total: 8 t→z, 2 z→z, 2 z→t = 12 transactions" << std::endl;
    }

    void
    run() override
    {
        testOrchardFullFlowWithStaticKeys();
        testOrchardDoubleTransactions();
    }
};

BEAST_DEFINE_TESTSUITE(OrchardFullFlow, rpc, ripple);

}  // namespace test
}  // namespace ripple
