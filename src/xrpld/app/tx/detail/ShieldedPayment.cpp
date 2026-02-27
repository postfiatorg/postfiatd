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

#include <xrpld/app/tx/detail/ShieldedPayment.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/st.h>
#include <xrpl/ledger/View.h>
#include <xrpld/app/misc/OrchardWallet.h>

namespace ripple {

//------------------------------------------------------------------------------

std::optional<OrchardBundleWrapper>
ShieldedPayment::getBundle(STTx const& tx)
{
    if (!tx.isFieldPresent(sfOrchardBundle))
        return std::nullopt;

    auto bundleData = tx[sfOrchardBundle];
    return OrchardBundleWrapper::parse(bundleData);
}

//------------------------------------------------------------------------------

TxConsequences
ShieldedPayment::makeTxConsequences(PreflightContext const& ctx)
{
    // Calculate maximum XRP that could be spent from account
    auto bundle = getBundle(ctx.tx);
    if (!bundle)
    {
        // No bundle means malformed, but that's caught in preflight
        return TxConsequences{ctx.tx, XRPAmount{0}};
    }

    auto valueBalance = bundle->getValueBalance();
    XRPAmount maxSpend{0};

    if (valueBalance < 0)
    {
        // t→z: Account sends amount to shielded pool
        if (ctx.tx.isFieldPresent(sfAmount))
        {
            auto amount = ctx.tx[sfAmount];
            if (amount.native())
                maxSpend = amount.xrp();
        }
    }
    // For z→z and z→t, account may spend nothing if fee paid from shielded

    return TxConsequences{ctx.tx, maxSpend};
}

//------------------------------------------------------------------------------

NotTEC
ShieldedPayment::preflight(PreflightContext const& ctx)
{
    // Check OrchardPrivacy feature is enabled
    if (!ctx.rules.enabled(featureOrchardPrivacy))
        return temDISABLED;

    // Standard preflight checks are performed by Transactor::invokePreflight().
    JLOG(ctx.j.warn()) << "ShieldedPayment::preflight: CALLED";

    // Must have OrchardBundle
    if (!ctx.tx.isFieldPresent(sfOrchardBundle))
    {
        JLOG(ctx.j.warn())
            << "ShieldedPayment: Missing OrchardBundle field";
        return temMALFORMED;
    }

    // Parse and validate bundle
    auto bundle = getBundle(ctx.tx);
    if (!bundle)
    {
        JLOG(ctx.j.warn())
            << "ShieldedPayment: Failed to parse OrchardBundle";
        return temMALFORMED;
    }

    if (!bundle->isValid())
    {
        JLOG(ctx.j.warn())
            << "ShieldedPayment: OrchardBundle structure is invalid";
        return temMALFORMED;
    }

    // Check for empty bundle (no actions)
    if (bundle->numActions() == 0)
    {
        // Empty bundle with Amount field is invalid
        // (no shielded operations but trying to transfer value)
        if (ctx.tx.isFieldPresent(sfAmount))
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: Empty bundle cannot have Amount field";
            return temMALFORMED;
        }
    }

    // Get value balance for field validation
    auto valueBalance = bundle->getValueBalance();

    // Validate field consistency based on value balance
    if (valueBalance < 0)
    {
        // t→z: Must have Amount field
        if (!ctx.tx.isFieldPresent(sfAmount))
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: t→z transaction requires Amount field";
            return temMALFORMED;
        }

