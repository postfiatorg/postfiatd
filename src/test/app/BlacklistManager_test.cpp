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
#include <xrpld/app/misc/BlacklistManager.h>
#include <xrpl/basics/random.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <thread>
#include <vector>

namespace ripple {
namespace test {

struct BlacklistManager_test : public beast::unit_test::suite
{
    AccountID
    randomAccount()
    {
        auto const keypair = randomKeyPair(KeyType::secp256k1);
        return calcAccountID(keypair.first);
    }

    void
    testBasicOperations()
    {
        testcase("BlacklistManager basic operations");

        using namespace jtx;
        
        auto const compliance = Account("compliance");
        auto const notCompliance = Account("notcompliance");
        auto const target1 = randomAccount();
        auto const target2 = randomAccount();
        
        // Create config with compliance account
        auto cfg = envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->COMPLIANCE_ACCOUNT = compliance.human();
            return cfg;
        });

        Env env(*this, std::move(cfg));
        auto& blacklistManager = env.app().getBlacklistManager();
        
        // Test initial state
        BEAST_EXPECT(!blacklistManager.isBlacklisted(target1));
        BEAST_EXPECT(!blacklistManager.isBlacklisted(target2));
        BEAST_EXPECT(blacklistManager.getBlacklist().empty());
        
        // Test unauthorized add
        BEAST_EXPECT(!blacklistManager.addToBlacklist(target1, notCompliance.id()));
        BEAST_EXPECT(!blacklistManager.isBlacklisted(target1));
        
        // Test authorized add
        BEAST_EXPECT(blacklistManager.addToBlacklist(target1, compliance.id()));
        BEAST_EXPECT(blacklistManager.isBlacklisted(target1));
        BEAST_EXPECT(blacklistManager.getBlacklist().size() == 1);
        
        // Test duplicate add
        BEAST_EXPECT(!blacklistManager.addToBlacklist(target1, compliance.id()));
        BEAST_EXPECT(blacklistManager.getBlacklist().size() == 1);
        
        // Test add another
        BEAST_EXPECT(blacklistManager.addToBlacklist(target2, compliance.id()));
        BEAST_EXPECT(blacklistManager.isBlacklisted(target2));
        BEAST_EXPECT(blacklistManager.getBlacklist().size() == 2);
        
        // Test unauthorized remove
        BEAST_EXPECT(!blacklistManager.removeFromBlacklist(target1, notCompliance.id()));
        BEAST_EXPECT(blacklistManager.isBlacklisted(target1));
        
        // Test authorized remove
        BEAST_EXPECT(blacklistManager.removeFromBlacklist(target1, compliance.id()));
        BEAST_EXPECT(!blacklistManager.isBlacklisted(target1));
        BEAST_EXPECT(blacklistManager.getBlacklist().size() == 1);
        
        // Test remove non-existent
        BEAST_EXPECT(!blacklistManager.removeFromBlacklist(target1, compliance.id()));
        BEAST_EXPECT(blacklistManager.getBlacklist().size() == 1);
    }

