# ShieldedPayment Transaction Verification

## Overview

Every transaction in PostFiat goes through a **3-stage verification pipeline** before being included in the ledger. This document explains what `ttSHIELDED_PAYMENT` needs to implement at each stage.

---

## Verification Pipeline

```
Transaction Submitted
       ↓
┌──────────────────────────────┐
│  STAGE 1: PREFLIGHT          │
│  (Static Validation)         │
│                              │
│  ✓ No ledger access          │
│  ✓ Fast validation           │
│  ✓ Can reject early          │
└──────────────────────────────┘
       ↓ (tesSUCCESS)
┌──────────────────────────────┐
│  STAGE 2: PRECLAIM           │
│  (Ledger-Based Validation)   │
│                              │
│  ✓ Read ledger state         │
│  ✓ Check balances            │
│  ✓ Verify no double-spends   │
└──────────────────────────────┘
       ↓ (tesSUCCESS)
┌──────────────────────────────┐
│  STAGE 3: APPLY              │
│  (Execution)                 │
│                              │
│  ✓ Deduct fee                │
│  ✓ Execute transaction       │
│  ✓ Modify ledger             │
└──────────────────────────────┘
       ↓ (tesSUCCESS)
    Ledger Updated
```

---

## Stage 1: Preflight Validation

**Purpose**: Fast, static validation without ledger access

**When**: Transaction is first received by node

**What to Check**:

### 1. Feature Enabled
```cpp
if (!ctx.rules.enabled(featureOrchardPrivacy))
    return temDISABLED;
```

### 2. OrchardBundle Present
```cpp
if (!ctx.tx.isFieldPresent(sfOrchardBundle))
    return temMALFORMED;
```

### 3. Parse Bundle
```cpp
auto bundleData = ctx.tx[sfOrchardBundle];
auto bundle = OrchardBundleWrapper::parse(bundleData);
if (!bundle)
    return temMALFORMED;  // Invalid bundle format
```

### 4. Bundle Structure Valid
```cpp
if (!bundle->isValid())
    return temMALFORMED;  // Invalid internal structure
```

### 5. Field Consistency
```cpp
int64_t valueBalance = bundle->getValueBalance();

// Check field combinations
if (valueBalance < 0) {
    // t→z: Must have sfAmount
    if (!ctx.tx.isFieldPresent(sfAmount))
        return temMALFORMED;

    // Amount must match absolute value of valueBalance
    if (ctx.tx[sfAmount].xrp().drops() != -valueBalance)
        return temBAD_AMOUNT;
}

if (ctx.tx.isFieldPresent(sfDestination)) {
    // z→t: Must have sfAmount and positive valueBalance
    if (!ctx.tx.isFieldPresent(sfAmount))
        return temMALFORMED;

    if (valueBalance <= 0)
        return temMALFORMED;
}
```

### 6. Standard Checks
```cpp
// Call standard preflight checks
if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
    return ret;

return preflight2(ctx);  // Signature validation
```

**Complete Preflight:**
```cpp
NotTEC
ShieldedPayment::preflight(PreflightContext const& ctx)
{
    // Check feature enabled
    if (!ctx.rules.enabled(featureOrchardPrivacy))
        return temDISABLED;

    // Standard checks (account, fee, etc.)
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    // Must have OrchardBundle
    if (!ctx.tx.isFieldPresent(sfOrchardBundle))
        return temMALFORMED;

    // Parse bundle
    auto bundleData = ctx.tx[sfOrchardBundle];
    auto bundle = OrchardBundleWrapper::parse(bundleData);
    if (!bundle)
        return temMALFORMED;

    if (!bundle->isValid())
        return temMALFORMED;

    // Validate field combinations based on value balance
    int64_t valueBalance = bundle->getValueBalance();

    if (valueBalance < 0) {
        // t→z: Must have Amount
        if (!ctx.tx.isFieldPresent(sfAmount))
            return temMALFORMED;

        auto amount = ctx.tx[sfAmount];
        if (!amount.native())
            return temBAD_CURRENCY;

        if (amount.xrp().drops() != -valueBalance)
            return temBAD_AMOUNT;
    }

    if (ctx.tx.isFieldPresent(sfDestination)) {
        // z→t: Must have Amount and positive valueBalance
        if (!ctx.tx.isFieldPresent(sfAmount))
            return temMALFORMED;

        if (valueBalance <= 0)
            return temMALFORMED;

        auto amount = ctx.tx[sfAmount];
        if (!amount.native())
            return temBAD_CURRENCY;
    }

    // Signature validation
    return preflight2(ctx);
}
```

