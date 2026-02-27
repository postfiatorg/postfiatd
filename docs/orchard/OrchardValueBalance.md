# Orchard Value Balance System

## Overview

PostFiat implements Zcash's **value balance** model for Orchard shielded transactions. This elegant system uses a single number (`valueBalance`) to represent the net flow of funds between transparent and shielded pools.

## The Value Balance Concept

### What is Value Balance?

The `OrchardBundle` contains a field called `valueBalance` (in drops):

```cpp
int64_t valueBalance = bundle.getValueBalance();
```

This number represents:
- **Negative** (`< 0`): Value flowing FROM transparent TO shielded pool
- **Positive** (`> 0`): Value flowing FROM shielded TO transparent pool
- **Zero** (`= 0`): Fully shielded transaction (no transparent interaction)

### Why This Is Brilliant

With just this one number, we can:
1. ✅ Track value flow between pools
2. ✅ Pay fees from either transparent or shielded funds
3. ✅ Support all transaction types (t→z, z→z, z→t)
4. ✅ Validate balance with simple arithmetic

## Transaction Types

### Type 1: Transparent to Shielded (t→z)

**Goal**: Shield 100 XRP from transparent account into privacy pool

```json
{
  "TransactionType": "ShieldedPayment",
  "Account": "rALICE...",
  "Fee": "10",
  "Amount": "100000000",  // 100 XRP in drops
  "OrchardBundle": "<serialized bundle>"
}
```

**OrchardBundle contains:**
- `valueBalance`: **-100 XRP** (negative = inflow to shielded)
- Actions: 1 output (creates encrypted note for 100 XRP)

**Balance Equation:**
```
Account provides: 100 + 0.00001 XRP (amount + fee)
Shielded receives: 100 XRP (via negative valueBalance)
Fee burned: 0.00001 XRP

Account balance - 100 XRP - 0.00001 XRP ✓
```

**Value Flow:**
```
┌─────────────┐
│ rALICE      │ -100 XRP
│ (transparent)│ -0.00001 XRP (fee)
└──────┬──────┘
       │ valueBalance = -100
       ▼
┌─────────────┐
│ Shielded    │ +100 XRP
│ Pool        │ (encrypted note)
└─────────────┘
```

---

### Type 2: Shielded to Shielded (z→z)

**Goal**: Send 50 XRP privately from Alice's shielded balance to Bob

#### Option A: Fee from Transparent Account

```json
{
  "TransactionType": "ShieldedPayment",
  "Account": "rALICE...",
  "Fee": "10",
  "OrchardBundle": "<serialized bundle>"
}
```

