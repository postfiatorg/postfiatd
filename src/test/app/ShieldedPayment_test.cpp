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

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/OrchardBundle.h>
#include <xrpl/protocol/jss.h>

// Include Orchard FFI for test bundle generation
#include "orchard-postfiat/src/ffi/bridge.rs.h"

namespace ripple {
namespace test {

class ShieldedPayment_test : public beast::unit_test::suite
{
public:
    void
    testFeatureDisabled()
    {
        testcase("Feature disabled");

        using namespace jtx;

        // Create env WITHOUT OrchardPrivacy amendment
        Env env{*this, supported_amendments() - featureOrchardPrivacy};
        Account const alice("alice");

        std::cout << "TEST: Created alice account object" << std::endl;
        std::cout << "TEST: Alice address: " << alice.human() << std::endl;
        std::cout.flush();

        std::cout << "TEST: About to fund alice..." << std::endl;
        std::cout.flush();
        env.fund(XRP(10000), alice);
        std::cout << "TEST: Funded alice with XRP(10000)" << std::endl;
        std::cout.flush();

        env.close();
        std::cout << "TEST: Closed ledger" << std::endl;
        std::cout << "TEST: Alice balance: " << env.balance(alice) << std::endl;
        std::cout << "TEST: Alice sequence: " << env.seq(alice) << std::endl;

        // Create an empty bundle (minimal valid structure)
        Blob emptyBundle;
        emptyBundle.push_back(0);  // nActionsOrchard = 0
        std::cout << "TEST: Created empty bundle" << std::endl;

        // Create transaction JSON with explicit Sequence and Fee
        // to avoid account lookup before preflight
        Json::Value jv;
        jv[jss::TransactionType] = jss::ShieldedPayment;
        jv[jss::Account] = alice.human();
        jv[jss::Amount] = XRP(1000).value().getJson(JsonOptions::none);
        jv[jss::Sequence] = env.seq(alice);
        jv[jss::Fee] = to_string(env.current()->fees().base);
        jv[sfOrchardBundle.jsonName] = strHex(emptyBundle);

        std::cout << "TEST: Created transaction JSON:" << std::endl;
        std::cout << jv.toStyledString() << std::endl;
        std::cout << "TEST: jv[Account] = " << jv[jss::Account].asString() << std::endl;
        std::cout << "TEST: jv[Sequence] = " << jv[jss::Sequence].asUInt() << std::endl;
        std::cout << "TEST: jv has Sequence? " << jv.isMember(jss::Sequence) << std::endl;

        std::cout << "TEST: About to submit transaction..." << std::endl;

        // Should fail because OrchardPrivacy amendment is not enabled
        // Use sig(alice) to pass Account object for signing instead of string lookup
        env(jv, sig(alice), ter(temDISABLED));

        std::cout << "TEST: Transaction submitted successfully" << std::endl;
    }

    void
    testMissingBundle()
    {
        testcase("Missing OrchardBundle field");

        using namespace jtx;

        // Create env WITH OrchardPrivacy amendment
        Env env{*this, supported_amendments() | featureOrchardPrivacy};
        Account const alice("alice");

        env.fund(XRP(10000), alice);
        env.close();

        // Create transaction without OrchardBundle field
        Json::Value jv;
        jv[jss::TransactionType] = jss::ShieldedPayment;
        jv[jss::Account] = alice.human();
        jv[jss::Amount] = XRP(1000).value().getJson(JsonOptions::none);
        jv[jss::Sequence] = env.seq(alice);
        jv[jss::Fee] = to_string(env.current()->fees().base);
        // No sfOrchardBundle field!

        env(jv, sig(alice), ter(temMALFORMED));
    }