---

## Stage 2: Preclaim Validation

**Purpose**: Ledger-based validation (read-only)

**When**: Before consensus, to determine if transaction will claim a fee

**What to Check**:

### 1. Parse Bundle (Again)
```cpp
auto bundle = OrchardBundleWrapper::parse(ctx.tx[sfOrchardBundle]);
int64_t valueBalance = bundle->getValueBalance();
```

### 2. Check for Double-Spends
```cpp
// Get all nullifiers from bundle
auto nullifiers = bundle->getNullifiers();

for (auto const& nf : nullifiers) {
    // Check if this nullifier already exists in ledger
    if (ctx.view.exists(keylet::orchardNullifier(nf))) {
        return tefORCHARD_DUPLICATE_NULLIFIER;
    }
}
```

### 3. Verify Anchor is Valid
```cpp
// Get the anchor (Merkle tree root)
auto anchor = bundle->getAnchor();

// Check if this anchor exists in recent ledger history
if (!ctx.view.exists(keylet::orchardAnchor(anchor))) {
    return tefORCHARD_INVALID_ANCHOR;
}
```

### 4. Verify Halo2 Proof
```cpp
// Get transaction signing hash
auto sighash = ctx.tx.getSigningHash();

// Verify the zero-knowledge proof
if (!bundle->verifyProof(sighash)) {
    return tefORCHARD_INVALID_PROOF;
}
```

### 5. Check Destination (if z→t)
```cpp
if (ctx.tx.isFieldPresent(sfDestination)) {
    auto const destID = ctx.tx[sfDestination];
    auto const sleDest = ctx.view.read(keylet::account(destID));

    if (!sleDest) {
        // Destination doesn't exist
        // For z→t, we might want to create the account
        // Check if amount is sufficient for reserve
        auto amount = ctx.tx[sfAmount].xrp();
        auto reserve = ctx.view.fees().accountReserve(0);

        if (amount < reserve)
            return tecNO_DST_INSUF_XRP;
    }

    // Check destination tag requirements
    if (sleDest && (sleDest->getFlags() & lsfRequireDestTag)) {
        if (!ctx.tx.isFieldPresent(sfDestinationTag))
            return tecDST_TAG_NEEDED;
    }
}
```

### 6. Check Account Balance for Fees
```cpp
if (valueBalance < 0) {
    // t→z: Account pays both amount and fee
    auto const sleAccount = ctx.view.read(keylet::account(ctx.tx[sfAccount]));
    auto balance = (*sleAccount)[sfBalance].xrp();
    auto amount = ctx.tx[sfAmount].xrp();
    auto fee = ctx.tx[sfFee].xrp();

    if (balance < amount + fee)
        return tecUNFUNDED_PAYMENT;
}
else if (valueBalance < ctx.tx[sfFee].xrp().drops()) {
    // Fee from transparent (valueBalance doesn't cover it)
    auto const sleAccount = ctx.view.read(keylet::account(ctx.tx[sfAccount]));
    auto balance = (*sleAccount)[sfBalance].xrp();
    auto fee = ctx.tx[sfFee].xrp();

    if (balance < fee)
        return tecINSUF_FEE;
}
// else: Fee paid from shielded pool (valueBalance >= fee)
```

