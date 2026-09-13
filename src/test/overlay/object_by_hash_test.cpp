#include <xrpld/overlay/detail/ObjectByHashLimits.h>

#include <xrpl/beast/unit_test.h>

#include <array>
#include <limits>
#include <string>

namespace ripple {
namespace test {

class object_by_hash_test : public beast::unit_test::suite
{
    static protocol::TMGetObjectByHash
    reply()
    {
        protocol::TMGetObjectByHash result;
        result.set_type(protocol::TMGetObjectByHash::otSTATE_NODE);
        result.set_query(false);
        return result;
    }

    static protocol::TMIndexedObject
    requested()
    {
        protocol::TMIndexedObject object;
        object.set_hash(std::string(32, 'h'));
        return object;
    }

    void
    testRequestCount()
    {
        testcase("Reject count amplification before lookup or job scheduling");
        protocol::TMGetObjectByHash request;
        request.set_query(true);
        request.set_type(protocol::TMGetObjectByHash::otSTATE_NODE);
        BEAST_EXPECT(objectByHashRequestWithinLimits(request));
        for (int i = 0; i < maxObjectByHashRequestObjects; ++i)
            *request.add_objects() = requested();
        BEAST_EXPECT(objectByHashRequestWithinLimits(request));
        *request.add_objects() = requested();
        BEAST_EXPECT(!objectByHashRequestWithinLimits(request));

        // Fetch-pack queries remain bounded, while transaction retrieval
        // preserves the existing reduced-relay queue size for honest peers.
        request.set_type(protocol::TMGetObjectByHash::otFETCH_PACK);
        BEAST_EXPECT(!objectByHashRequestWithinLimits(request));
        request.set_type(protocol::TMGetObjectByHash::otTRANSACTIONS);
        BEAST_EXPECT(objectByHashRequestWithinLimits(request));
        while (request.objects_size() < reduce_relay::MAX_TX_QUEUE_SIZE)
            *request.add_objects() = requested();
        BEAST_EXPECT(objectByHashRequestWithinLimits(request));
        *request.add_objects() = requested();
        BEAST_EXPECT(!objectByHashRequestWithinLimits(request));
    }

    void
    testWireAccounting()
    {
        testcase("Exact wire accounting across blob and integer varints");
        for (std::size_t size : {0, 1, 127, 128, 16383, 16384})
        {
            auto response = reply();
            response.set_seq(std::numeric_limits<std::uint32_t>::max());
            response.set_ledgerhash(std::string(32, 'l'));
            ObjectByHashReplyBudget budget(response);
            auto request = requested();
            std::string data(size, 'd');
            request.set_nodeid(std::string(size, 'i'));
            request.set_ledgerseq(std::numeric_limits<std::uint32_t>::max());
            for (int n = 0; n < 3; ++n)
                BEAST_EXPECT(budget.append(request, makeSlice(data)));
            BEAST_EXPECT(response.objects_size() == 3);
            auto const& object = response.objects(0);
            BEAST_EXPECT(object.hash() == request.hash());
            BEAST_EXPECT(object.data() == data);
            BEAST_EXPECT(object.index() == request.nodeid());
            BEAST_EXPECT(object.ledgerseq() == request.ledgerseq());
            BEAST_EXPECT(
                Message::messageSize(response) <= maxObjectByHashReplyBytes);
            Message encoded(response, protocol::mtGET_OBJECTS);
            BEAST_EXPECT(!encoded.empty());
        }
    }

    void
    testExactBudget()
    {
        testcase(
            "Exact byte ceiling, metadata overhead and unchanged rejection");
        auto request = requested();
        std::string data(maxObjectByHashReplyBytes - 1024, 'd');
        auto reference = reply();
        auto& object = *reference.add_objects();
        object.set_hash(request.hash());
        object.set_data(data);
        auto const overhead = Message::messageSize(reference) - data.size();
        data.resize(maxObjectByHashReplyBytes - overhead);
        object.set_data(data);
        BEAST_EXPECT(
            Message::messageSize(reference) == maxObjectByHashReplyBytes);

        auto response = reply();
        ObjectByHashReplyBudget budget(response);
        BEAST_EXPECT(budget.append(request, makeSlice(data)));
        BEAST_EXPECT(
            Message::messageSize(response) == maxObjectByHashReplyBytes);
        auto const before = response.SerializeAsString();
        BEAST_EXPECT(!budget.append(request, Slice{}));
        BEAST_EXPECT(response.SerializeAsString() == before);

        response = reply();
        ObjectByHashReplyBudget tooLarge(response);
        data.push_back('x');
        BEAST_EXPECT(!tooLarge.append(request, makeSlice(data)));
        BEAST_EXPECT(response.objects_size() == 0);

        // Large attacker-controlled node IDs are not copied before the check.
        request.set_nodeid(std::string(maxObjectByHashReplyBytes, 'i'));
        BEAST_EXPECT(!tooLarge.append(request, Slice{}));
        BEAST_EXPECT(response.objects_size() == 0);
        request.clear_nodeid();
        request.set_hash("short");
        BEAST_EXPECT(!tooLarge.append(request, Slice{}));
    }

    void
    testRepeatedObjects()
    {
        testcase("Repeated hashes have bounded count and aggregate bytes");
        auto request = requested();
        auto response = reply();
        ObjectByHashReplyBudget budget(response);
        for (int i = 0; i < maxObjectByHashRequestObjects; ++i)
            BEAST_EXPECT(budget.append(request, Slice{}));
        BEAST_EXPECT(!budget.append(request, Slice{}));
        BEAST_EXPECT(response.objects_size() == maxObjectByHashRequestObjects);
        BEAST_EXPECT(
            Message::messageSize(response) <= maxObjectByHashReplyBytes);

        response = reply();
        ObjectByHashReplyBudget bytes(response);
        std::string data(1024 * 1024, 'd');
        int accepted = 0;
        while (bytes.append(request, makeSlice(data)))
            ++accepted;
        BEAST_EXPECT(accepted == 7);
        BEAST_EXPECT(response.objects_size() == accepted);
        BEAST_EXPECT(
            Message::messageSize(response) <= maxObjectByHashReplyBytes);
    }

public:
    void
    run() override
    {
        testRequestCount();
        testWireAccounting();
        testExactBudget();
        testRepeatedObjects();
    }
};

BEAST_DEFINE_TESTSUITE(object_by_hash, overlay, ripple);

}  // namespace test
}  // namespace ripple