    void
    testEmptyBundle()
    {
        testcase("Empty bundle with Amount field");

        using namespace jtx;

        Env env{*this, supported_amendments() | featureOrchardPrivacy};
        Account const alice("alice");

        env.fund(XRP(10000), alice);
        env.close();

        // Create empty bundle (0 actions)
        Blob emptyBundle;
        emptyBundle.push_back(0);  // nActionsOrchard = 0

        // Empty bundle with Amount field should fail
        // (empty bundle means no shielded operations, but we specified an amount)
        Json::Value jv;
        jv[jss::TransactionType] = jss::ShieldedPayment;
        jv[jss::Account] = alice.human();
        jv[jss::Amount] = XRP(1000).value().getJson(JsonOptions::none);
        jv[jss::Sequence] = env.seq(alice);
        jv[jss::Fee] = to_string(env.current()->fees().base);
        jv[sfOrchardBundle.jsonName] = strHex(emptyBundle);

        // This should fail in preflight because empty bundle has no actions
        env(jv, sig(alice), ter(temMALFORMED));
    }

    void
    testInvalidBundleData()
    {
        testcase("Invalid bundle data");

        using namespace jtx;

        Env env{*this, supported_amendments() | featureOrchardPrivacy};
        Account const alice("alice");

        env.fund(XRP(10000), alice);
        env.close();

        // Create invalid bundle data (random bytes)
        Blob invalidBundle{0xFF, 0xFF, 0xFF, 0xFF};

        Json::Value jv;
        jv[jss::TransactionType] = jss::ShieldedPayment;
        jv[jss::Account] = alice.human();
        jv[jss::Amount] = XRP(1000).value().getJson(JsonOptions::none);
        jv[jss::Sequence] = env.seq(alice);
        jv[jss::Fee] = to_string(env.current()->fees().base);
        jv[sfOrchardBundle.jsonName] = strHex(invalidBundle);

        // Should fail to parse
        env(jv, sig(alice), ter(temMALFORMED));
    }

    void
    testInsufficientBalance()
    {
        testcase("Insufficient balance for t→z");

        using namespace jtx;

        Env env{*this, supported_amendments() | featureOrchardPrivacy};
        Account const alice("alice");

        // Fund alice with 1000 XRP (enough to create account + have balance)
        env.fund(XRP(1000), alice);
        env.close();

        // TODO: When we have bundle generation, test with a real bundle
        // For now, this test documents the expected behavior
        BEAST_EXPECT(env.balance(alice) == XRP(1000));

        // When a real bundle for 10000 XRP is submitted, it should fail
        // with tecUNFUNDED_PAYMENT because alice only has 1000 XRP
    }

    void
    testBundleParsingBasics()
    {
        testcase("Bundle parsing basics");

        // Test that our OrchardBundle wrapper can parse empty bundles
        Blob emptyBundle;
        emptyBundle.push_back(0);  // nActionsOrchard = 0

        auto bundle = OrchardBundleWrapper::parse(makeSlice(emptyBundle));
        BEAST_EXPECT(bundle.has_value());
        BEAST_EXPECT(bundle->numActions() == 0);
        BEAST_EXPECT(bundle->getValueBalance() == 0);
        BEAST_EXPECT(!bundle->isPresent());  // Empty bundle is not "present"
    }