**Complete Preclaim:**
```cpp
TER
ShieldedPayment::preclaim(PreclaimContext const& ctx)
{
    // Parse bundle
    auto bundle = OrchardBundleWrapper::parse(ctx.tx[sfOrchardBundle]);
    if (!bundle)
        return tefINTERNAL;  // Should not happen (validated in preflight)

    int64_t valueBalance = bundle->getValueBalance();

    // Check for double-spends
    auto nullifiers = bundle->getNullifiers();
    for (auto const& nf : nullifiers) {
        if (ctx.view.exists(keylet::orchardNullifier(nf))) {
            return tefORCHARD_DUPLICATE_NULLIFIER;
        }
    }

    // Verify anchor
    auto anchor = bundle->getAnchor();
    if (!ctx.view.exists(keylet::orchardAnchor(anchor))) {
        return tefORCHARD_INVALID_ANCHOR;
    }

    // Verify proof (expensive, but necessary)
    auto sighash = ctx.tx.getSigningHash();
    if (!bundle->verifyProof(sighash)) {
        return tefORCHARD_INVALID_PROOF;
    }

    // Check destination (if z→t)
    if (ctx.tx.isFieldPresent(sfDestination)) {
        auto const destID = ctx.tx[sfDestination];
        auto const sleDest = ctx.view.read(keylet::account(destID));

        if (!sleDest) {
            auto amount = ctx.tx[sfAmount].xrp();
            auto reserve = ctx.view.fees().accountReserve(0);

            if (amount < reserve)
                return tecNO_DST_INSUF_XRP;
        }

        if (sleDest && (sleDest->getFlags() & lsfRequireDestTag)) {
            if (!ctx.tx.isFieldPresent(sfDestinationTag))
                return tecDST_TAG_NEEDED;
        }
    }

    // Check account balance
    auto const sleAccount = ctx.view.read(keylet::account(ctx.tx[sfAccount]));
    if (!sleAccount)
        return terNO_ACCOUNT;

    auto balance = (*sleAccount)[sfBalance].xrp();
    auto fee = ctx.tx[sfFee].xrp();

    if (valueBalance < 0) {
        // t→z: Account pays amount + fee
        auto amount = ctx.tx[sfAmount].xrp();
        if (balance < amount + fee)
            return tecUNFUNDED_PAYMENT;
    }
    else if (valueBalance < fee.drops()) {
        // Partial fee from transparent
        if (balance < fee)
            return tecINSUF_FEE;
    }
    // else: Fee fully paid from shielded

    return tesSUCCESS;
}
```

---

## Stage 3: Apply (Execution)

**Purpose**: Execute transaction and modify ledger state

**When**: During consensus, transaction is being applied to ledger

**What to Do**:

### 1. Parse Bundle
```cpp
auto bundle = OrchardBundleWrapper::parse(ctx_.tx[sfOrchardBundle]);
int64_t valueBalance = bundle->getValueBalance();
XRPAmount fee = ctx_.tx[sfFee].xrp();
```

### 2. Handle Transparent Input (t→z)
```cpp
if (valueBalance < 0) {
    // Deduct amount from account (going to shielded pool)
    XRPAmount amount = ctx_.tx[sfAmount].xrp();

    auto sleAccount = view().peek(keylet::account(account_));
    if (!sleAccount)
        return tefINTERNAL;

    (*sleAccount)[sfBalance] = (*sleAccount)[sfBalance] - amount;
    view().update(sleAccount);
}
```

### 3. Handle Transparent Output (z→t)
```cpp
if (ctx_.tx.isFieldPresent(sfDestination)) {
    // Credit destination account (from shielded pool)
    XRPAmount amount = ctx_.tx[sfAmount].xrp();
    AccountID const destID = ctx_.tx[sfDestination];

    auto sleDest = view().peek(keylet::account(destID));

    if (!sleDest) {
        // Create destination account
        sleDest = std::make_shared<SLE>(keylet::account(destID));
        (*sleDest)[sfBalance] = amount;
        (*sleDest)[sfAccount] = destID;
        (*sleDest)[sfSequence] = view().seq();
        view().insert(sleDest);
    }
    else {
        // Update existing account
        (*sleDest)[sfBalance] = (*sleDest)[sfBalance] + amount;
        view().update(sleDest);
    }
}
```

