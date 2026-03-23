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

#ifndef RIPPLE_TX_SHIELDEDPAYMENT_H_INCLUDED
#define RIPPLE_TX_SHIELDEDPAYMENT_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>
#include <xrpl/protocol/OrchardBundle.h>

namespace ripple {

/** Transactor for ShieldedPayment transactions.

    ShieldedPayment handles all Orchard shielded operations:
    - t→z: Shield transparent funds into privacy pool
    - z→z: Transfer funds privately between shielded addresses
    - z→t: Unshield funds from privacy pool to transparent account

    The transaction uses Zcash's value balance model:
    - Negative valueBalance: transparent → shielded
    - Positive valueBalance: shielded → transparent (can pay fees!)
    - Zero valueBalance: fully shielded

    @see ttSHIELDED_PAYMENT in transactions.macro
    @see OrchardValueBalance.md for complete documentation
*/
class ShieldedPayment : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit ShieldedPayment(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Calculate transaction consequences (maximum XRP spend).

        For t→z transactions, the account spends the Amount field.
        For z→z and z→t, the account may spend nothing (fee from shielded).

        @param ctx Preflight context
        @return Transaction consequences
    */
    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Preflight validation (static checks, no ledger access).

        Validates:
        - OrchardPrivacy feature is enabled
        - OrchardBundle is present and structurally valid
        - Field consistency based on valueBalance
        - Standard preflight checks (account, fee, signatures)

        @param ctx Preflight context
        @return NotTEC error code or tesSUCCESS
    */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Preclaim validation (ledger-based checks).

        Validates:
        - Zero-knowledge proof verification (expensive!)
        - No double-spends (nullifiers not already used)
        - Anchor exists in recent ledger history
        - Destination account constraints (if z→t)
        - Sufficient balance for fees and amounts

        @param ctx Preclaim context
        @return TER error code or tesSUCCESS
    */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Check transaction signature authorization.

        For z→z transactions without account field, authorization comes
        from OrchardBundle cryptographic signatures (spend_auth_sig).
        For traditional transactions, uses standard account signature verification.

        @param ctx Preclaim context
        @return NotTEC error code or tesSUCCESS
    */
    static NotTEC
    checkSign(PreclaimContext const& ctx);

    /** Apply transaction to ledger (execution).

        Performs:
        - Deduct amount from account (if t→z)
        - Credit destination account (if z→t)
        - Handle fee payment (transparent or shielded)
        - Store nullifiers (prevent double-spend)

        @return TER error code or tesSUCCESS
    */
    TER
    doApply() override;

private:
    /** Helper to parse OrchardBundle from transaction.

        @param tx Transaction
        @return Parsed bundle or std::nullopt if not present
    */
    static std::optional<OrchardBundleWrapper>
    getBundle(STTx const& tx);
};

}  // namespace ripple

#endif
