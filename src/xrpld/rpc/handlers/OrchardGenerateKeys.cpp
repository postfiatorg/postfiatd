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
//   seed: <optional-hex-or-passphrase>  // If omitted, use random
//   diversifier_index: <optional-uint>  // Default 0
// }
Json::Value
doOrchardGenerateKeys(RPC::JsonContext& context)
{
    Json::Value result;

    // Parse diversifier_index (currently only 0 is supported by the test functions)
    std::uint32_t diversifier_index = 0;
    if (context.params.isMember(jss::diversifier_index))
    {
        if (!context.params[jss::diversifier_index].isUInt())
            return RPC::expected_field_error(jss::diversifier_index, "unsigned integer");

        diversifier_index = context.params[jss::diversifier_index].asUInt();

        // For now, only diversifier 0 is supported
        if (diversifier_index != 0)
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "Only diversifier_index 0 is currently supported";
            return result;
        }
    }

    // Generate spending key
    try
    {
        rust::Vec<std::uint8_t> sk_vec;

        if (context.params.isMember(jss::seed))
        {
            // User provided seed - use it for deterministic TEST generation
            // WARNING: This is only for testing! Uses weak key derivation!
            if (!context.params[jss::seed].isString())
                return RPC::expected_field_error(jss::seed, "string");

            auto seed_str = context.params[jss::seed].asString();

            // For simplicity, hash the seed string to get a single byte
            // In production, you'd want more sophisticated key derivation
            std::hash<std::string> hasher;
            auto seed_hash = hasher(seed_str);
            std::uint8_t seed_byte = static_cast<std::uint8_t>(seed_hash & 0xFF);

            sk_vec = ::orchard_test_generate_spending_key(seed_byte);
        }
        else
        {
            // No seed provided - use cryptographically secure random generation
            sk_vec = ::orchard_generate_random_spending_key();
        }

        if (sk_vec.size() != 32)
        {
            result[jss::error] = "internal";
            result[jss::error_message] = "Invalid spending key length";
            return result;
        }

        // Derive full viewing key (96 bytes)
        rust::Slice<const uint8_t> sk_slice{sk_vec.data(), sk_vec.size()};
        auto fvk_result = ::orchard_test_get_full_viewing_key(sk_slice);
        auto fvk_vec = std::move(fvk_result);

        if (fvk_vec.size() != 96)
        {
            result[jss::error] = "internal";
            result[jss::error_message] = "Invalid full viewing key length";
            return result;
        }

        // Derive address (43 bytes)
        rust::Slice<const uint8_t> sk_slice2{sk_vec.data(), sk_vec.size()};
        auto addr_result = ::orchard_test_get_address_from_sk(sk_slice2);
        auto addr_vec = std::move(addr_result);

        if (addr_vec.size() != 43)
        {
            result[jss::error] = "internal";
            result[jss::error_message] = "Invalid address length";
            return result;
        }

        // Return keys as hex strings
        result[jss::spending_key] = strHex(std::string_view(
            reinterpret_cast<const char*>(sk_vec.data()), sk_vec.size()));
        result[jss::full_viewing_key] = strHex(std::string_view(
            reinterpret_cast<const char*>(fvk_vec.data()), fvk_vec.size()));
        result[jss::address] = strHex(std::string_view(
            reinterpret_cast<const char*>(addr_vec.data()), addr_vec.size()));
        result[jss::diversifier_index] = diversifier_index;

        // Important warning
        result[jss::warning] = "IMPORTANT: Store spending_key securely in your wallet. "
                               "The node does not retain any keys. "
                               "Loss of spending_key means permanent loss of funds.";

        return result;
    }
    catch (std::exception const& e)
    {
        result[jss::error] = "internal";
        result[jss::error_message] = std::string("Failed to generate keys: ") + e.what();
        return result;
    }
}

}  // namespace ripple