### 4. Handle Fee Payment
```cpp
// Fee payment logic
if (valueBalance >= fee.drops()) {
    // Fee paid from shielded pool (included in valueBalance)
    // Don't deduct from account balance
}
else {
    // Fee paid from transparent account
    auto sleAccount = view().peek(keylet::account(account_));
    (*sleAccount)[sfBalance] = (*sleAccount)[sfBalance] - fee;
    view().update(sleAccount);
}
```

### 5. Store Nullifiers
```cpp
// Mark nullifiers as spent (prevent double-spend)
auto nullifiers = bundle->getNullifiers();
for (auto const& nf : nullifiers) {
    auto sleNullifier = std::make_shared<SLE>(keylet::orchardNullifier(nf));
    // Store minimal data (just mark as spent)
    view().insert(sleNullifier);
}
```

### 6. Update Anchor (Optional)
```cpp
// If we're maintaining commitment tree state on-ledger
auto anchor = bundle->getAnchor();
auto sleAnchor = view().peek(keylet::orchardAnchor(anchor));
if (!sleAnchor) {
    sleAnchor = std::make_shared<SLE>(keylet::orchardAnchor(anchor));
    // Store anchor with ledger sequence
    (*sleAnchor)[sfLedgerSequence] = view().seq();
    view().insert(sleAnchor);
}
```

**Complete doApply:**
```cpp
TER
ShieldedPayment::doApply()
{
    // Parse bundle
    auto bundle = OrchardBundleWrapper::parse(ctx_.tx[sfOrchardBundle]);
    if (!bundle)
        return tefINTERNAL;

    int64_t valueBalance = bundle->getValueBalance();
    XRPAmount fee = ctx_.tx[sfFee].xrp();

    // Handle transparent input (t→z)
    if (valueBalance < 0) {
        XRPAmount amount = ctx_.tx[sfAmount].xrp();
        auto sleAccount = view().peek(keylet::account(account_));

        (*sleAccount)[sfBalance] = (*sleAccount)[sfBalance] - amount;
        view().update(sleAccount);
    }

    // Handle transparent output (z→t)
    if (ctx_.tx.isFieldPresent(sfDestination)) {
        XRPAmount amount = ctx_.tx[sfAmount].xrp();
        AccountID const destID = ctx_.tx[sfDestination];

        auto sleDest = view().peek(keylet::account(destID));

        if (!sleDest) {
            // Create account
            sleDest = std::make_shared<SLE>(keylet::account(destID));
            (*sleDest)[sfBalance] = amount;
            (*sleDest)[sfAccount] = destID;
            (*sleDest)[sfSequence] = view().seq();
            view().insert(sleDest);
        }
        else {
            (*sleDest)[sfBalance] = (*sleDest)[sfBalance] + amount;
            view().update(sleDest);
        }
    }

    // Handle fee payment
    if (valueBalance < fee.drops()) {
        // Fee from transparent (partial or full)
        XRPAmount transparentFee = fee;
        if (valueBalance > 0) {
            // Partial fee from shielded
            transparentFee = fee - XRPAmount(valueBalance);
        }

        auto sleAccount = view().peek(keylet::account(account_));
        (*sleAccount)[sfBalance] = (*sleAccount)[sfBalance] - transparentFee;
        view().update(sleAccount);
    }
    // else: Fee fully paid from shielded (valueBalance >= fee)

    // Store nullifiers
    auto nullifiers = bundle->getNullifiers();
    for (auto const& nf : nullifiers) {
        auto sleNullifier = std::make_shared<SLE>(keylet::orchardNullifier(nf));
        view().insert(sleNullifier);
    }

    return tesSUCCESS;
}
```

---

## Error Codes to Use

### Preflight Errors (tem*)
- `temDISABLED` - OrchardPrivacy feature not enabled
- `temMALFORMED` - Invalid transaction structure
- `temBAD_AMOUNT` - Invalid amount value
- `temBAD_CURRENCY` - Non-XRP amount (we only support XRP)

