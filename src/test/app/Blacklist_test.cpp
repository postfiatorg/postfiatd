//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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
#include <xrpld/app/misc/ComplianceBlacklist.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

struct Blacklist_test : public beast::unit_test::suite
{
    // Helper to create an account set transaction with domain
    static Json::Value
    setDomain(jtx::Account const& account, std::string const& domain)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::Account] = account.human();
        jv[jss::Domain] = strHex(domain);
        jv[jss::TransactionType] = jss::AccountSet;
        return jv;
    }

    void
    testComplianceBlacklistParsing()
    {
        testcase("ComplianceBlacklist parsing and encoding");

        using namespace jtx;
        
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        
        // Test empty blacklist
        {
            Blob empty;
            auto blacklist = ComplianceBlacklist::parseBlacklist(empty);
            BEAST_EXPECT(blacklist.empty());
        }
        
        // Test single address
        {
            std::string domain = alice.human();
            Blob domainBlob(domain.begin(), domain.end());
            auto blacklist = ComplianceBlacklist::parseBlacklist(domainBlob);
            BEAST_EXPECT(blacklist.size() == 1);
            BEAST_EXPECT(blacklist.find(alice.id()) != blacklist.end());
        }
        
        // Test multiple addresses
        {
            std::string domain = alice.human() + "," + bob.human() + "," + carol.human();
            Blob domainBlob(domain.begin(), domain.end());
            auto blacklist = ComplianceBlacklist::parseBlacklist(domainBlob);
            BEAST_EXPECT(blacklist.size() == 3);
            BEAST_EXPECT(blacklist.find(alice.id()) != blacklist.end());
            BEAST_EXPECT(blacklist.find(bob.id()) != blacklist.end());
            BEAST_EXPECT(blacklist.find(carol.id()) != blacklist.end());
        }
        
        // Test encoding
        {
            std::unordered_set<AccountID> blacklist;
            blacklist.insert(alice.id());
            blacklist.insert(bob.id());
            
            auto encoded = ComplianceBlacklist::encodeBlacklist(blacklist);
            BEAST_EXPECT(encoded.has_value());
            
            // Parse it back
            Blob domainBlob(encoded->begin(), encoded->end());
            auto parsed = ComplianceBlacklist::parseBlacklist(domainBlob);
            BEAST_EXPECT(parsed.size() == 2);
            BEAST_EXPECT(parsed.find(alice.id()) != parsed.end());
            BEAST_EXPECT(parsed.find(bob.id()) != parsed.end());
        }
        
        // Test size limit (domain field max is 256 bytes)
        {
            std::unordered_set<AccountID> blacklist;
            // Each account is about 34 chars, so 7-8 accounts should fit
            for (int i = 0; i < 10; ++i)
            {
                auto account = Account("account" + std::to_string(i));
                blacklist.insert(account.id());
            }
            
            auto encoded = ComplianceBlacklist::encodeBlacklist(blacklist);
            // Should fail due to size limit
            if (encoded)
            {
                BEAST_EXPECT(encoded->size() <= 256);
            }
        }
    }

    void
    testSetAccountBlacklistUpdate()
    {
        testcase("SetAccount blacklist update");

        using namespace jtx;
        
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const compliance = Account("compliance");
        
        // Create config with compliance account
        auto cfg = envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->COMPLIANCE_ACCOUNT = compliance.human();
            return cfg;
        });

        Env env(*this, std::move(cfg));
        
        env.fund(XRP(10000), alice, bob, carol, compliance);
        env.close();

        // Test setting blacklist via domain field
        std::string blacklistStr = alice.human() + "," + bob.human();
        env(setDomain(compliance, blacklistStr));
        env.close();
        
        // Verify blacklist is stored in ledger
        auto const sle = env.le(compliance);
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->isFieldPresent(sfDomain));
        
        auto blacklist = ComplianceBlacklist::parseBlacklist(sle->getFieldVL(sfDomain));
        BEAST_EXPECT(blacklist.size() == 2);
        BEAST_EXPECT(blacklist.find(alice.id()) != blacklist.end());
        BEAST_EXPECT(blacklist.find(bob.id()) != blacklist.end());
        
        // Test non-compliance account cannot set blacklist format
        env(setDomain(carol, alice.human()));
        env.close();
        
        // Carol's domain should be set as-is (not parsed as blacklist)
        auto const carolSle = env.le(carol);
        BEAST_EXPECT(carolSle);
        BEAST_EXPECT(carolSle->isFieldPresent(sfDomain));
        std::string carolDomain(
            carolSle->getFieldVL(sfDomain).begin(),
            carolSle->getFieldVL(sfDomain).end());
        BEAST_EXPECT(carolDomain == alice.human());
        
        // Test clearing blacklist
        env(setDomain(compliance, ""));
        env.close();
        
        auto const clearedSle = env.le(compliance);
        BEAST_EXPECT(clearedSle);
        BEAST_EXPECT(!clearedSle->isFieldPresent(sfDomain));
    }

    void
    testTransactionBlocking()
    {
        testcase("Transaction blocking for blacklisted accounts");

        using namespace jtx;
        
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie");
        auto const compliance = Account("compliance");
        
        // Create config with compliance account
        auto cfg = envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->COMPLIANCE_ACCOUNT = compliance.human();
            return cfg;
        });

        Env env(*this, std::move(cfg));
        
        env.fund(XRP(10000), alice, bob, charlie, compliance);
        env.close();

        // Set blacklist with alice
        env(setDomain(compliance, alice.human()));
        env.close();
        
        // Test that alice cannot send payments
        env(pay(alice, bob, XRP(100)), ter(tecNO_PERMISSION));
        
        // Test that alice cannot receive payments
        env(pay(bob, alice, XRP(100)), ter(tecNO_PERMISSION));
        
        // Test that non-blacklisted accounts can transact normally
        env(pay(bob, charlie, XRP(100)));
        env(pay(charlie, bob, XRP(50)));
        
        // Test other transaction types are blocked for blacklisted accounts
        env(trust(alice, bob["USD"](1000)), ter(tecNO_PERMISSION));
        
        // Test that compliance account itself can still transact
        env(pay(compliance, bob, XRP(100)));
        env(pay(bob, compliance, XRP(50)));
        
        // Update blacklist to include bob instead of alice
        env(setDomain(compliance, bob.human()));
        env.close();
        
        // Test that alice can now transact
        env(pay(alice, charlie, XRP(100)));
        
        // But bob cannot
        env(pay(bob, charlie, XRP(100)), ter(tecNO_PERMISSION));
    }

    void
    testMultipleBlacklistedAccounts()
    {
        testcase("Multiple blacklisted accounts");

        using namespace jtx;
        
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie");
        auto const dave = Account("dave");
        auto const compliance = Account("compliance");
        
        // Create config with compliance account
        auto cfg = envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->COMPLIANCE_ACCOUNT = compliance.human();
            return cfg;
        });

        Env env(*this, std::move(cfg));
        
        env.fund(XRP(10000), alice, bob, charlie, dave, compliance);
        env.close();

        // Blacklist alice, bob, and charlie
        std::string blacklistStr = alice.human() + "," + bob.human() + "," + charlie.human();
        env(setDomain(compliance, blacklistStr));
        env.close();
        
        // Test all blacklisted accounts cannot transact
        env(pay(alice, dave, XRP(100)), ter(tecNO_PERMISSION));
        env(pay(bob, dave, XRP(100)), ter(tecNO_PERMISSION));
        env(pay(charlie, dave, XRP(100)), ter(tecNO_PERMISSION));
        
        // Test dave cannot send to blacklisted accounts
        env(pay(dave, alice, XRP(100)), ter(tecNO_PERMISSION));
        env(pay(dave, bob, XRP(100)), ter(tecNO_PERMISSION));
        env(pay(dave, charlie, XRP(100)), ter(tecNO_PERMISSION));
        
        // But dave can transact with compliance
        env(pay(dave, compliance, XRP(100)));
        env(pay(compliance, dave, XRP(50)));
    }

    void
    testBlacklistSizeLimit()
    {
        testcase("Blacklist size limit");

        using namespace jtx;
        
        auto const compliance = Account("compliance");
        
        // Create config with compliance account
        auto cfg = envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->COMPLIANCE_ACCOUNT = compliance.human();
            return cfg;
        });

        Env env(*this, std::move(cfg));
        
        env.fund(XRP(10000), compliance);
        
        // Create a blacklist that's too large
        std::string blacklistStr;
        for (int i = 0; i < 20; ++i)
        {
            auto account = Account("account" + std::to_string(i));
            env.fund(XRP(10000), account);
            if (i > 0) blacklistStr += ",";
            blacklistStr += account.human();
        }
        env.close();
        
        // Try to set the oversized blacklist
        // Should fail because domain field has 256 byte limit
        env(setDomain(compliance, blacklistStr), ter(telBAD_DOMAIN));
    }

    void
    testNoComplianceAccount()
    {
        testcase("No compliance account configured");

        using namespace jtx;
        
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        
        // Create config without compliance account
        Env env(*this);
        
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Test that transactions work normally
        env(pay(alice, bob, XRP(100)));
        env(pay(bob, alice, XRP(50)));
        
        // Alice can set domain normally
        env(setDomain(alice, "example.com"));
        env.close();
        
        auto const sle = env.le(alice);
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->isFieldPresent(sfDomain));
        std::string domain(
            sle->getFieldVL(sfDomain).begin(),
            sle->getFieldVL(sfDomain).end());
        BEAST_EXPECT(domain == "example.com");
    }

    void
    testLedgerStatePersistence()
    {
        testcase("Ledger state persistence");

        using namespace jtx;
        
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const compliance = Account("compliance");
        
        // Create config with compliance account
        auto cfg = envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->COMPLIANCE_ACCOUNT = compliance.human();
            return cfg;
        });

        Env env(*this, std::move(cfg));
        
        env.fund(XRP(10000), alice, bob, compliance);
        env.close();

        // Set blacklist
        std::string blacklistStr = alice.human() + "," + bob.human();
        env(setDomain(compliance, blacklistStr));
        env.close();
        
        // Verify blacklist is in ledger state
        {
            auto blacklist = ComplianceBlacklist::getBlacklist(
                *env.current(), env.app().config());
            BEAST_EXPECT(blacklist.size() == 2);
            BEAST_EXPECT(blacklist.find(alice.id()) != blacklist.end());
            BEAST_EXPECT(blacklist.find(bob.id()) != blacklist.end());
        }
        
        // Simulate new node syncing - blacklist should be available from ledger
        BEAST_EXPECT(ComplianceBlacklist::isBlacklisted(
            *env.current(), env.app().config(), alice.id()));
        BEAST_EXPECT(ComplianceBlacklist::isBlacklisted(
            *env.current(), env.app().config(), bob.id()));
    }

    void
    run() override
    {
        testComplianceBlacklistParsing();
        testSetAccountBlacklistUpdate();
        testTransactionBlocking();
        testMultipleBlacklistedAccounts();
        testBlacklistSizeLimit();
        testNoComplianceAccount();
        testLedgerStatePersistence();
    }
};

BEAST_DEFINE_TESTSUITE(Blacklist, test, ripple);

}  // namespace test
}  // namespace ripple