        auto amount = ctx.tx[sfAmount];
        if (!amount.native())
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: Only native XRP is supported";
            return temBAD_CURRENCY;
        }

        // Amount must match absolute value of valueBalance
        if (amount.xrp().drops() != -valueBalance)
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: Amount (" << amount.xrp().drops()
                << ") doesn't match valueBalance (" << -valueBalance << ")";
            return temBAD_AMOUNT;
        }
    }

    if (ctx.tx.isFieldPresent(sfDestination))
    {
        // z→t: Must have Amount and positive valueBalance
        if (!ctx.tx.isFieldPresent(sfAmount))
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: z→t transaction requires Amount field";
            return temMALFORMED;
        }

        if (valueBalance <= 0)
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: z→t transaction requires positive "
                   "valueBalance";
            return temMALFORMED;
        }

        auto amount = ctx.tx[sfAmount];
        if (!amount.native())
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: Only native XRP is supported";
            return temBAD_CURRENCY;
        }
    }

    // Check that Amount field is native XRP if present
    if (ctx.tx.isFieldPresent(sfAmount))
    {
        auto amount = ctx.tx[sfAmount];
        if (!amount.native())
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: Only native XRP is supported";
            return temBAD_CURRENCY;
        }

        // Amount must be positive
        if (amount.xrp() <= beast::zero)
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: Amount must be positive";
            return temBAD_AMOUNT;
        }
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TER
ShieldedPayment::preclaim(PreclaimContext const& ctx)
{
    JLOG(ctx.j.warn()) << "ShieldedPayment::preclaim: CALLED for tx "
                       << to_string(ctx.tx.getTransactionID());

    // Get bundle
    auto bundle = getBundle(ctx.tx);
    if (!bundle)
    {
        JLOG(ctx.j.warn())
            << "ShieldedPayment: Bundle not found (should not happen after "
               "preflight)";
        return tefINTERNAL;
    }

    auto valueBalance = bundle->getValueBalance();

    // Verify zero-knowledge proof (EXPENSIVE!)
    auto sighash = ctx.tx.getSigningHash();
    if (!bundle->verifyProof(sighash))
    {
        JLOG(ctx.j.warn())
            << "ShieldedPayment: Halo2 proof verification failed";
        return tefORCHARD_INVALID_PROOF;
    }

    // Check for double-spends (nullifiers already used)
    auto nullifiers = bundle->getNullifiers();
    for (auto const& nf : nullifiers)
    {
        if (ctx.view.exists(keylet::orchardNullifier(nf)))
        {
            JLOG(ctx.j.warn()) << "ShieldedPayment: Duplicate nullifier "
                                  "detected (double-spend attempt)";
            return tefORCHARD_DUPLICATE_NULLIFIER;
        }
    }

    // Verify anchor exists in recent ledger history
    // For the empty anchor (first transactions), we'll auto-create it in doApply
    auto anchor = bundle->getAnchor();
    JLOG(ctx.j.warn())
        << "ShieldedPayment: Checking anchor: " << to_string(anchor);

    if (!ctx.view.exists(keylet::orchardAnchor(anchor)))
    {
        // Check if this is the empty anchor (for first transactions)
        // The empty anchor is a well-known constant, so we can accept it here
        // and create it in doApply if needed
        auto emptyAnchorBytes = orchard_test_get_empty_anchor();
        uint256 emptyAnchor;
        std::memcpy(emptyAnchor.data(), emptyAnchorBytes.data(), 32);

        if (anchor != emptyAnchor)
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: Anchor not found in ledger history";
            return tefORCHARD_INVALID_ANCHOR;
        }

        JLOG(ctx.j.info())
            << "ShieldedPayment: Empty anchor will be created in doApply";
    }

    JLOG(ctx.j.warn())
        << "ShieldedPayment: Anchor validated successfully";

    // Check destination (if z→t)
    if (ctx.tx.isFieldPresent(sfDestination))
    {
        auto const destID = ctx.tx[sfDestination];
        auto const sleDest = ctx.view.read(keylet::account(destID));

        if (!sleDest)
        {
            // Destination doesn't exist - check if we can create it
            auto amount = ctx.tx[sfAmount].xrp();
            auto reserve = ctx.view.fees().accountReserve(0);

            if (amount < reserve)
            {
                JLOG(ctx.j.warn())
                    << "ShieldedPayment: Insufficient amount to create "
                       "destination account";
                return tecNO_DST_INSUF_XRP;
            }
        }

        // Check destination tag requirements
        if (sleDest && (sleDest->getFlags() & lsfRequireDestTag))
        {
            if (!ctx.tx.isFieldPresent(sfDestinationTag))
            {
                JLOG(ctx.j.warn())
                    << "ShieldedPayment: Destination requires tag";
                return tecDST_TAG_NEEDED;
            }
        }
    }

    // Check account balance for fees and amounts (skip for z->z with no account)
    auto const accountID = ctx.tx.getAccountID(sfAccount);

    if (accountID != beast::zero)
    {
        // Traditional transaction with account
        auto const sleAccount = ctx.view.read(keylet::account(accountID));
        if (!sleAccount)
            return terNO_ACCOUNT;

        auto balance = (*sleAccount)[sfBalance].xrp();
        auto fee = ctx.tx[sfFee].xrp();

        if (valueBalance < 0)
        {
            // t→z: Account must pay amount + fee
            auto amount = ctx.tx[sfAmount].xrp();
            if (balance < amount + fee)
            {
                JLOG(ctx.j.warn())
                    << "ShieldedPayment: Insufficient balance for amount and fee";
                return tecUNFUNDED_PAYMENT;
            }
        }
        else if (valueBalance < fee.drops())
        {
            // Partial or full fee from transparent
            if (balance < fee)
            {
                JLOG(ctx.j.warn())
                    << "ShieldedPayment: Insufficient balance for fee";
                return tecINSUFF_FEE;
            }
        }
        // else: Fee fully paid from shielded pool (valueBalance >= fee)
    }
    else
    {
        // z->z transaction: no account, fee must be paid from shielded pool
        auto fee = ctx.tx[sfFee].xrp();

        if (valueBalance < fee.drops())
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: z->z transaction requires fee payment from shielded pool";
            return tecINSUFF_FEE;
        }

        // z->z with no destination means pure shielded transfer
        // valueBalance should equal fee (no transparent output)
        if (!ctx.tx.isFieldPresent(sfDestination) && valueBalance != fee.drops())
        {
            JLOG(ctx.j.warn())
                << "ShieldedPayment: z->z valueBalance must equal fee for pure shielded transfer";
            return temBAD_AMOUNT;
        }
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

