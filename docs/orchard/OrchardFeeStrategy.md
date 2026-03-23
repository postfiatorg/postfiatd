# Orchard Privacy - Fee Payment Strategy (Zcash Analysis)

## Summary

After analyzing the Zcash implementation, here's how fees actually work with Orchard shielded transactions:

**Key Finding**: Zcash uses **value balancing** - fees can be paid from EITHER transparent inputs OR the shielded pool, depending on the transaction type.

## Zcash Fee Payment Model

### How It Works

In Zcash, a transaction has multiple components:
```cpp
class CTransaction {
    std::vector<CTxIn> vin;           // Transparent inputs
    std::vector<CTxOut> vout;         // Transparent outputs
    SaplingBundle saplingBundle;       // Sapling shielded operations
    OrchardBundle orchardBundle;       // Orchard shielded operations
};
```

**Fee Calculation** (from ZIP-317):
```cpp
Fee = MARGINAL_FEE × max(GRACE_ACTIONS, logicalActionCount)

Where:
  MARGINAL_FEE = 5000 zatoshis (0.00005 ZEC)
  GRACE_ACTIONS = 2
  logicalActionCount = transparent_actions + max(sapling_spends, sapling_outputs) + orchard_actions
```

### Value Balance Formula

The **critical insight** is how Zcash balances value:

```
Input Value = Output Value + Fees

Where Input Value includes:
  - Transparent inputs (vin)
  - Shielded spends via value balance

And Output Value includes:
  - Transparent outputs (vout)
  - Shielded outputs via value balance
```

#### Value Balance Explained

Each shielded bundle has a `valueBalance`:

**Orchard Bundle Value Balance:**
- **Negative** (`-X`): Value flowing INTO shielded pool (transparent → shielded)
- **Positive** (`+X`): Value flowing OUT OF shielded pool (shielded → transparent)
- **Zero** (`0`): Fully shielded (z→z), no transparent interaction

From `CTransaction::GetValueOut()`:
```cpp
CAmount nValueOut = 0;

// Add transparent outputs
for (const auto& out : vout) {
    nValueOut += out.nValue;
}

// If valueBalanceOrchard is NEGATIVE, it "takes" money from transparent pool
auto valueBalanceOrchard = orchardBundle.GetValueBalance();
if (valueBalanceOrchard <= 0) {
    nValueOut += -valueBalanceOrchard;  // Add absolute value
}
```

### Transaction Types & Fee Payment

#### 1. Transparent to Shielded (t→z)

```
Transparent Inputs: 1000 ZEC
Transparent Outputs: 0 ZEC
Orchard Value Balance: -999.99995 ZEC (negative = inflow)
Fee: 0.00005 ZEC

Balance:
  Inputs:  1000 ZEC (transparent)
  Outputs: 0 ZEC (transparent) + 999.99995 ZEC (to shielded)
  Fee:     0.00005 ZEC

  1000 = 0 + 999.99995 + 0.00005 ✓
```

**Fee paid from**: Transparent inputs (the 1000 ZEC)

---

#### 2. Shielded to Shielded (z→z)

This is where it gets interesting! Zcash has **TWO options**:

**Option A: With Transparent Input for Fee Only**
```
Transparent Inputs: 0.00005 ZEC
Transparent Outputs: 0 ZEC
Orchard Value Balance: 0 ZEC (zero = fully shielded)
Fee: 0.00005 ZEC

Orchard Actions:
  - Spend: 500 ZEC (from shielded note)
  - Output: 500 ZEC (to recipient, encrypted)

Balance:
  Inputs:  0.00005 ZEC (transparent) + 500 ZEC (shielded spend)
  Outputs: 0 ZEC (transparent) + 500 ZEC (shielded output)
  Fee:     0.00005 ZEC

  500.00005 = 0 + 500 + 0.00005 ✓
```

**Fee paid from**: Small transparent input (user maintains fee-only account)

---

