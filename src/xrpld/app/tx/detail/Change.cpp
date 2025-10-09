//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/app/misc/ExclusionManager.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/tx/detail/Change.h>
#include <xrpld/core/UNLConfig.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/Sandbox.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpld/app/misc/TxQ.h>

#include <string_view>

namespace ripple {

NotTEC
Change::preflight(PreflightContext const& ctx)
{
    auto const ret = preflight0(ctx);
    if (!isTesSuccess(ret))
    {
        if (ctx.tx.getTxnType() == ttVALIDATOR_VOTE)
        {
            JLOG(ctx.j.warn()) << "ValidatorVote: preflight0 failed with " << transToken(ret);
        }
        return ret;
    }

    auto account = ctx.tx.getAccountID(sfAccount);
    if (account != beast::zero)
    {
        JLOG(ctx.j.warn()) << "Change: Bad source id";
        return temBAD_SRC_ACCOUNT;
    }

    // No point in going any further if the transaction fee is malformed.
    auto const fee = ctx.tx.getFieldAmount(sfFee);
    if (!fee.native() || fee != beast::zero)
    {
        JLOG(ctx.j.warn()) << "Change: invalid fee";
        return temBAD_FEE;
    }

    if (!ctx.tx.getSigningPubKey().empty() || !ctx.tx.getSignature().empty() ||
        ctx.tx.isFieldPresent(sfSigners))
    {
        JLOG(ctx.j.warn()) << "Change: Bad signature";
        return temBAD_SIGNATURE;
    }

    if (ctx.tx.getFieldU32(sfSequence) != 0 ||
        ctx.tx.isFieldPresent(sfPreviousTxnID))
    {
        JLOG(ctx.j.warn()) << "Change: Bad sequence";
        return temBAD_SEQUENCE;
    }

    if (ctx.tx.getTxnType() == ttUNL_MODIFY &&
        !ctx.rules.enabled(featureNegativeUNL))
    {
        JLOG(ctx.j.warn()) << "Change: NegativeUNL not enabled";
        return temDISABLED;
    }

    if (ctx.tx.getTxnType() == ttVALIDATOR_VOTE &&
        !ctx.rules.enabled(featureValidatorVoteTracking))
    {
        JLOG(ctx.j.warn()) << "Change: ValidatorVoteTracking not enabled";
        return temDISABLED;
    }

    return tesSUCCESS;
}

TER
Change::preclaim(PreclaimContext const& ctx)
{
    // If tapOPEN_LEDGER is resurrected into ApplyFlags,
    // this block can be moved to preflight.
    if (ctx.view.open())
    {
        JLOG(ctx.j.warn()) << "Change transaction against open ledger";
        return temINVALID;
    }

    switch (ctx.tx.getTxnType())
    {
        case ttFEE:
            if (ctx.view.rules().enabled(featureXRPFees))
            {
                // The ttFEE transaction format defines these fields as
                // optional, but once the XRPFees feature is enabled, they are
                // required.
                if (!ctx.tx.isFieldPresent(sfBaseFeeDrops) ||
                    !ctx.tx.isFieldPresent(sfReserveBaseDrops) ||
                    !ctx.tx.isFieldPresent(sfReserveIncrementDrops))
                    return temMALFORMED;
                // The ttFEE transaction format defines these fields as
                // optional, but once the XRPFees feature is enabled, they are
                // forbidden.
                if (ctx.tx.isFieldPresent(sfBaseFee) ||
                    ctx.tx.isFieldPresent(sfReferenceFeeUnits) ||
                    ctx.tx.isFieldPresent(sfReserveBase) ||
                    ctx.tx.isFieldPresent(sfReserveIncrement))
                    return temMALFORMED;
            }
            else
            {
                // The ttFEE transaction format formerly defined these fields
                // as required. When the XRPFees feature was implemented, they
                // were changed to be optional. Until the feature has been
                // enabled, they are required.
                if (!ctx.tx.isFieldPresent(sfBaseFee) ||
                    !ctx.tx.isFieldPresent(sfReferenceFeeUnits) ||
                    !ctx.tx.isFieldPresent(sfReserveBase) ||
                    !ctx.tx.isFieldPresent(sfReserveIncrement))
                    return temMALFORMED;
                // The ttFEE transaction format defines these fields as
                // optional, but without the XRPFees feature, they are
                // forbidden.
                if (ctx.tx.isFieldPresent(sfBaseFeeDrops) ||
                    ctx.tx.isFieldPresent(sfReserveBaseDrops) ||
                    ctx.tx.isFieldPresent(sfReserveIncrementDrops))
                    return temDISABLED;
            }
            return tesSUCCESS;
        case ttAMENDMENT:
        case ttUNL_MODIFY:
            return tesSUCCESS;
        case ttVALIDATOR_VOTE:
            JLOG(ctx.j.info()) << "ValidatorVote: Preclaim passed for tx " 
                               << ctx.tx.getTransactionID();
            return tesSUCCESS;
        default:
            return temUNKNOWN;
    }
}

TER
Change::doApply()
{
    switch (ctx_.tx.getTxnType())
    {
        case ttAMENDMENT:
            return applyAmendment();
        case ttFEE:
            return applyFee();
        case ttUNL_MODIFY:
            return applyUNLModify();
        case ttVALIDATOR_VOTE:
        {
            JLOG(j_.info()) << "ValidatorVote: Calling applyValidatorVote";
            TER result = applyValidatorVote();
            JLOG(j_.info()) << "ValidatorVote: applyValidatorVote returned " << transToken(result);
            return result;
        }
        default:
            UNREACHABLE("ripple::Change::doApply : invalid transaction type");
            return tefFAILURE;
    }
}

void
Change::preCompute()
{
    XRPL_ASSERT(
        account_ == beast::zero, "ripple::Change::preCompute : zero account");
}

void
Change::reloadUNL()
{
    JLOG(j_.warn()) << "UNL Update amendment activated - reloading validator list";

    // Get the active UNL based on enabled amendments (single source of truth)
    std::vector<std::string> activeValidatorList =
        UNLConfig::getActiveUNL(ctx_.app.getAmendmentTable());

    JLOG(j_.warn()) << "Reloading UNL with "
                    << activeValidatorList.size() << " validators";

    // Get the local signing key if available
    std::optional<PublicKey> localSigningKey;
    if (ctx_.app.getValidationPublicKey())
        localSigningKey = *ctx_.app.getValidationPublicKey();

    // Reload the validator list
    if (!ctx_.app.validators().load(
            localSigningKey,
            activeValidatorList,
            {},
            {}))
    {
        JLOG(j_.error()) << "Failed to reload validator list after UNL update amendment";
        return;
    }

    JLOG(j_.warn()) << "UNL successfully reloaded with " << activeValidatorList.size() << " validators";

    // Notify the amendment table of the new trusted validators
    ctx_.app.getAmendmentTable().trustChanged(
        ctx_.app.validators().getQuorumKeys().second);
}

void
Change::activateTrustLinesToSelfFix()
{
    JLOG(j_.warn()) << "fixTrustLinesToSelf amendment activation code starting";

    auto removeTrustLineToSelf = [this](Sandbox& sb, uint256 id) {
        auto tl = sb.peek(keylet::child(id));

        if (tl == nullptr)
        {
            JLOG(j_.warn()) << id << ": Unable to locate trustline";
            return true;
        }

        if (tl->getType() != ltRIPPLE_STATE)
        {
            JLOG(j_.warn()) << id << ": Unexpected type "
                            << static_cast<std::uint16_t>(tl->getType());
            return true;
        }

        auto const& lo = tl->getFieldAmount(sfLowLimit);
        auto const& hi = tl->getFieldAmount(sfHighLimit);

        if (lo != hi)
        {
            JLOG(j_.warn()) << id << ": Trustline doesn't meet requirements";
            return true;
        }

        if (auto const page = tl->getFieldU64(sfLowNode); !sb.dirRemove(
                keylet::ownerDir(lo.getIssuer()), page, tl->key(), false))
        {
            JLOG(j_.error()) << id << ": failed to remove low entry from "
                             << toBase58(lo.getIssuer()) << ":" << page
                             << " owner directory";
            return false;
        }

        if (auto const page = tl->getFieldU64(sfHighNode); !sb.dirRemove(
                keylet::ownerDir(hi.getIssuer()), page, tl->key(), false))
        {
            JLOG(j_.error()) << id << ": failed to remove high entry from "
                             << toBase58(hi.getIssuer()) << ":" << page
                             << " owner directory";
            return false;
        }

        if (tl->getFlags() & lsfLowReserve)
            adjustOwnerCount(
                sb, sb.peek(keylet::account(lo.getIssuer())), -1, j_);

        if (tl->getFlags() & lsfHighReserve)
            adjustOwnerCount(
                sb, sb.peek(keylet::account(hi.getIssuer())), -1, j_);

        sb.erase(tl);

        JLOG(j_.warn()) << "Successfully deleted trustline " << id;

        return true;
    };

    using namespace std::literals;

    Sandbox sb(&view());

    if (removeTrustLineToSelf(
            sb,
            uint256{
                "2F8F21EFCAFD7ACFB07D5BB04F0D2E18587820C7611305BB674A64EAB0FA71E1"sv}) &&
        removeTrustLineToSelf(
            sb,
            uint256{
                "326035D5C0560A9DA8636545DD5A1B0DFCFF63E68D491B5522B767BB00564B1A"sv}))
    {
        JLOG(j_.warn()) << "fixTrustLinesToSelf amendment activation code "
                           "executed successfully";
        sb.apply(ctx_.rawView());
    }
}

TER
Change::applyAmendment()
{
    uint256 amendment(ctx_.tx.getFieldH256(sfAmendment));

    auto const k = keylet::amendments();

    SLE::pointer amendmentObject = view().peek(k);

    if (!amendmentObject)
    {
        amendmentObject = std::make_shared<SLE>(k);
        view().insert(amendmentObject);
    }

    STVector256 amendments = amendmentObject->getFieldV256(sfAmendments);

    if (std::find(amendments.begin(), amendments.end(), amendment) !=
        amendments.end())
        return tefALREADY;

    auto flags = ctx_.tx.getFlags();

    bool const gotMajority = (flags & tfGotMajority) != 0;
    bool const lostMajority = (flags & tfLostMajority) != 0;

    if (gotMajority && lostMajority)
        return temINVALID_FLAG;

    STArray newMajorities(sfMajorities);

    bool found = false;
    if (amendmentObject->isFieldPresent(sfMajorities))
    {
        STArray const& oldMajorities =
            amendmentObject->getFieldArray(sfMajorities);
        for (auto const& majority : oldMajorities)
        {
            if (majority.getFieldH256(sfAmendment) == amendment)
            {
                if (gotMajority)
                    return tefALREADY;
                found = true;
            }
            else
            {
                // pass through
                newMajorities.push_back(majority);
            }
        }
    }

    if (!found && lostMajority)
        return tefALREADY;

    if (gotMajority)
    {
        // This amendment now has a majority
        newMajorities.push_back(STObject::makeInnerObject(sfMajority));
        auto& entry = newMajorities.back();
        entry[sfAmendment] = amendment;
        entry[sfCloseTime] =
            view().parentCloseTime().time_since_epoch().count();

        if (!ctx_.app.getAmendmentTable().isSupported(amendment))
        {
            JLOG(j_.warn()) << "Unsupported amendment " << amendment
                            << " received a majority.";
        }
    }
    else if (!lostMajority)
    {
        // No flags, enable amendment
        amendments.push_back(amendment);
        amendmentObject->setFieldV256(sfAmendments, amendments);

        if (amendment == fixTrustLinesToSelf)
            activateTrustLinesToSelfFix();

        // Check if this is a UNL update amendment and reload the validator list
        if (amendment == featureUNLUpdate1)
            reloadUNL();

        ctx_.app.getAmendmentTable().enable(amendment);

        if (!ctx_.app.getAmendmentTable().isSupported(amendment))
        {
            JLOG(j_.error()) << "Unsupported amendment " << amendment
                             << " activated: server blocked.";
            ctx_.app.getOPs().setAmendmentBlocked();
        }
    }

    if (newMajorities.empty())
        amendmentObject->makeFieldAbsent(sfMajorities);
    else
        amendmentObject->setFieldArray(sfMajorities, newMajorities);

    view().update(amendmentObject);

    return tesSUCCESS;
}

TER
Change::applyFee()
{
    auto const k = keylet::fees();

    SLE::pointer feeObject = view().peek(k);

    if (!feeObject)
    {
        feeObject = std::make_shared<SLE>(k);
        view().insert(feeObject);
    }
    auto set = [](SLE::pointer& feeObject, STTx const& tx, auto const& field) {
        feeObject->at(field) = tx[field];
    };
    if (view().rules().enabled(featureXRPFees))
    {
        set(feeObject, ctx_.tx, sfBaseFeeDrops);
        set(feeObject, ctx_.tx, sfReserveBaseDrops);
        set(feeObject, ctx_.tx, sfReserveIncrementDrops);
        // Ensure the old fields are removed
        feeObject->makeFieldAbsent(sfBaseFee);
        feeObject->makeFieldAbsent(sfReferenceFeeUnits);
        feeObject->makeFieldAbsent(sfReserveBase);
        feeObject->makeFieldAbsent(sfReserveIncrement);
    }
    else
    {
        set(feeObject, ctx_.tx, sfBaseFee);
        set(feeObject, ctx_.tx, sfReferenceFeeUnits);
        set(feeObject, ctx_.tx, sfReserveBase);
        set(feeObject, ctx_.tx, sfReserveIncrement);
    }

    view().update(feeObject);

    JLOG(j_.warn()) << "Fees have been changed";
    return tesSUCCESS;
}

TER
Change::applyUNLModify()
{
    if (!isFlagLedger(view().seq()))
    {
        JLOG(j_.warn()) << "N-UNL: applyUNLModify, not a flag ledger, seq="
                        << view().seq();
        return tefFAILURE;
    }

    if (!ctx_.tx.isFieldPresent(sfUNLModifyDisabling) ||
        ctx_.tx.getFieldU8(sfUNLModifyDisabling) > 1 ||
        !ctx_.tx.isFieldPresent(sfLedgerSequence) ||
        !ctx_.tx.isFieldPresent(sfUNLModifyValidator))
    {
        JLOG(j_.warn()) << "N-UNL: applyUNLModify, wrong Tx format.";
        return tefFAILURE;
    }

    bool const disabling = ctx_.tx.getFieldU8(sfUNLModifyDisabling);
    auto const seq = ctx_.tx.getFieldU32(sfLedgerSequence);
    if (seq != view().seq())
    {
        JLOG(j_.warn()) << "N-UNL: applyUNLModify, wrong ledger seq=" << seq;
        return tefFAILURE;
    }

    Blob const validator = ctx_.tx.getFieldVL(sfUNLModifyValidator);
    if (!publicKeyType(makeSlice(validator)))
    {
        JLOG(j_.warn()) << "N-UNL: applyUNLModify, bad validator key";
        return tefFAILURE;
    }

    JLOG(j_.info()) << "N-UNL: applyUNLModify, "
                    << (disabling ? "ToDisable" : "ToReEnable")
                    << " seq=" << seq
                    << " validator data:" << strHex(validator);

    auto const k = keylet::negativeUNL();
    SLE::pointer negUnlObject = view().peek(k);
    if (!negUnlObject)
    {
        negUnlObject = std::make_shared<SLE>(k);
        view().insert(negUnlObject);
    }

    bool const found = [&] {
        if (negUnlObject->isFieldPresent(sfDisabledValidators))
        {
            auto const& negUnl =
                negUnlObject->getFieldArray(sfDisabledValidators);
            for (auto const& v : negUnl)
            {
                if (v.isFieldPresent(sfPublicKey) &&
                    v.getFieldVL(sfPublicKey) == validator)
                    return true;
            }
        }
        return false;
    }();

    if (disabling)
    {
        // cannot have more than one toDisable
        if (negUnlObject->isFieldPresent(sfValidatorToDisable))
        {
            JLOG(j_.warn()) << "N-UNL: applyUNLModify, already has ToDisable";
            return tefFAILURE;
        }

        // cannot be the same as toReEnable
        if (negUnlObject->isFieldPresent(sfValidatorToReEnable))
        {
            if (negUnlObject->getFieldVL(sfValidatorToReEnable) == validator)
            {
                JLOG(j_.warn())
                    << "N-UNL: applyUNLModify, ToDisable is same as ToReEnable";
                return tefFAILURE;
            }
        }

        // cannot be in negative UNL already
        if (found)
        {
            JLOG(j_.warn())
                << "N-UNL: applyUNLModify, ToDisable already in negative UNL";
            return tefFAILURE;
        }

        negUnlObject->setFieldVL(sfValidatorToDisable, validator);
    }
    else
    {
        // cannot have more than one toReEnable
        if (negUnlObject->isFieldPresent(sfValidatorToReEnable))
        {
            JLOG(j_.warn()) << "N-UNL: applyUNLModify, already has ToReEnable";
            return tefFAILURE;
        }

        // cannot be the same as toDisable
        if (negUnlObject->isFieldPresent(sfValidatorToDisable))
        {
            if (negUnlObject->getFieldVL(sfValidatorToDisable) == validator)
            {
                JLOG(j_.warn())
                    << "N-UNL: applyUNLModify, ToReEnable is same as ToDisable";
                return tefFAILURE;
            }
        }

        // must be in negative UNL
        if (!found)
        {
            JLOG(j_.warn())
                << "N-UNL: applyUNLModify, ToReEnable is not in negative UNL";
            return tefFAILURE;
        }

        negUnlObject->setFieldVL(sfValidatorToReEnable, validator);
    }

    view().update(negUnlObject);
    return tesSUCCESS;
}

TER
Change::applyValidatorVote()
{
    // Check if ValidatorVoteTracking amendment is enabled
    if (!view().rules().enabled(featureValidatorVoteTracking))
    {
        return temDISABLED;
    }

    // Get validator public key from transaction
    // Note: This should be the master key (when available), as set by ValidatorVoteTracker
    auto const validatorPubKey = ctx_.tx.getFieldVL(sfValidatorPublicKey);

    // Create an account ID from the validator public key (master key when available)
    // This is used as the key for the ValidatorVoteStats ledger object
    PublicKey pubKey(makeSlice(validatorPubKey));
    AccountID validatorAccount = calcAccountID(pubKey);

    JLOG(j_.info()) << "ValidatorVote: Processing vote with pubKey: "
                    << toBase58(TokenType::NodePublic, pubKey)
                    << " -> Account: " << toBase58(validatorAccount)
                    << " in ledger " << view().seq();
    
    // Get or create the ValidatorVoteStats object for this validator
    auto const k = keylet::validatorVoteStats(validatorAccount);
    SLE::pointer voteStats = view().peek(k);
    
    if (!voteStats)
    {
        JLOG(j_.info()) << "ValidatorVote: Creating new ValidatorVoteStats object";
        // Create new ValidatorVoteStats object
        voteStats = std::make_shared<SLE>(ltVALIDATOR_VOTE_STATS, k.key);
        voteStats->setAccountID(sfAccount, validatorAccount);
        voteStats->setFieldU32(sfVoteCount, 1);
        
        // Add to owner directory
        JLOG(j_.info()) << "ValidatorVote: Adding to owner directory";
        auto const page = view().dirInsert(
            keylet::ownerDir(validatorAccount),
            k.key,
            describeOwnerDir(validatorAccount));
            
        if (!page)
        {
            JLOG(j_.warn()) << "ValidatorVote: Failed to add to directory - tecDIR_FULL";
            return tecDIR_FULL;
        }
            
        JLOG(j_.info()) << "ValidatorVote: Setting fields on new ValidatorVoteStats object";
        voteStats->setFieldU64(sfOwnerNode, *page);
        voteStats->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
        voteStats->setFieldU32(sfPreviousTxnLgrSeq, view().seq());
        
        JLOG(j_.info()) << "ValidatorVote: Inserting new ValidatorVoteStats into view";
        view().insert(voteStats);
    }
    else
    {
        // Update existing ValidatorVoteStats object
        uint32_t currentVotes = voteStats->getFieldU32(sfVoteCount);
        voteStats->setFieldU32(sfVoteCount, currentVotes + 1);
        voteStats->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
        voteStats->setFieldU32(sfPreviousTxnLgrSeq, view().seq());
        
        view().update(voteStats);
    }
    
    // Process exclusion list changes if AccountExclusion is enabled
    if (view().rules().enabled(featureAccountExclusion))
    {
        bool hasExclusionAdd = ctx_.tx.isFieldPresent(sfExclusionAdd);
        bool hasExclusionRemove = ctx_.tx.isFieldPresent(sfExclusionRemove);

        if (hasExclusionAdd || hasExclusionRemove)
        {
            // Get or create the validator's account to update exclusion list
            // Note: ValidatorVote transactions are only created for UNL validators
            auto const validatorAccountKey = keylet::account(validatorAccount);
            SLE::pointer validatorAccountSLE = view().peek(validatorAccountKey);

            if (!validatorAccountSLE)
            {
                JLOG(j_.info()) << "ValidatorVote: Creating validator account for "
                                << toBase58(validatorAccount)
                                << " (from pubKey: " << toBase58(TokenType::NodePublic, pubKey) << ")";

                // Create new account for the validator
                validatorAccountSLE = std::make_shared<SLE>(validatorAccountKey);
                validatorAccountSLE->setAccountID(sfAccount, validatorAccount);
                validatorAccountSLE->setFieldAmount(sfBalance, STAmount{0});

                // Set the correct starting sequence based on DeletableAccounts feature
                std::uint32_t const startingSeq =
                    view().rules().enabled(featureDeletableAccounts) ? view().seq() : 1;
                validatorAccountSLE->setFieldU32(sfSequence, startingSeq);
                validatorAccountSLE->setFieldU32(sfOwnerCount, 0);

                view().insert(validatorAccountSLE);
                JLOG(j_.info()) << "ValidatorVote: Validator account inserted into ledger";
            }
            else
            {
                JLOG(j_.info()) << "ValidatorVote: Validator account already exists: "
                                << toBase58(validatorAccount);
            }

            // Get existing exclusion list or create new one
            STArray exclusionList;
            if (validatorAccountSLE->isFieldPresent(sfExclusionList))
            {
                exclusionList = validatorAccountSLE->getFieldArray(sfExclusionList);
            }

            // Process exclusion removal
            if (hasExclusionRemove)
            {
                auto const toRemove = ctx_.tx.getAccountID(sfExclusionRemove);

                auto newEnd = std::remove_if(
                    exclusionList.begin(),
                    exclusionList.end(),
                    [&toRemove](STObject const& obj) {
                        return obj.isFieldPresent(sfAccount) &&
                               obj.getAccountID(sfAccount) == toRemove;
                    });

                if (newEnd != exclusionList.end())
                {
                    exclusionList.erase(newEnd, exclusionList.end());
                    JLOG(j_.info()) << "ValidatorVote: Removed " << toBase58(toRemove)
                                   << " from validator's exclusion list";
                }
            }

            // Process exclusion addition
            if (hasExclusionAdd)
            {
                auto const toAdd = ctx_.tx.getAccountID(sfExclusionAdd);

                // Check if already in list
                bool alreadyExists = std::any_of(
                    exclusionList.begin(),
                    exclusionList.end(),
                    [&toAdd](STObject const& obj) {
                        return obj.isFieldPresent(sfAccount) &&
                               obj.getAccountID(sfAccount) == toAdd;
                    });

                if (!alreadyExists)
                {
                    // Check max list size (100 addresses)
                    if (exclusionList.size() < 100)
                    {
                        exclusionList.push_back(STObject::makeInnerObject(sfExclusionEntry));
                        STObject& entry = exclusionList.back();
                        entry.setAccountID(sfAccount, toAdd);

                        JLOG(j_.info()) << "ValidatorVote: Added " << toBase58(toAdd)
                                       << " to validator's exclusion list";
                    }
                    else
                    {
                        JLOG(j_.warn()) << "ValidatorVote: Exclusion list full, cannot add "
                                       << toBase58(toAdd);
                    }
                }
            }

            // Update the account with the modified exclusion list
            if (exclusionList.empty())
            {
                validatorAccountSLE->makeFieldAbsent(sfExclusionList);
            }
            else
            {
                validatorAccountSLE->setFieldArray(sfExclusionList, exclusionList);
            }

            view().update(validatorAccountSLE);

            // Update the ExclusionManager cache
            std::unordered_set<AccountID> exclusions;
            for (auto const& entry : exclusionList)
            {
                if (entry.isFieldPresent(sfAccount))
                {
                    exclusions.insert(entry.getAccountID(sfAccount));
                }
            }

            ctx_.app.getExclusionManager().updateValidatorExclusions(
                validatorAccount, exclusions);
        }
    }

    JLOG(j_.trace()) << "ValidatorVote: Successfully recorded vote for validator "
                    << toBase58(validatorAccount);

    return tesSUCCESS;
}

}  // namespace ripple
