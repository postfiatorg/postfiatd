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
#include <xrpld/app/misc/OrchardScanner.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/ledger/LedgerMaster.h>

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

        // Add the new IVK without resetting the wallet
        // The commitment tree must persist to match anchors stored in the ledger
        if (!wallet.addIncomingViewingKey(ivk_blob))
        {
            result[jss::error] = "internal";
            result[jss::error_message] = "Failed to add IVK to wallet";
            return result;
        }

        // Scan backward through existing ledgers to find notes for this key
        // This allows the wallet to discover historical notes
        auto& ledgerMaster = context.ledgerMaster;
        auto current_ledger = ledgerMaster.getValidatedLedger();

        std::size_t notes_found = 0;

        if (current_ledger)
        {
            LedgerIndex current_seq = current_ledger->seq();
            LedgerIndex start_seq = (current_seq > 1000) ? (current_seq - 1000) : 1;  // Scan last 1000 ledgers

            JLOG(context.j.warn()) << "OrchardWalletAddKey: Scanning ledgers " << start_seq
                                   << " to " << current_seq;

            // Scan from start to current, processing each ShieldedPayment transaction
            for (LedgerIndex seq = start_seq; seq <= current_seq; ++seq)
            {
                auto scan_ledger = ledgerMaster.getLedgerBySeq(seq);
                if (!scan_ledger)
                    continue;

                // Iterate through all transactions in this ledger
                for (auto const& item : scan_ledger->txs)
                {
                    auto const& [sttx, stmeta] = item;
                    if (!sttx || sttx->getTxnType() != ttSHIELDED_PAYMENT)
                        continue;

                    // Extract OrchardBundle if present
                    if (!sttx->isFieldPresent(sfOrchardBundle))
                        continue;

                    auto const& bundle_blob = sttx->getFieldVL(sfOrchardBundle);
                    if (bundle_blob.empty())
                        continue;

                    try
                    {
                        // Parse the bundle
                        Slice bundle_slice(bundle_blob.data(), bundle_blob.size());
                        auto bundle = OrchardBundleWrapper::parse(bundle_slice);
                        if (!bundle)
                            continue;

                        // Get transaction hash
                        uint256 tx_hash = sttx->getTransactionID();

                        JLOG(context.j.warn())
                            << "OrchardWalletAddKey: Found ShieldedPayment tx "
                            << to_string(tx_hash) << " at ledger " << seq;

                        // First add ALL commitments to tree for witness computation
                        // This must happen BEFORE tryDecryptNotes() to avoid overflow
                        auto commitments = bundle->getNoteCommitments();
                        JLOG(context.j.warn())
                            << "OrchardWalletAddKey: Adding " << commitments.size()
                            << " commitments to wallet tree";

                        for (auto const& cmx : commitments)
                        {
                            wallet.appendCommitment(cmx);
                            JLOG(context.j.trace())
                                << "OrchardWalletAddKey: Added commitment " << to_string(cmx);
                        }

                        // Now try to decrypt notes from this bundle
                        // The tree now has commitments, so witness creation won't overflow
                        std::size_t decrypted = wallet.tryDecryptNotes(*bundle, tx_hash, seq);
                        notes_found += decrypted;

                        if (decrypted > 0)
                        {
                            JLOG(context.j.warn())
                                << "OrchardWalletAddKey: Decrypted " << decrypted
                                << " notes from tx " << to_string(tx_hash);
                        }
                    }
                    catch (...)
                    {
                        // Failed to parse bundle or decrypt notes
                        continue;
                    }
                }

                // Checkpoint after each ledger
                wallet.checkpoint(seq);
            }
        }

        // Check wallet state after scanning
        auto anchor_opt = wallet.getAnchor();
        if (anchor_opt)
        {
            JLOG(context.j.warn())
                << "OrchardWalletAddKey: Wallet anchor after scan: "
                << to_string(*anchor_opt);
        }
        else
        {
            JLOG(context.j.warn())
                << "OrchardWalletAddKey: Wallet has no anchor (empty tree)";
        }

        // Return success
        result[jss::status] = "success";
        result["ivk"] = strHex(ivk_blob);
        result["tracked_keys"] = static_cast<unsigned>(wallet.getIncomingViewingKeyCount());
        result["notes_found"] = static_cast<unsigned>(notes_found);
        result["balance"] = std::to_string(wallet.getBalance());

        if (anchor_opt)
        {
            result["anchor"] = to_string(*anchor_opt);
        }

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