**Option B: Fee from Shielded Pool**
```
Transparent Inputs: 0 ZEC
Transparent Outputs: 0 ZEC
Orchard Value Balance: +0.00005 ZEC (positive = small outflow for fee)
Fee: 0.00005 ZEC

Orchard Actions:
  - Spend: 500 ZEC (from shielded note)
  - Output: 499.99995 ZEC (to recipient, slightly less)

Balance:
  Inputs:  500 ZEC (shielded spend)
  Outputs: 499.99995 ZEC (shielded output) + 0.00005 ZEC (fee via value balance)
  Fee:     0.00005 ZEC

  500 = 499.99995 + 0.00005 ✓
```

**Fee paid from**: Shielded pool (via positive value balance that's burned)

---

#### 3. Shielded to Transparent (z→t)

```
Transparent Inputs: 0 ZEC
Transparent Outputs: 499.99995 ZEC (to recipient)
Orchard Value Balance: +500 ZEC (positive = outflow from shielded)
Fee: 0.00005 ZEC

Orchard Actions:
  - Spend: 500 ZEC (from shielded note)

Balance:
  Inputs:  500 ZEC (shielded spend via positive value balance)
  Outputs: 499.99995 ZEC (transparent)
  Fee:     0.00005 ZEC

  500 = 499.99995 + 0.00005 ✓
```

**Fee paid from**: Shielded pool (included in the +500 ZEC value balance)

---

## Zcash Transaction Builder Logic

From `TransactionBuilder::Build()`:

```cpp
// Calculate change
CAmount change = valueBalanceSapling + valueBalanceOrchard - fee;

for (auto tIn : tIns) {
    change += tIn.nValue;  // Add transparent inputs
}

for (auto tOut : mtx.vout) {
    change -= tOut.nValue;  // Subtract transparent outputs
}

// If change is positive, create a change output (transparent or shielded)
if (change > 0) {
    if (orchardChangeAddr) {
        AddOrchardOutput(..., change, ...);  // Shielded change
    } else if (tChangeAddr) {
        AddTransparentOutput(..., change);   // Transparent change
    }
}
```

**Key Point**: The fee is **always subtracted from the total available value**, regardless of whether it comes from transparent or shielded sources!

## Implementation Strategy for PostFiat

Based on this analysis, I recommend a **hybrid approach**:

### Recommended: Value Balance Model (Like Zcash)

```cpp
// ShieldedPayment transaction
TRANSACTION(ttSHIELDED_PAYMENT, 72, ShieldedPayment, Delegation::notDelegatable, ({
    {sfDestination, soeOPTIONAL},    // For z→t
    {sfAmount, soeOPTIONAL},         // For t→z or z→t
    {sfOrchardBundle, soeOPTIONAL},  // The shielded operations
}))

// Common fields (all transactions):
// - sfAccount (OPTIONAL for shielded-only transactions!)
// - sfFee (REQUIRED)
// - sfSequence (REQUIRED)
```

### Fee Payment Options

#### Option 1: Transparent Fee Account (Simpler to Start)

**Phase 1 Implementation**:
```cpp
// Require sfAccount field (transparent account pays fees)
if (!tx.isFieldPresent(sfAccount)) {
    return temMALFORMED;
}

// Fee deducted from account's transparent balance
XRPAmount fee = tx[sfFee];
```

**Advantages**:
- ✅ Simpler to implement initially
- ✅ Reuses existing account infrastructure
- ✅ No changes to fee validation logic
- ✅ Spam prevention via account reserves

**Usage**:
- t→z: Account pays fee from transparent balance
- z→z: Account pays small fee (keeps minimal transparent balance)
- z→t: Account pays fee from transparent balance

---

#### Option 2: Value Balance Fees (Future Enhancement)

**Phase 2+ Enhancement**:
```cpp
// Calculate net value flow
CAmount transparentIn = getTransparentInputValue(account);
CAmount transparentOut = 0;
if (tx.isFieldPresent(sfDestination) && tx.isFieldPresent(sfAmount)) {
    transparentOut = tx[sfAmount].xrp().drops();
}

// Get Orchard value balance
auto bundle = parseOrchardBundle(tx[sfOrchardBundle]);
CAmount orchardValueBalance = bundle.getValueBalance();

// Fee can come from either:
// - Transparent (if no bundle or negative value balance)
// - Shielded (if positive value balance includes fee)
CAmount totalIn = transparentIn;
if (orchardValueBalance > 0) {
    totalIn += orchardValueBalance;  // Shielded → transparent
}

CAmount totalOut = transparentOut + fee;
if (orchardValueBalance < 0) {
    totalOut += -orchardValueBalance;  // Transparent → shielded
}

if (totalIn < totalOut) {
    return tefINSUFFICIENT_FUNDS;
}
```

**Advantages**:
- ✅ Fully shielded accounts can operate independently
- ✅ Better privacy (no correlation with fee-paying account)
- ✅ More flexible for users

**Challenges**:
- ❌ Needs sequence number mechanism for shielded-only transactions
- ❌ More complex validation logic
- ❌ Spam prevention without accounts needs new approach

---

## Recommended Implementation Plan

### Phase 1: Transparent Fee Payment (Current)
```cpp
// All shielded transactions require transparent account
// Fee paid from account balance
// Simple and proven
```

**Files to modify**:
- `ShieldedPayment::preflight()` - Require sfAccount
- `ShieldedPayment::preclaim()` - Check sufficient balance for fee
- `ShieldedPayment::doApply()` - Deduct fee from account

### Phase 2: Add Value Balance Fee Support

**Add to interface**:
```cpp
// Allow fee to be paid from shielded pool
// Still require account for sequence numbers
// But balance can be zero if value balance covers fee
```

### Phase 3: Full Shielded Accounts (Advanced)

**New mechanism**:
```cpp
// Shielded sequence numbers (tracked via nullifiers)
// Account-less transactions (if community desires)
// New spam prevention (proof-of-work or staked nullifiers)
```

---

## Comparison: XRP vs Zcash

| Aspect | Zcash | PostFiat/XRP (Recommended) |
|--------|-------|---------------------------|
| **Phase 1** | Transparent fees | Transparent fees (same) |
| **UTXO vs Account** | UTXO model | Account model |
| **Spam Prevention** | Transaction fees | Account reserves + fees |
| **Sequence Numbers** | UTXO inputs | Account sequence |
| **Fee Source** | Any input (transparent or shielded via value balance) | Transparent account (initially) |
| **Future** | Already supports shielded fees | Can add value balance fees later |

---

## Key Insight: Value Balance Is The Secret

The Zcash approach is elegant:

```
valueBalanceOrchard represents the NET FLOW of value between pools:

  Negative: transparent → shielded (money going IN to privacy)
  Positive: shielded → transparent (money coming OUT for fees/payments)
  Zero:     fully shielded (no transparent interaction)
```

This single number handles:
- ✅ t→z shielding
- ✅ z→t unshielding
- ✅ Fee payment from shielded pool
- ✅ All validated with simple arithmetic

---

## Conclusion

**Start with Option 1** (Transparent fee account):
- Simple to implement
- Proven by Zcash initially
- Works for all transaction types
- Can enhance later

**Future: Add Option 2** (Value balance fees):
- Better privacy for fully shielded users
- More flexible
- Still compatible with account model (use sequence numbers)

The value balance model is brilliant and we should adopt it, but starting with transparent fee accounts is the pragmatic first step!

---

## References

- **ZIP-317**: https://zips.z.cash/zip-0317 (Zcash fee calculation)
- **Zcash Transaction Structure**: `zcash/src/primitives/transaction.{h,cpp}`
- **Transaction Builder**: `zcash/src/transaction_builder.{h,cpp}`
- **Value Balance**: `zcash/src/zip317.cpp`
