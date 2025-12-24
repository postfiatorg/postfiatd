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
#include <xrpl/protocol/TxFlags.h>

#include <orchard-postfiat/src/ffi/bridge.rs.h>

namespace ripple {

// {
//   payment_type: "t_to_z" | "z_to_z" | "z_to_t"
//   amount: <drops-string>               // Amount to send
//   recipient: <hex-string>              // Orchard address (43 bytes = 86 hex chars)
//
//   // For t→z: (transparent to shielded)
//   source_account: <address>            // XRP account paying
//
//   // For z→z: (shielded to shielded)
//   spending_key: <hex-string>           // 32 bytes (64 hex chars) - NOT STORED!
//   // Notes are automatically selected from wallet to cover amount + fee
//   // NO source_account needed - authorization via OrchardBundle signatures
//
//   // For z→t: (shielded to transparent)
//   spending_key: <hex-string>           // 32 bytes (64 hex chars) - NOT STORED!
//   source_account: <address>            // Account for transaction format (pays fees)
//   destination_account: <address>       // XRP account receiving
//
//   fee: <optional-drops>                // Override fee
// }
Json::Value
doOrchardPreparePayment(RPC::JsonContext& context)
{
    Json::Value result;

    // Payment type is required
    if (!context.params.isMember(jss::payment_type))
        return RPC::missing_field_error(jss::payment_type);

    if (!context.params[jss::payment_type].isString())
        return RPC::expected_field_error(jss::payment_type, "string");

    auto payment_type = context.params[jss::payment_type].asString();

    // Validate payment type
    if (payment_type != "t_to_z" && payment_type != "z_to_z" && payment_type != "z_to_t")
    {
        result[jss::error] = "invalidParams";
        result[jss::error_message] = "payment_type must be 't_to_z', 'z_to_z', or 'z_to_t'";
        return result;
    }

    // z_to_t is now implemented!

    // Amount is required
    if (!context.params.isMember(jss::amount))
        return RPC::missing_field_error(jss::amount);

    if (!context.params[jss::amount].isString())
        return RPC::expected_field_error(jss::amount, "string");

    std::uint64_t amount_drops = 0;
    try
    {
        amount_drops = std::stoull(context.params[jss::amount].asString());
    }
    catch (...)
    {
        result[jss::error] = "invalidParams";
        result[jss::error_message] = "Invalid amount format";
        return result;
    }

    // Recipient is required for t→z and z→z (Orchard address)
    // For z→t, destination_account is used instead
    std::optional<Blob> recipient_blob;
    if (payment_type == "t_to_z" || payment_type == "z_to_z")
    {
        if (!context.params.isMember(jss::recipient))
            return RPC::missing_field_error(jss::recipient);

        if (!context.params[jss::recipient].isString())
            return RPC::expected_field_error(jss::recipient, "string");

        auto recipient_hex = context.params[jss::recipient].asString();
        recipient_blob = strUnHex(recipient_hex);

        if (!recipient_blob || recipient_blob->size() != 43)
        {
            result[jss::error] = "invalidParams";
            result[jss::error_message] = "recipient must be 43 bytes (86 hex characters)";
            return result;
        }
    }

    // Get current ledger for anchor
    std::shared_ptr<ReadView const> ledger;
    auto ledgerResult = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return ledgerResult;

    // Get the current anchor (empty anchor for now - will be improved)
    auto anchor = ::orchard_test_get_empty_anchor();

    // Build transaction based on payment type
    try
    {
        rust::Vec<uint8_t> bundle_bytes;

        if (payment_type == "t_to_z")
        {
            // Transparent to shielded - requires source account
            if (!context.params.isMember(jss::source_account))
                return RPC::missing_field_error(jss::source_account);

            rust::Slice<const uint8_t> recipient_slice{recipient_blob->data(), recipient_blob->size()};
            auto bundle_bytes_result = ::orchard_test_build_transparent_to_shielded(
                amount_drops,
                recipient_slice,
                anchor);
            bundle_bytes = std::move(bundle_bytes_result);
        }
        else if (payment_type == "z_to_z")
        {
            // Shielded to shielded - requires spending key
            if (!context.params.isMember(jss::spending_key))
                return RPC::missing_field_error(jss::spending_key);

            if (!context.params[jss::spending_key].isString())
                return RPC::expected_field_error(jss::spending_key, "string");

            auto sk_hex = context.params[jss::spending_key].asString();
            auto sk_blob = strUnHex(sk_hex);

            if (!sk_blob || sk_blob->size() != 32)
            {
                result[jss::error] = "invalidParams";
                result[jss::error_message] = "spending_key must be 32 bytes (64 hex characters)";
                return result;
            }

            // Get global OrchardWallet instance
            // Note: The wallet automatically selects notes to cover amount + fee
            auto& wallet = context.app.getOrchardWallet();

            // Check if wallet has sufficient balance
            auto wallet_balance = wallet.getBalance();
            if (wallet_balance < amount_drops)
            {
                result[jss::error] = "invalidParams";
                result[jss::error_message] = "Insufficient wallet balance: have " +
                                              std::to_string(wallet_balance) +
                                              " drops, need " + std::to_string(amount_drops);
                return result;
            }

            // Determine fee for z→z transaction
            std::uint64_t fee_drops;
            if (context.params.isMember(jss::fee))
            {
                try
                {
                    fee_drops = std::stoull(context.params[jss::fee].asString());
                }
                catch (...)
                {
                    result[jss::error] = "invalidParams";
                    result[jss::error_message] = "Invalid fee format";
                    return result;
                }
            }
            else
            {
                // Use base fee from current ledger
                fee_drops = ledger->fees().base.drops();
            }

            // Check wallet has enough for amount + fee
            if (wallet_balance < amount_drops + fee_drops)
            {
                result[jss::error] = "invalidParams";
                result[jss::error_message] = "Insufficient wallet balance: have " +
                                              std::to_string(wallet_balance) +
                                              " drops, need " + std::to_string(amount_drops + fee_drops) +
                                              " (amount + fee)";
                return result;
            }

            // Build z→z bundle using wallet state (includes fee in valueBalance)
            rust::Slice<const uint8_t> sk_slice{sk_blob->data(), sk_blob->size()};
            rust::Slice<const uint8_t> recipient_slice{recipient_blob->data(), recipient_blob->size()};

            auto bundle_bytes_result = ::orchard_wallet_build_z_to_z(
                *wallet.getRustState(),
                sk_slice,
                recipient_slice,
                amount_drops,
                fee_drops);
            bundle_bytes = std::move(bundle_bytes_result);
        }
        else if (payment_type == "z_to_t")
        {
            // Shielded to transparent - requires spending key and destination account
            if (!context.params.isMember(jss::spending_key))
                return RPC::missing_field_error(jss::spending_key);

            if (!context.params[jss::spending_key].isString())
                return RPC::expected_field_error(jss::spending_key, "string");

            auto sk_hex = context.params[jss::spending_key].asString();
            auto sk_blob = strUnHex(sk_hex);

            if (!sk_blob || sk_blob->size() != 32)
            {
                result[jss::error] = "invalidParams";
                result[jss::error_message] = "spending_key must be 32 bytes (64 hex characters)";
                return result;
            }

            // destination_account is required for z→t
            if (!context.params.isMember(jss::destination_account))
                return RPC::missing_field_error(jss::destination_account);

            // Get global OrchardWallet instance
            auto& wallet = context.app.getOrchardWallet();

            // Check if wallet has sufficient balance
            auto wallet_balance = wallet.getBalance();

            // Determine fee for z→t transaction
            std::uint64_t fee_drops;
            if (context.params.isMember(jss::fee))
            {
                try
                {
                    fee_drops = std::stoull(context.params[jss::fee].asString());
                }
                catch (...)
                {
                    result[jss::error] = "invalidParams";
                    result[jss::error_message] = "Invalid fee format";
                    return result;
                }
            }
            else
            {
                // Use base fee from current ledger
                fee_drops = ledger->fees().base.drops();
            }

            // Check wallet has enough for amount + fee
            if (wallet_balance < amount_drops + fee_drops)
            {
                result[jss::error] = "invalidParams";
                result[jss::error_message] = "Insufficient wallet balance: have " +
                                              std::to_string(wallet_balance) +
                                              " drops, need " + std::to_string(amount_drops + fee_drops) +
                                              " (amount + fee)";
                return result;
            }

            // Build z→t bundle using wallet state (includes fee in valueBalance)
            rust::Slice<const uint8_t> sk_slice{sk_blob->data(), sk_blob->size()};

            auto bundle_bytes_result = ::orchard_wallet_build_z_to_t(
                *wallet.getRustState(),
                sk_slice,
                amount_drops,
                fee_drops);
            bundle_bytes = std::move(bundle_bytes_result);
        }

        // Build transaction JSON
        Json::Value tx_json;
        tx_json[jss::TransactionType] = "ShieldedPayment";

        // For t→z, include the source account that pays
        if (payment_type == "t_to_z")
        {
            tx_json[jss::Account] = context.params[jss::source_account].asString();
            tx_json[jss::Amount] = std::to_string(amount_drops);
        }
        else if (payment_type == "z_to_z")
        {
            // For z→z, there is NO Account field required
            // Authorization comes from OrchardBundle cryptographic signatures (spend_auth_sig)
            // The Account field defaults to the zero account (accountID == beast::zero)
            // No Amount field needed - purely shielded transfer
            // Fee is paid from the shielded pool (valueBalance)
        }
        else if (payment_type == "z_to_t")
        {
            // For z→t: Like z→z, NO Account field required
            // Authorization comes from OrchardBundle cryptographic signatures
            // Destination and Amount specify where funds are unshielded to
            // Fee is paid from the shielded pool (valueBalance)
            tx_json[jss::Destination] = context.params[jss::destination_account].asString();
            tx_json[jss::Amount] = std::to_string(amount_drops);
        }

        tx_json["OrchardBundle"] = strHex(std::string_view(
            reinterpret_cast<const char*>(bundle_bytes.data()), bundle_bytes.size()));

        // Add fee if specified
        if (context.params.isMember(jss::fee))
        {
            tx_json[jss::Fee] = context.params[jss::fee].asString();
        }

        result[jss::tx_json] = tx_json;
        result[jss::payment_type] = payment_type;
        result[jss::bundle_size] = static_cast<Json::UInt>(bundle_bytes.size());

        result[jss::warning] = "Bundle generated successfully. This includes a Halo2 proof. "
                               "Sign and submit this transaction to complete the payment.";

        return result;
    }
    catch (std::exception const& e)
    {
        result[jss::error] = "internal";
        result[jss::error_message] = std::string("Failed to build bundle: ") + e.what();
        return result;
    }
}

}  // namespace ripple
