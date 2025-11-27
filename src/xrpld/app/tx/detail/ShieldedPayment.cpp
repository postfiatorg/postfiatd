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
#include <xrpld/ledger/View.h>

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

    // Standard preflight checks (account, fee, etc.)
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

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

    // Signature validation
    return preflight2(ctx);
}

//------------------------------------------------------------------------------

TER
ShieldedPayment::preclaim(PreclaimContext const& ctx)
{
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
    auto anchor = bundle->getAnchor();
    if (!ctx.view.exists(keylet::orchardAnchor(anchor)))
    {
        JLOG(ctx.j.warn())
            << "ShieldedPayment: Anchor not found in ledger history";
        return tefORCHARD_INVALID_ANCHOR;
    }

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

    // Check account balance for fees and amounts
    auto const sleAccount = ctx.view.read(keylet::account(ctx.tx[sfAccount]));
    if (!sleAccount)
    {
        JLOG(ctx.j.warn())
            << "ShieldedPayment: Source account does not exist";
        return terNO_ACCOUNT;
    }

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
            return tecINSUF_FEE;
        }
    }
    // else: Fee fully paid from shielded pool (valueBalance >= fee)

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

TER
ShieldedPayment::doApply()
{
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
        (*sleAccount)[sfBalance] = (*sleAccount)[sfBalance] - amount;
        view().update(sleAccount);

        JLOG(j_.trace()) << "ShieldedPayment: Debited " << amount
                         << " from account (t→z)";
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

    // Handle fee payment
    if (valueBalance < fee.drops())
    {
        // Fee from transparent account (partial or full)
        XRPAmount transparentFee = fee;

        if (valueBalance > 0)
        {
            // Partial fee from shielded
            transparentFee = fee - XRPAmount(valueBalance);
        }

        auto sleAccount = view().peek(keylet::account(account_));
        (*sleAccount)[sfBalance] = (*sleAccount)[sfBalance] - transparentFee;
        view().update(sleAccount);

        JLOG(j_.trace()) << "ShieldedPayment: Fee " << transparentFee
                         << " paid from transparent account";
    }
    else
    {
        JLOG(j_.trace()) << "ShieldedPayment: Fee " << fee
                         << " paid from shielded pool";
    }
    // else: Fee fully paid from shielded pool (included in valueBalance)

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

    JLOG(j_.info()) << "ShieldedPayment: Successfully applied transaction";

    return tesSUCCESS;
}

}  // namespace ripple
