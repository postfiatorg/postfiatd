#include <test/jtx.h>

#include <xrpld/app/misc/RemoteExclusionListFetcher.h>

namespace ripple {
namespace test {

class RemoteExclusionListFetcher_test : public beast::unit_test::suite
{
    void
    complete(RemoteExclusionListFetcher& fetcher, bool first, bool second)
    {
        fetcher.fetching_ = true;
        fetcher.fetchResults_.clear();
        for (bool successful : {first, second})
        {
            RemoteExclusionListFetcher::SourceResult result;
            result.success = successful;
            if (successful)
                result.list = RemoteExclusionListFetcher::ExclusionList{};
            else
                result.errorMessage = "synthetic timeout";
            fetcher.fetchResults_.push_back(std::move(result));
        }
        fetcher.onAllFetchesComplete();
        BEAST_EXPECT(!fetcher.fetching_);
        BEAST_EXPECT(fetcher.isInitialFetchComplete());
    }

public:
    void
    run() override
    {
        jtx::Env env(*this);
        auto config = jtx::envconfig();
        config->VALIDATOR_EXCLUSIONS_SOURCES = {
            {"http://synthetic-a.invalid", "synthetic-key-a"},
            {"http://synthetic-b.invalid", "synthetic-key-b"}};
        RemoteExclusionListFetcher fetcher(env.app(), *config, env.journal);
        // No start(), request, network work, or signature verification.
        testcase("initial outage stays unavailable");
        complete(fetcher, false, false);
        BEAST_EXPECT(!fetcher.areAllSourcesAccessible());
        BEAST_EXPECT(fetcher.cachedLists_.empty());

        testcase("success then complete outage");
        complete(fetcher, true, true);
        BEAST_EXPECT(fetcher.areAllSourcesAccessible());
        BEAST_EXPECT(fetcher.cachedLists_.size() == 2);
        auto const lastSuccess = fetcher.lastSuccessfulFetchTime_;
        complete(fetcher, false, false);
        BEAST_EXPECT(!fetcher.areAllSourcesAccessible());
        BEAST_EXPECT(fetcher.cachedLists_.size() == 2);
        BEAST_EXPECT(fetcher.lastSuccessfulFetchTime_ == lastSuccess);

        testcase("recovery then partial outage");
        complete(fetcher, true, true);
        BEAST_EXPECT(fetcher.areAllSourcesAccessible());
        complete(fetcher, true, false);
        BEAST_EXPECT(!fetcher.areAllSourcesAccessible());
        BEAST_EXPECT(fetcher.cachedLists_.size() == 2);

        testcase("final recovery");
        complete(fetcher, true, true);
        BEAST_EXPECT(fetcher.areAllSourcesAccessible());
    }
};

BEAST_DEFINE_TESTSUITE(RemoteExclusionListFetcher, app, ripple);

}  // namespace test
}  // namespace ripple
