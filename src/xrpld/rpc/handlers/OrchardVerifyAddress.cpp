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

namespace ripple {

// Verify an Orchard address format
// {
//   address: <hex-string>                  // Orchard address to verify (43 bytes = 86 hex chars)
// }
Json::Value
doOrchardVerifyAddress(RPC::JsonContext& context)
{
    Json::Value result;

    // Address is required
    if (!context.params.isMember(jss::address))
        return RPC::missing_field_error(jss::address);

    if (!context.params[jss::address].isString())
        return RPC::expected_field_error(jss::address, "string");

    auto address_hex = context.params[jss::address].asString();

    // Try to parse as hex
    auto address_blob = strUnHex(address_hex);

    // Basic validation
    bool valid = false;
    std::string error_reason;

    if (!address_blob)
    {
        error_reason = "Invalid hex encoding";
    }
    else if (address_blob->size() != 43)
    {
        error_reason = "Invalid length (expected 43 bytes, got " +
                      std::to_string(address_blob->size()) + " bytes)";
    }
    else
    {
        // Additional validation could be done here:
        // - Check diversifier (first 11 bytes)
        // - Validate the public key component
        // - Verify checksum if applicable

        // For now, if it's 43 bytes of valid hex, we consider it potentially valid
        valid = true;
    }

    result[jss::valid] = valid;
    result[jss::address] = address_hex;

    if (valid)
    {
        result[jss::address_type] = "orchard";
        result[jss::length_bytes] = 43;
        result[jss::length_hex] = 86;

        // Extract diversifier (first 11 bytes)
        if (address_blob && address_blob->size() >= 11)
        {
            std::string diversifier_hex = strHex(std::string_view(
                reinterpret_cast<const char*>(address_blob->data()), 11));
            result[jss::diversifier] = diversifier_hex;
        }
    }
    else
    {
        result[jss::error_reason] = error_reason;
    }

    return result;
}

}  // namespace ripple
