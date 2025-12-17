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
#include <xrpld/app/misc/OrchardWallet.h>
#include <xrpld/app/main/Application.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

#include <orchard-postfiat/src/ffi/bridge.rs.h>

namespace ripple {

// {
//   full_viewing_key: <96-byte-hex>  // Full viewing key to track
// }
Json::Value
doOrchardWalletAddKey(RPC::JsonContext& context)
{
    Json::Value result;

    // Require full_viewing_key parameter
    if (!context.params.isMember(jss::full_viewing_key))
        return RPC::missing_field_error(jss::full_viewing_key);

    if (!context.params[jss::full_viewing_key].isString())
        return RPC::expected_field_error(jss::full_viewing_key, "string");

    auto fvk_hex = context.params[jss::full_viewing_key].asString();

    // Parse FVK hex (should be 96 bytes = 192 hex chars)
    if (fvk_hex.length() != 192)
    {
        result[jss::error] = "invalidParams";
        result[jss::error_message] = "full_viewing_key must be 192 hex characters (96 bytes)";
        return result;
    }

    auto fvk_blob = strUnHex(fvk_hex);
    if (!fvk_blob || fvk_blob->size() != 96)
    {
        result[jss::error] = "invalidParams";
        result[jss::error_message] = "Invalid hex encoding for full_viewing_key";
        return result;
    }

    try
    {
        // Derive IVK from FVK
        rust::Slice<const uint8_t> fvk_slice{fvk_blob->data(), fvk_blob->size()};
        auto ivk_vec = ::orchard_derive_ivk_from_fvk(fvk_slice);

        if (ivk_vec.size() != 64)
        {
            result[jss::error] = "internal";
            result[jss::error_message] = "Invalid IVK length returned from derivation";
            return result;
        }

        // Add IVK to global wallet
        auto& wallet = context.app.getOrchardWallet();
        Blob ivk_blob(ivk_vec.begin(), ivk_vec.end());

        if (!wallet.addIncomingViewingKey(ivk_blob))
        {
            result[jss::error] = "internal";
            result[jss::error_message] = "Failed to add IVK to wallet";
            return result;
        }

        // Return success
        result[jss::status] = "success";
        result["ivk"] = strHex(ivk_blob);
        result["tracked_keys"] = static_cast<unsigned>(wallet.getIncomingViewingKeyCount());

        return result;
    }
    catch (std::exception const& e)
    {
        result[jss::error] = "internal";
        result[jss::error_message] = std::string("Failed to derive IVK: ") + e.what();
        return result;
    }
}

}  // namespace ripple