    void
    testThreadSafety()
    {
        testcase("BlacklistManager thread safety");

        using namespace jtx;
        
        auto const compliance = Account("compliance");
        
        // Create config with compliance account
        auto cfg = envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->COMPLIANCE_ACCOUNT = compliance.human();
            return cfg;
        });

        Env env(*this, std::move(cfg));
        auto& blacklistManager = env.app().getBlacklistManager();
        
        constexpr int numThreads = 10;
        constexpr int numAccountsPerThread = 100;
        
        std::vector<std::thread> threads;
        std::atomic<int> successfulAdds{0};
        
        // Create threads that add accounts
        for (int i = 0; i < numThreads; ++i)
        {
            threads.emplace_back([&, i]() {
                for (int j = 0; j < numAccountsPerThread; ++j)
                {
                    auto account = randomAccount();
                    if (blacklistManager.addToBlacklist(account, compliance.id()))
                        successfulAdds++;
                }
            });
        }
        
        // Wait for all threads
        for (auto& t : threads)
            t.join();
        
        // Verify results
        BEAST_EXPECT(successfulAdds == numThreads * numAccountsPerThread);
        BEAST_EXPECT(blacklistManager.getBlacklist().size() == 
                     static_cast<size_t>(numThreads * numAccountsPerThread));
        
        // Test concurrent reads
        threads.clear();
        std::atomic<int> blacklistedFound{0};
        auto blacklist = blacklistManager.getBlacklist();
        std::vector<AccountID> accountList(blacklist.begin(), blacklist.end());
        
        for (int i = 0; i < numThreads; ++i)
        {
            threads.emplace_back([&]() {
                for (const auto& account : accountList)
                {
                    if (blacklistManager.isBlacklisted(account))
                        blacklistedFound++;
                }
            });
        }
        
        for (auto& t : threads)
            t.join();
        
        // Each thread should find all accounts
        BEAST_EXPECT(blacklistedFound == 
                     numThreads * static_cast<int>(accountList.size()));
    }

    void
    testPersistence()
    {
        testcase("BlacklistManager persistence");

        using namespace jtx;
        
        auto const compliance = Account("compliance");
        auto const target1 = randomAccount();
        auto const target2 = randomAccount();
        auto const target3 = randomAccount();
        
        // Create config with compliance account
        auto cfg = envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->COMPLIANCE_ACCOUNT = compliance.human();
            return cfg;
        });

        // First, add some accounts to blacklist
        {
            Env env(*this, cfg->clone());
            auto& blacklistManager = env.app().getBlacklistManager();
            
            BEAST_EXPECT(blacklistManager.addToBlacklist(target1, compliance.id()));
            BEAST_EXPECT(blacklistManager.addToBlacklist(target2, compliance.id()));
            BEAST_EXPECT(blacklistManager.addToBlacklist(target3, compliance.id()));
            
            // Remove one
            BEAST_EXPECT(blacklistManager.removeFromBlacklist(target2, compliance.id()));
        }
        
        // Create new environment and verify persistence
        {
            Env env(*this, std::move(cfg));
            auto& blacklistManager = env.app().getBlacklistManager();
            
            // Load from database
            blacklistManager.load();
            
            // Verify state
            BEAST_EXPECT(blacklistManager.isBlacklisted(target1));
            BEAST_EXPECT(!blacklistManager.isBlacklisted(target2));
            BEAST_EXPECT(blacklistManager.isBlacklisted(target3));
            BEAST_EXPECT(blacklistManager.getBlacklist().size() == 2);
        }
    }

    void
    testInvalidComplianceAccount()
    {
        testcase("BlacklistManager invalid compliance account");

        using namespace jtx;
        
        auto const target = randomAccount();
        
        // Test with invalid compliance account in config
        {
            auto cfg = envconfig([&](std::unique_ptr<Config> cfg) {
                cfg->COMPLIANCE_ACCOUNT = "invalid_account_format";
                return cfg;
            });

            Env env(*this, std::move(cfg));
            auto& blacklistManager = env.app().getBlacklistManager();
            
            // Should have no compliance account
            BEAST_EXPECT(!blacklistManager.getComplianceAccount());
            
            // Operations should fail
            BEAST_EXPECT(!blacklistManager.addToBlacklist(
                target, randomAccount()));
        }
        
        // Test with empty compliance account
        {
            auto cfg = envconfig([&](std::unique_ptr<Config> cfg) {
                cfg->COMPLIANCE_ACCOUNT = "";
                return cfg;
            });

            Env env(*this, std::move(cfg));
            auto& blacklistManager = env.app().getBlacklistManager();
            
            BEAST_EXPECT(!blacklistManager.getComplianceAccount());
        }
    }

    void
    testLargeBlacklist()
    {
        testcase("BlacklistManager large blacklist");

        using namespace jtx;
        
        auto const compliance = Account("compliance");
        
        // Create config with compliance account
        auto cfg = envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->COMPLIANCE_ACCOUNT = compliance.human();
            return cfg;
        });

        Env env(*this, std::move(cfg));
        auto& blacklistManager = env.app().getBlacklistManager();
        
        constexpr int numAccounts = 10000;
        std::vector<AccountID> accounts;
        accounts.reserve(numAccounts);
        
        // Add many accounts
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < numAccounts; ++i)
        {
            auto account = randomAccount();
            accounts.push_back(account);
            BEAST_EXPECT(blacklistManager.addToBlacklist(account, compliance.id()));
        }
        auto addDuration = std::chrono::steady_clock::now() - start;
        
        log << "Added " << numAccounts << " accounts in " 
            << std::chrono::duration_cast<std::chrono::milliseconds>(addDuration).count() 
            << "ms" << std::endl;
        
        BEAST_EXPECT(blacklistManager.getBlacklist().size() == numAccounts);
        
        // Test lookup performance
        start = std::chrono::steady_clock::now();
        for (const auto& account : accounts)
        {
            BEAST_EXPECT(blacklistManager.isBlacklisted(account));
        }
        auto lookupDuration = std::chrono::steady_clock::now() - start;
        
        log << "Looked up " << numAccounts << " accounts in " 
            << std::chrono::duration_cast<std::chrono::milliseconds>(lookupDuration).count() 
            << "ms" << std::endl;
        
        // Test non-existent lookup
        start = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i)
        {
            BEAST_EXPECT(!blacklistManager.isBlacklisted(randomAccount()));
        }
        auto nonExistentDuration = std::chrono::steady_clock::now() - start;
        
        log << "Looked up 1000 non-existent accounts in " 
            << std::chrono::duration_cast<std::chrono::milliseconds>(nonExistentDuration).count() 
            << "ms" << std::endl;
    }

    void
    run() override
    {
        testBasicOperations();
        testThreadSafety();
        testPersistence();
        testInvalidComplianceAccount();
        testLargeBlacklist();
    }
};

BEAST_DEFINE_TESTSUITE(BlacklistManager, test, ripple);

}  // namespace test
}  // namespace ripple