NotTEC
ShieldedPayment::checkSign(PreclaimContext const& ctx)
{
    // For z->z transactions with no account, authorization comes from
    // the OrchardBundle cryptographic signatures (spend_auth_sig).
    // The verifyProof() call in preclaim() already validates these signatures.
    auto const accountID = ctx.tx.getAccountID(sfAccount);

    if (accountID == beast::zero)
    {
        // z->z transaction: authorization is cryptographically proven
        // in the OrchardBundle via RedPallas signatures on each spend action.
        // The spend_auth_sig proves control of the spent notes without
        // needing an account signature.
        //
        // Verification is already done in preclaim() via verifyProof(),
        // which checks:
        // 1. Zero-knowledge proof validity
        // 2. spend_auth_sig for each Action
        // 3. Binding signature for value balance
        //
        // No additional signature check needed here.
        return tesSUCCESS;
    }

    // Traditional account-based authorization
    return Transactor::checkSign(ctx);
}

//------------------------------------------------------------------------------

TER
ShieldedPayment::doApply()
{
    JLOG(j_.warn()) << "ShieldedPayment::doApply: CALLED";

    // Get bundle
    auto bundle = getBundle(ctx_.tx);
    if (!bundle)
    {
        JLOG(j_.warn()) << "ShieldedPayment: Bundle not found in doApply "
                           "(should not happen)";
        return tefINTERNAL;
    }

    auto valueBalance = bundle->getValueBalance();
    XRPAmount fee = ctx_.tx[sfFee].xrp();

    JLOG(j_.warn()) << "ShieldedPayment::doApply: valueBalance=" << valueBalance
                    << " fee=" << fee;

    // Handle transparent input (t→z)
    if (valueBalance < 0)
    {
        XRPAmount amount = ctx_.tx[sfAmount].xrp();
        auto sleAccount = view().peek(keylet::account(account_));

        if (!sleAccount)
        {
            JLOG(j_.warn())
                << "ShieldedPayment: Source account not found in doApply";
            return tefINTERNAL;
        }

        // Deduct amount from account (going to shielded pool)
        // Note: Fee is automatically deducted by Transactor::payFee(), not here!
        (*sleAccount)[sfBalance] = (*sleAccount)[sfBalance] - amount;
        view().update(sleAccount);

        JLOG(j_.trace()) << "ShieldedPayment: Debited " << amount
                         << " from account (t→z), fee handled by base class";
    }

    // Handle transparent output (z→t)
    if (ctx_.tx.isFieldPresent(sfDestination))
    {
        XRPAmount amount = ctx_.tx[sfAmount].xrp();
        AccountID const destID = ctx_.tx[sfDestination];

        auto sleDest = view().peek(keylet::account(destID));

        if (!sleDest)
        {
            // Create destination account
            sleDest = std::make_shared<SLE>(keylet::account(destID));
            (*sleDest)[sfBalance] = amount;
            (*sleDest)[sfAccount] = destID;
            (*sleDest)[sfSequence] = view().seq();
            view().insert(sleDest);

            JLOG(j_.trace()) << "ShieldedPayment: Created destination account "
                             << toBase58(destID) << " with " << amount;
        }
        else
        {
            // Credit existing account
            (*sleDest)[sfBalance] = (*sleDest)[sfBalance] + amount;
            view().update(sleDest);

            JLOG(j_.trace()) << "ShieldedPayment: Credited " << amount
                             << " to destination (z→t)";
        }
    }

    // Note: Fee payment is handled automatically by Transactor::payFee()
    // We don't need to deduct it here

    // Store nullifiers (mark as spent to prevent double-spend)
    auto nullifiers = bundle->getNullifiers();
    for (auto const& nf : nullifiers)
    {
        auto sleNullifier = std::make_shared<SLE>(keylet::orchardNullifier(nf));
        // Nullifiers are minimal objects - just their presence matters
        view().insert(sleNullifier);

        JLOG(j_.trace()) << "ShieldedPayment: Stored nullifier "
                         << to_string(nf);
    }

    // Store note commitments (outputs that can be spent in future transactions)
    // This allows wallets to scan the ledger and trial-decrypt notes with their viewing keys
    // We only store the encrypted note + ephemeral key, not the full bundle
    // The bundle is already stored in the transaction itself
    auto encryptedNotes = bundle->getEncryptedNotes();

    // Update wallet with new notes
    // CRITICAL: Only update wallet tree during CONSENSUS, not during OpenView validation
    // This prevents double-updating the tree which causes anchor mismatches
    // Following XRP Ledger pattern (see Transactor.cpp:1346):
    //   - view().open() == true during OpenView (validation phase)
    //   - view().open() == false during LedgerConsensus (final ledger building)
    // The commitment tree should only grow during consensus to match Zcash's approach
    auto& wallet = ctx_.app.getOrchardWallet();

    // CRITICAL: Mark notes as spent in wallet (only during consensus)
    // This keeps the wallet's in-memory spent_notes set synchronized with on-chain nullifier set
    // Following Zcash's approach: when a nullifier is revealed, the corresponding note is spent
    if (!view().open())
    {
        for (auto const& nf : nullifiers)
        {
            wallet.markSpent(nf);
            JLOG(j_.warn()) << "ShieldedPayment::doApply: [CONSENSUS] Marked note as spent, nullifier: "
                            << to_string(nf);
        }
    }

    // NOTE: Checkpoint is now done at the LEDGER level in BuildLedger.cpp
    // Following Zcash's approach: checkpoint once per ledger (block), not per transaction
    // This ensures the tree state is checkpointed before ANY transactions are applied

    size_t commitment_idx = 0;
    for (auto const& noteData : encryptedNotes)
    {
        auto sleCommitment =
            std::make_shared<SLE>(keylet::orchardNoteCommitment(noteData.cmx));
        sleCommitment->setFieldU32(sfLedgerSequence, view().seq());
        sleCommitment->setFieldVL(sfOrchardEncryptedNote, noteData.encryptedNote);
        sleCommitment->setFieldVL(sfOrchardEphemeralKey, noteData.ephemeralKey);
        // Note: We do NOT store sfOrchardBundle here to avoid duplication
        // Wallets decrypt from transaction bundle during real-time processing
        // or from the encrypted note + ephemeral key for historical scanning
        view().insert(sleCommitment);

        JLOG(j_.trace()) << "ShieldedPayment: Stored note commitment "
                         << to_string(noteData.cmx) << " at ledger " << view().seq();

        // Only update wallet tree during consensus, NOT during OpenView
        if (!view().open())
        {
            // Log wallet anchor BEFORE adding this commitment
            auto anchor_before_commitment = wallet.getAnchor();

            JLOG(j_.warn()) << "ShieldedPayment::doApply: [CONSENSUS] BEFORE adding commitment #" << commitment_idx
                            << " (cmx=" << to_string(noteData.cmx) << "), wallet anchor: "
                            << (anchor_before_commitment ? to_string(*anchor_before_commitment) : "EMPTY");

            // Add commitment to wallet tree
            wallet.appendCommitment(noteData.cmx);

            // Log wallet anchor AFTER adding this commitment
            auto anchor_after_commitment = wallet.getAnchor();

            JLOG(j_.warn()) << "ShieldedPayment::doApply: [CONSENSUS] AFTER adding commitment #" << commitment_idx
                            << ", wallet anchor: "
                            << (anchor_after_commitment ? to_string(*anchor_after_commitment) : "EMPTY");
        }
        else
        {
            JLOG(j_.trace()) << "ShieldedPayment::doApply: [OPENVIEW] Skipping wallet tree update for commitment #" << commitment_idx
                             << " (cmx=" << to_string(noteData.cmx) << ")";
        }

        commitment_idx++;
    }

    // Try to decrypt notes with registered IVKs (only during consensus)
    // This is the key step for automatic note detection!
    size_t decryptedCount = 0;
    if (!view().open())
    {
        JLOG(j_.warn()) << "ShieldedPayment::doApply: [CONSENSUS] About to call wallet.tryDecryptNotes, tracked_keys="
                        << wallet.getIncomingViewingKeyCount();

        decryptedCount = wallet.tryDecryptNotes(*bundle, ctx_.tx.getTransactionID(), view().seq());

        JLOG(j_.warn()) << "ShieldedPayment::doApply: [CONSENSUS] Decrypted " << decryptedCount << " notes";
    }
    else
    {
        JLOG(j_.trace()) << "ShieldedPayment::doApply: [OPENVIEW] Skipping wallet.tryDecryptNotes";
    }

    if (decryptedCount > 0)
    {
        JLOG(j_.info()) << "ShieldedPayment: Decrypted " << decryptedCount
                        << " note(s) for tracked keys in tx "
                        << to_string(ctx_.tx.getTransactionID());
    }

    // CRITICAL: Store anchors following Zcash's approach
    // Zcash stores the tree root AFTER appending bundle commitments (main.cpp:3516-3518)
    // We need to store BOTH:
    // 1. The bundle's anchor (for validation) - t→z uses empty anchor, z→z uses historical
    // 2. The wallet's current anchor (for future transactions) - this is what next tx will reference

    auto bundle_anchor = bundle->getAnchor();
    JLOG(j_.warn()) << "ShieldedPayment::doApply: Bundle anchor (from bundle): " << to_string(bundle_anchor);

    // Store the bundle's anchor (this validates that the spend was against a known tree state)
    auto sleAnchorBundle = view().peek(keylet::orchardAnchor(bundle_anchor));
    if (!sleAnchorBundle)
    {
        sleAnchorBundle = std::make_shared<SLE>(keylet::orchardAnchor(bundle_anchor));
        (*sleAnchorBundle)[sfLedgerSequence] = view().seq();
        view().insert(sleAnchorBundle);

        JLOG(j_.warn()) << "ShieldedPayment: Stored bundle anchor "
                        << to_string(bundle_anchor) << " at ledger "
                        << view().seq();
    }

    // Get wallet's current anchor AFTER adding this bundle's commitments
    // This is the tree root that FUTURE transactions will use when spending notes created by this tx
    auto wallet_anchor_opt = wallet.getAnchor();
    if (wallet_anchor_opt)
    {
        auto wallet_anchor = *wallet_anchor_opt;
        JLOG(j_.warn()) << "ShieldedPayment::doApply: Wallet anchor after adding commitments: "
                        << to_string(wallet_anchor);

        // Store the wallet's current anchor (matching Zcash's PushAnchor after processing tx)
        // Only store if different from bundle anchor
        if (wallet_anchor != bundle_anchor)
        {
            auto sleAnchorWallet = view().peek(keylet::orchardAnchor(wallet_anchor));
            if (!sleAnchorWallet)
            {
                sleAnchorWallet = std::make_shared<SLE>(keylet::orchardAnchor(wallet_anchor));
                (*sleAnchorWallet)[sfLedgerSequence] = view().seq();
                view().insert(sleAnchorWallet);

                JLOG(j_.warn()) << "ShieldedPayment: Stored wallet anchor (for future txs) "
                                << to_string(wallet_anchor) << " at ledger "
                                << view().seq();
            }
        }
    }

    JLOG(j_.info()) << "ShieldedPayment: Successfully applied transaction";

    return tesSUCCESS;
}

}  // namespace ripple
