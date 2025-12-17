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

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

#include <orchard-postfiat/src/ffi/bridge.rs.h>

namespace ripple {

// {
//   spending_key: <hex-string>          // 32 bytes (64 hex chars)
//   diversifier_index: <optional-uint>  // Default 0
// }
Json::Value
doOrchardDeriveAddress(RPC::JsonContext& context)
{
    Json::Value result;

    // Spending key is required
    if (!context.params.isMember(jss::spending_key))
        return RPC::missing_field_error(jss::spending_key);

    if (!context.params[jss::spending_key].isString())
        return RPC::expected_field_error(jss::spending_key, "string");

    // Parse diversifier_index (currently only 0 is supported)
    std::uint32_t diversifier_index = 0;
    if (context.params.isMember(jss::diversifier_index))
    {
        if (!context.params[jss::diversifier_index].isUInt())
            return RPC::expected_field_error(jss::diversifier_index, "unsigned integer");

        diversifier_index = context.params[jss::diversifier_index].asUInt();

        if (diversifier_index != 0)
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "Only diversifier_index 0 is currently supported";
            return result;
        }
    }

    // Parse spending key from hex
    auto sk_hex = context.params[jss::spending_key].asString();
    auto sk_blob = strUnHex(sk_hex);

    if (!sk_blob || sk_blob->size() != 32)
    {
        result[jss::error] = "invalidParams";
        result[jss::error_message] = "spending_key must be 32 bytes (64 hex characters)";
        return result;
    }

    try
    {
        // Derive address from spending key
        rust::Slice<const uint8_t> sk_slice{sk_blob->data(), sk_blob->size()};
        auto addr_result = ::orchard_test_get_address_from_sk(sk_slice);
        auto addr_vec = std::move(addr_result);

        if (addr_vec.size() != 43)
        {
            result[jss::error] = "internal";
            result[jss::error_message] = "Invalid address length";
            return result;
        }

        // Return address as hex
        result[jss::address] = strHex(std::string_view(
            reinterpret_cast<const char*>(addr_vec.data()), addr_vec.size()));
        result[jss::diversifier_index] = diversifier_index;

        return result;
    }
    catch (std::exception const& e)
    {
        result[jss::error] = "internal";
        result[jss::error_message] = std::string("Failed to derive address: ") + e.what();
        return result;
    }
}

}  // namespace ripple