### Preclaim Errors (tef*)
- `tefORCHARD_DUPLICATE_NULLIFIER` - Double-spend detected
- `tefORCHARD_INVALID_ANCHOR` - Invalid Merkle tree anchor
- `tefORCHARD_INVALID_PROOF` - Zero-knowledge proof verification failed
- `tefINTERNAL` - Internal error (shouldn't happen)

### Apply Errors (tec*)
- `tecUNFUNDED_PAYMENT` - Insufficient balance for payment
- `tecINSUF_FEE` - Insufficient balance for fee
- `tecNO_DST` - Destination doesn't exist
- `tecNO_DST_INSUF_XRP` - Destination creation underfunded
- `tecDST_TAG_NEEDED` - Destination requires tag

### Need to Define (in TER.h)
```cpp
// Add to TER.h:
tefORCHARD_DUPLICATE_NULLIFIER,
tefORCHARD_INVALID_ANCHOR,
tefORCHARD_INVALID_PROOF,
```

---

## Implementation Checklist

### Files to Create
- [ ] `src/xrpld/app/tx/detail/ShieldedPayment.h`
- [ ] `src/xrpld/app/tx/detail/ShieldedPayment.cpp`

### Files to Modify
- [ ] `include/xrpl/protocol/TER.h` - Add new error codes
- [ ] `src/xrpld/app/tx/impl/details.cpp` - Add error descriptions
- [ ] `include/xrpl/protocol/Keylet.h` - Add keylet functions
- [ ] `src/libxrpl/protocol/Keylet.cpp` - Implement keylet functions

### Implementation Order
1. Define error codes in TER.h
2. Define keylet functions for nullifiers and anchors
3. Create ShieldedPayment.h with method signatures
4. Implement preflight() - static validation
5. Implement preclaim() - ledger validation
6. Implement doApply() - execution
7. Write tests

---

## Testing Strategy

### Unit Tests
```cpp
// Test preflight validation
TEST_F(ShieldedPayment_test, preflight_missing_bundle)
TEST_F(ShieldedPayment_test, preflight_invalid_bundle)
TEST_F(ShieldedPayment_test, preflight_mismatched_amount)

// Test preclaim validation
TEST_F(ShieldedPayment_test, preclaim_duplicate_nullifier)
TEST_F(ShieldedPayment_test, preclaim_invalid_anchor)
TEST_F(ShieldedPayment_test, preclaim_invalid_proof)

// Test apply execution
TEST_F(ShieldedPayment_test, apply_transparent_to_shielded)
TEST_F(ShieldedPayment_test, apply_shielded_to_shielded)
TEST_F(ShieldedPayment_test, apply_shielded_to_transparent)
TEST_F(ShieldedPayment_test, apply_fee_from_shielded)
```

---

## Performance Considerations

### Proof Verification
- **Most expensive operation**: Halo2 proof verification
- **Where**: Should be in preclaim() to reject invalid transactions early
- **Optimization**: Use batch verification for blocks

### Nullifier Lookups
- **Frequency**: Every shielded spend
- **Solution**: Efficient ledger object indexing
- **Consider**: Bloom filters for quick rejection

### Anchor Validation
- **History**: Need to track ~200 recent anchors
- **Pruning**: Remove old anchors after expiry
- **Storage**: Minimal (just 32-byte hash per anchor)

---

## Summary

The `ttSHIELDED_PAYMENT` transaction requires:

1. **Preflight**: Validate bundle structure and field consistency
2. **Preclaim**: Check double-spends, verify proof, validate balances
3. **Apply**: Execute transfer, store nullifiers, handle fees

The most critical security check is **nullifier verification** in preclaim to prevent double-spending of shielded notes.

The most expensive operation is **proof verification** (~1-2 seconds), which should use batch verification for blocks.

Next step: Implement these three methods in `ShieldedPayment.{h,cpp}`!
