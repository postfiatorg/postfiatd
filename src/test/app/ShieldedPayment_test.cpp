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
    run() override
    {
        testFeatureDisabled();
        testMissingBundle();
        testEmptyBundle();
        testInvalidBundleData();
        testInsufficientBalance();
        testBundleParsingBasics();

        // TODO: Add tests with real bundles when we have bundle generation:
        // - testBasicTransparentToShielded()
        // - testNullifierDoubleSpend()
        // - testInvalidAnchor()
        // - testProofVerification()
        // - testValueBalanceValidation()
    }
};

BEAST_DEFINE_TESTSUITE(ShieldedPayment, app, ripple);

}  // namespace test
}  // namespace ripple