    void
    testRealBundleGeneration()
    {
        testcase("Real bundle generation with FFI");

        using namespace jtx;

        // NOTE: This test generates REAL Orchard bundles with Halo2 proofs
        // It takes ~5-10 seconds per bundle due to proof generation
        // Consider marking as slow/integration test if needed

        Env env{*this, supported_amendments() | featureOrchardPrivacy};
        Account const alice("alice");

        env.fund(XRP(10000), alice);
        env.close();

        std::cout << "TEST: Generating real Orchard bundle (this takes ~5-10 seconds)..." << std::endl;

        try {
            // Generate a deterministic spending key for testing
            auto sk_bytes = orchard_test_generate_spending_key(42);
            BEAST_EXPECT(sk_bytes.size() == 32);

            // Get Orchard address from the spending key
            rust::Slice<const uint8_t> sk_slice{sk_bytes.data(), sk_bytes.size()};
            auto addr_result = orchard_test_get_address_from_sk(sk_slice);
            auto addr_bytes = std::move(addr_result);
            BEAST_EXPECT(addr_bytes.size() == 43);

            // Get empty anchor (for first transaction in tree)
            auto anchor = orchard_test_get_empty_anchor();
            BEAST_EXPECT(anchor.size() == 32);

            // Build a t→z bundle for 1000 XRP (1 billion drops)
            uint64_t amount_drops = 1000000000;  // 1000 XRP
            rust::Slice<const uint8_t> addr_slice{addr_bytes.data(), addr_bytes.size()};

            auto bundle_result = orchard_test_build_transparent_to_shielded(
                amount_drops,
                addr_slice,
                anchor
            );
            auto bundle_bytes = std::move(bundle_result);

            std::cout << "TEST: Bundle generated! Size: " << bundle_bytes.size() << " bytes" << std::endl;

            // Convert rust::Vec to Blob for parsing
            Blob bundle_blob(bundle_bytes.begin(), bundle_bytes.end());

            // Parse and validate the generated bundle
            auto bundle = OrchardBundleWrapper::parse(makeSlice(bundle_blob));
            BEAST_EXPECT(bundle.has_value());

            if (bundle) {
                BEAST_EXPECT(bundle->isPresent());
                BEAST_EXPECT(bundle->numActions() == 1);
                BEAST_EXPECT(bundle->getValueBalance() == -1000000000);  // Negative = t→z

                // Verify it has nullifiers
                auto nullifiers = bundle->getNullifiers();
                BEAST_EXPECT(nullifiers.size() == 1);

                std::cout << "TEST: Bundle validation passed!" << std::endl;
                std::cout << "TEST:   - Actions: " << bundle->numActions() << std::endl;
                std::cout << "TEST:   - Value balance: " << bundle->getValueBalance() << " drops" << std::endl;
                std::cout << "TEST:   - Nullifiers: " << nullifiers.size() << std::endl;

                // Now submit as a transaction!
                // The empty anchor should exist after OrchardPrivacy amendment activation
                std::cout << "TEST: Submitting transaction..." << std::endl;

                Json::Value jv;
                jv[jss::TransactionType] = jss::ShieldedPayment;
                jv[jss::Account] = alice.human();
                jv[jss::Amount] = std::to_string(amount_drops);  // 1000 XRP in drops
                jv[sfOrchardBundle.jsonName] = strHex(bundle_blob);
                jv[jss::Fee] = std::to_string(env.current()->fees().base.drops());

                // Submit and expect success now that anchor tracking is implemented!
                env(jv, sig(alice), ter(tesSUCCESS));

                std::cout << "TEST: Transaction succeeded!" << std::endl;

                env.close();

                // Verify alice's balance was debited
                auto const balance = env.balance(alice);
                std::cout << "TEST: Alice's balance after: " << balance << std::endl;
                BEAST_EXPECT(balance < XRP(10000) - XRP(1000));

                // Now verify the shielded balance using the viewing key!
                std::cout << "TEST: Verifying shielded balance with viewing key..." << std::endl;

                // Derive full viewing key from spending key
                rust::Slice<const uint8_t> sk_slice_for_fvk{sk_bytes.data(), sk_bytes.size()};
                auto fvk_result = orchard_test_get_full_viewing_key(sk_slice_for_fvk);
                auto fvk_bytes = std::move(fvk_result);
                BEAST_EXPECT(fvk_bytes.size() == 96);
                std::cout << "TEST: Derived full viewing key (" << fvk_bytes.size() << " bytes)" << std::endl;

                // FIRST: Try to decrypt the note from the bundle (in-memory verification)
                std::cout << "TEST: Verifying in-memory bundle decryption..." << std::endl;
                rust::Slice<const uint8_t> fvk_slice{fvk_bytes.data(), fvk_bytes.size()};
                auto decrypt_result = orchard_test_try_decrypt_note(*bundle->getRustBundle(), 0, fvk_slice);
                auto decrypted_value = std::move(decrypt_result);

                std::cout << "TEST: Decrypted note value from bundle: " << decrypted_value << " drops" << std::endl;
                BEAST_EXPECT(decrypted_value == amount_drops);  // Should match 1000 XRP

                // SECOND: Scan the ledger and calculate shielded balance from ledger state
                std::cout << "TEST: Scanning ledger state for note commitments..." << std::endl;

                // Get all note commitment objects from the ledger
                auto const& view = env.current();
                uint64_t totalShieldedBalance = 0;
                size_t notesFound = 0;

                // Get the note commitments we stored
                auto commitments = bundle->getNoteCommitments();

                // Try to retrieve each note commitment from the ledger
                for (auto const& cmx : commitments) {
                    auto sle = view->read(keylet::orchardNoteCommitment(cmx));
                    if (!sle) {
                        std::cout << "TEST: Note commitment not found in ledger" << std::endl;
                        continue;
                    }

                    notesFound++;

                    // Extract the full bundle from ledger
                    if (!sle->isFieldPresent(sfOrchardBundle))
                    {
                        std::cout << "TEST: Note commitment missing bundle data" << std::endl;
                        continue;
                    }

                    auto ledgerBundleData = sle->getFieldVL(sfOrchardBundle);

                    std::cout << "TEST: Found note commitment in ledger with bundle ("
                              << ledgerBundleData.size() << " bytes), attempting decryption..." << std::endl;

                    // Parse the bundle from ledger
                    try {
                        Blob bundleBlob(ledgerBundleData.begin(), ledgerBundleData.end());
                        auto ledgerBundle = OrchardBundleWrapper::parse(makeSlice(bundleBlob));

                        if (!ledgerBundle) {
                            std::cout << "TEST: Failed to parse bundle from ledger" << std::endl;
                            continue;
                        }

                        // Try to decrypt notes from this bundle
                        size_t numActions = ledgerBundle->numActions();
                        for (size_t i = 0; i < numActions; ++i) {
                            try {
                                rust::Slice<const uint8_t> fvk_decrypt_slice{fvk_bytes.data(), fvk_bytes.size()};
                                auto ledger_decrypt_result = orchard_test_try_decrypt_note(
                                    *ledgerBundle->getRustBundle(),
                                    i,
                                    fvk_decrypt_slice
                                );

                                auto note_value = std::move(ledger_decrypt_result);
                                totalShieldedBalance += note_value;

                                std::cout << "TEST: Successfully decrypted note from ledger: "
                                          << note_value << " drops" << std::endl;
                            } catch (const std::exception& e) {
                                // Note not for this viewing key, continue
                            }
                        }
                    } catch (const std::exception& e) {
                        std::cout << "TEST: Error processing bundle from ledger: " << e.what() << std::endl;
                    }
                }

                std::cout << "TEST: Ledger scan complete. Found " << notesFound << " note commitments" << std::endl;
                std::cout << "TEST: Total shielded balance from ledger: " << totalShieldedBalance << " drops"
                          << " (" << (totalShieldedBalance / 1000000) << " XRP)" << std::endl;

                BEAST_EXPECT(notesFound == 1);  // Should find exactly 1 note
                BEAST_EXPECT(totalShieldedBalance == amount_drops);  // Should match 1000 XRP

                std::cout << "TEST: Shielded balance verified from ledger state! Successfully scanned and decrypted "
                          << notesFound << " note(s) showing " << (totalShieldedBalance / 1000000) << " XRP" << std::endl;
                std::cout << "TEST: Shielded payment transaction with real bundle successful!" << std::endl;
            }

        } catch (const std::exception& e) {
            std::cout << "TEST: Exception during bundle generation: " << e.what() << std::endl;
            BEAST_EXPECT(false);  // Test failed
        }
    }

    void
    run() override
    {
        testFeatureDisabled();
        testMissingBundle();
        testEmptyBundle();
        testInvalidBundleData();
        testInsufficientBalance();
        testBundleParsingBasics();

        // Real bundle generation test (slow - ~5-10 seconds)
        testRealBundleGeneration();

        // TODO: Add more tests with real bundles:
        // - testNullifierDoubleSpend()
        // - testInvalidAnchor()
        // - testProofVerification()
        // - testValueBalanceValidation()
    }
};

BEAST_DEFINE_TESTSUITE(ShieldedPayment, app, ripple);

}  // namespace test
}  // namespace ripple
