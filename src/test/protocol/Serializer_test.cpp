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

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Serializer.h>

#include <limits>
#include <stdexcept>
#include <vector>

namespace ripple {

struct Serializer_test : public beast::unit_test::suite
{
    void
    testVLLengthBound()
    {
        testcase("Variable-length header bound");
        // A three-byte header can state up to 929,984 bytes, but the encoder
        // writes at most maxVLLength. The decoder must refuse the gap:
        // an object that decodes but cannot be re-serialized throws from
        // every signing-hash computation, and STValidation::isValid is
        // noexcept.
        auto const decode3 = [](int length) {
            int const offset = length - 12481;
            return Serializer::decodeVLLength(
                241 + (offset >> 16), (offset >> 8) & 0xff, offset & 0xff);
        };
        BEAST_EXPECT(
            decode3(Serializer::maxVLLength) == Serializer::maxVLLength);
        try
        {
            decode3(Serializer::maxVLLength + 1);
            fail("Length above the encoder maximum was decoded");
        }
        catch (std::overflow_error const&)
        {
            pass();
        }

        // Round trip at the bound, refusal just above it and for a negative
        // length that would otherwise become a one-byte header.
        std::vector<std::uint8_t> const blob(Serializer::maxVLLength, 0);
        {
            Serializer s;
            s.addVL(blob.data(), static_cast<int>(blob.size()));
            SerialIter sit(s.slice());
            BEAST_EXPECT(sit.getVLDataLength() == Serializer::maxVLLength);
        }
        try
        {
            Serializer s;
            s.addVL(blob.data(), Serializer::maxVLLength + 1);
            fail("Length above the encoder maximum was encoded");
        }
        catch (std::overflow_error const&)
        {
            pass();
        }
        try
        {
            Serializer s;
            s.addVL(blob.data(), -1);
            fail("Negative length was encoded");
        }
        catch (std::overflow_error const&)
        {
            pass();
        }
    }

    void
    run() override
    {
        testVLLengthBound();
        testcase("Integer round trips");
        {
            std::initializer_list<std::int32_t> const values = {
                std::numeric_limits<std::int32_t>::min(),
                -1,
                0,
                1,
                std::numeric_limits<std::int32_t>::max()};
            for (std::int32_t value : values)
            {
                Serializer s;
                s.add32(value);
                BEAST_EXPECT(s.size() == 4);
                SerialIter sit(s.slice());
                BEAST_EXPECT(sit.geti32() == value);
            }
        }
        {
            std::initializer_list<std::int64_t> const values = {
                std::numeric_limits<std::int64_t>::min(),
                -1,
                0,
                1,
                std::numeric_limits<std::int64_t>::max()};
            for (std::int64_t value : values)
            {
                Serializer s;
                s.add64(value);
                BEAST_EXPECT(s.size() == 8);
                SerialIter sit(s.slice());
                BEAST_EXPECT(sit.geti64() == value);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(Serializer, protocol, ripple);

}  // namespace ripple