**OrchardBundle contains:**
- `valueBalance`: **0** (zero = fully shielded)
- Actions:
  - 1 spend (consumes Alice's 50 XRP note)
  - 1 output (creates 50 XRP note for Bob)

**Balance Equation:**
```
Account provides: 0.00001 XRP (just the fee)
Shielded: 50 XRP spent → 50 XRP created (net zero)
Fee burned: 0.00001 XRP

Account balance - 0.00001 XRP ✓
Alice shielded: -50 XRP
Bob shielded: +50 XRP
```

**Value Flow:**
```
┌─────────────┐
│ rALICE      │ -0.00001 XRP (fee only)
│ (transparent)│
└─────────────┘

┌─────────────┐  valueBalance = 0
│ Shielded    │  (fully shielded)
│ Pool        │
│             │
│ Alice: -50  │ ────────────► Bob: +50
│  (encrypted)│               (encrypted)
└─────────────┘
```

#### Option B: Fee from Shielded Pool

```json
{
  "TransactionType": "ShieldedPayment",
  "Account": "rALICE...",
  "Fee": "10",
  "OrchardBundle": "<serialized bundle>"
}
```

**OrchardBundle contains:**
- `valueBalance`: **+0.00001 XRP** (positive = small outflow for fee)
- Actions:
  - 1 spend (consumes Alice's 50 XRP note)
  - 1 output (creates 49.99999 XRP note for Bob)

**Balance Equation:**
```
Shielded provides: 50 XRP (from spend)
Shielded outputs: 49.99999 XRP (to Bob)
Value balance: +0.00001 XRP (burned as fee)
Fee burned: 0.00001 XRP

50 = 49.99999 + 0.00001 ✓
```

**Value Flow:**
```
┌─────────────┐
│ Shielded    │  valueBalance = +0.00001
│ Pool        │  (tiny outflow for fee)
│             │
│ Alice: -50  │ ──► Bob: +49.99999
│  (encrypted)│     (encrypted)
└──────┬──────┘
       │ +0.00001 XRP
       ▼
    [FEE BURNED]
```

---

### Type 3: Shielded to Transparent (z→t)

**Goal**: Unshield 100 XRP to transparent recipient

```json
{
  "TransactionType": "ShieldedPayment",
  "Account": "rALICE...",
  "Destination": "rBOB...",
  "Amount": "99999990",  // 99.99999 XRP (100 - fee)
  "Fee": "10",
  "OrchardBundle": "<serialized bundle>"
}
```

**OrchardBundle contains:**
- `valueBalance`: **+100 XRP** (positive = outflow from shielded)
- Actions:
  - 1 spend (consumes 100 XRP shielded note)

**Balance Equation:**
```
Shielded provides: 100 XRP (via positive valueBalance)
Destination receives: 99.99999 XRP
Fee burned: 0.00001 XRP

100 = 99.99999 + 0.00001 ✓
```

**Value Flow:**
```
┌─────────────┐
│ Shielded    │  valueBalance = +100
│ Pool        │  (outflow to transparent)
│             │
│ Alice: -100 │
│  (encrypted)│
└──────┬──────┘
       │ +100 XRP
       ▼
┌─────────────┐
│ rBOB        │ +99.99999 XRP
│ (transparent)│
└─────────────┘
       │
       ▼
    [FEE: 0.00001 XRP]
```

---

## Transaction Validation

### Balance Equation

For any `ttSHIELDED_PAYMENT` transaction:

```cpp
// Inputs (sources of value)
CAmount transparentIn = 0;
if (tx.isFieldPresent(sfAmount) && valueBalance < 0) {
    // t→z: transparent amount going into shielded
    transparentIn = tx[sfAmount].xrp().drops();
}
if (valueBalance > 0) {
    // z→t or fee from shielded: value coming out of shielded
    transparentIn += valueBalance;
}

// Outputs (uses of value)
CAmount transparentOut = 0;
if (tx.isFieldPresent(sfDestination) && tx.isFieldPresent(sfAmount)) {
    // z→t: sending to transparent recipient
    transparentOut = tx[sfAmount].xrp().drops();
}
if (valueBalance < 0) {
    // t→z: value going into shielded pool
    transparentOut += -valueBalance;  // Absolute value
}
transparentOut += tx[sfFee].xrp().drops();  // Always paid

// Validation
if (transparentIn < transparentOut) {
    return tefINSUFFICIENT_FUNDS;
}
```

### Fee Payment Rules

#### Rule 1: Transparent Fee Payment
```cpp
// Traditional: Account pays fee from transparent balance
if (valueBalance <= 0) {
    // No shielded outflow, so fee must come from account
    if (account.balance < fee) {
        return tefINSUFFICIENT_FUNDS;
    }
}
```

#### Rule 2: Shielded Fee Payment
```cpp
// Advanced: Fee included in positive valueBalance
if (valueBalance > 0) {
    // Shielded funds flowing out can cover fee
    if (valueBalance >= fee) {
        // Fee can be paid from shielded pool
        // Account balance not debited for fee
    }
}
```

---

## Implementation in C++

### Preflight Validation

```cpp
NotTEC
ShieldedPayment::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureOrchardPrivacy))
        return temDISABLED;

    if (!ctx.tx.isFieldPresent(sfOrchardBundle))
        return temMALFORMED;

    // Parse and validate bundle structure
    auto bundleData = ctx.tx[sfOrchardBundle];
    auto bundle = OrchardBundleWrapper::parse(bundleData);
    if (!bundle)
        return temMALFORMED;

    if (!bundle->isValid())
        return temMALFORMED;

    return preflight2(ctx);
}
```

### Preclaim Validation

```cpp
TER
ShieldedPayment::preclaim(PreclaimContext const& ctx)
{
    auto bundle = OrchardBundleWrapper::parse(ctx.tx[sfOrchardBundle]);

    // Get value balance
    int64_t valueBalance = bundle->getValueBalance();

    // Check nullifiers for double-spends
    if (valueBalance != 0) {  // Has shielded spends or outputs
        auto nullifiers = bundle->getNullifiers();
        for (auto const& nf : nullifiers) {
            if (ctx.view.exists(keylet::orchardNullifier(nf))) {
                return tefORCHARD_DUPLICATE_NULLIFIER;
            }
        }

        // Check anchor is valid
        auto anchor = bundle->getAnchor();
        if (!ctx.view.exists(keylet::orchardAnchor(anchor))) {
            return tefORCHARD_INVALID_ANCHOR;
        }
    }

    // Verify proof
    auto sighash = ctx.tx.getSigningHash();
    if (!bundle->verifyProof(sighash)) {
        return tefORCHARD_INVALID_PROOF;
    }

    return tesSUCCESS;
}
```

### DoApply Implementation

```cpp
TER
ShieldedPayment::doApply()
{
    auto bundle = OrchardBundleWrapper::parse(ctx_.tx[sfOrchardBundle]);
    int64_t valueBalance = bundle->getValueBalance();
    XRPAmount fee = ctx_.tx[sfFee].xrp();

    // Handle transparent input/output
    if (valueBalance < 0) {
        // t→z: Deduct amount from account (going to shielded)
        XRPAmount amount = ctx_.tx[sfAmount].xrp();
        auto sleAccount = view().peek(keylet::account(account_));
        (*sleAccount)[sfBalance] -= amount;
    }

    if (ctx_.tx.isFieldPresent(sfDestination)) {
        // z→t: Credit destination (coming from shielded)
        XRPAmount amount = ctx_.tx[sfAmount].xrp();
        auto sleDest = view().peek(keylet::account(ctx_.tx[sfDestination]));
        (*sleDest)[sfBalance] += amount;
    }

    // Handle fee payment
    if (valueBalance >= fee.drops()) {
        // Fee paid from shielded pool (included in valueBalance)
        // Don't deduct from account
    } else {
        // Fee paid from transparent account
        auto sleAccount = view().peek(keylet::account(account_));
        (*sleAccount)[sfBalance] -= fee;
    }

    // Store nullifiers to prevent double-spends
    auto nullifiers = bundle->getNullifiers();
    for (auto const& nf : nullifiers) {
        auto sleNullifier = std::make_shared<SLE>(keylet::orchardNullifier(nf));
        view().insert(sleNullifier);
    }

    return tesSUCCESS;
}
```

---

## Examples with Actual Numbers

### Example 1: Shield 100 XRP (t→z)

```json
{
  "Account": "rALICE",
  "AccountBalance": "1000 XRP",
  "Fee": "0.00001 XRP",
  "Amount": "100 XRP",
  "OrchardBundle": {
    "valueBalance": "-100 XRP",
    "actions": [
      {"type": "output", "value": "100 XRP", "recipient": "<encrypted>"}
    ]
  }
}
```

**After execution:**
- Account balance: `1000 - 100 - 0.00001 = 899.99999 XRP`
- Shielded pool: `+100 XRP`

---

### Example 2: Private Transfer 50 XRP (z→z, fee from shielded)

```json
{
  "Account": "rALICE",
  "AccountBalance": "0.1 XRP",  // Just for sequence numbers
  "Fee": "0.00001 XRP",
  "OrchardBundle": {
    "valueBalance": "+0.00001 XRP",  // Just the fee
    "actions": [
      {"type": "spend", "value": "50 XRP"},
      {"type": "output", "value": "49.99999 XRP", "recipient": "<encrypted>"}
    ]
  }
}
```

**After execution:**
- Account balance: `0.1 XRP` (unchanged! Fee paid from shielded)
- Alice shielded: `-50 XRP`
- Bob shielded: `+49.99999 XRP`
- Fee: `0.00001 XRP` (from valueBalance)

---

### Example 3: Unshield 200 XRP (z→t)

```json
{
  "Account": "rALICE",
  "AccountBalance": "1 XRP",
  "Destination": "rBOB",
  "Amount": "199.99999 XRP",
  "Fee": "0.00001 XRP",
  "OrchardBundle": {
    "valueBalance": "+200 XRP",  // Outflow from shielded
    "actions": [
      {"type": "spend", "value": "200 XRP"}
    ]
  }
}
```

**After execution:**
- Account balance: `1 XRP` (unchanged! Fee paid from shielded)
- Bob balance: `+199.99999 XRP`
- Alice shielded: `-200 XRP`
- Fee: `0.00001 XRP` (from valueBalance)

---

## Key Insights

### 1. Value Balance Is Self-Balancing

The bundle's `valueBalance` automatically ensures conservation of value:
```
Sum of spends = Sum of outputs + |valueBalance|
```

### 2. Fees Can Come From Either Pool

- **Transparent**: Account balance reduced by fee (traditional)
- **Shielded**: Positive valueBalance includes fee (advanced)

### 3. No Special Fields Needed

We don't need extra fields to specify "pay fee from shielded" - it's implicit in the valueBalance!

### 4. Simple Arithmetic Validation

All validation is just checking:
```
Inputs ≥ Outputs + Fees
```

---

## Security Considerations

1. **Nullifier Uniqueness**: Each nullifier can only be used once (prevents double-spend)
2. **Anchor Validity**: Anchor must exist in recent ledger history
3. **Proof Verification**: Halo2 proof must verify correctly
4. **Value Balance Limits**: `|valueBalance| ≤ MAX_MONEY`
5. **Conservation**: Transparent in + shielded in = transparent out + shielded out + fee

---

## References

- **Zcash Value Balance**: `zcash/src/primitives/transaction.cpp`
- **ZIP-317 Fees**: https://zips.z.cash/zip-0317
- **PostFiat Implementation**: `src/xrpld/app/tx/detail/ShieldedPayment.{h,cpp}`
