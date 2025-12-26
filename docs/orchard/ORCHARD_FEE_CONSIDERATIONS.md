# Orchard Transaction Fee Considerations

**Status**: Future Enhancement
**Priority**: Medium
**Related**: OrchardPrivacy Amendment, ShieldedPayment Transaction

---

## Current Implementation

PostFiat currently uses a **flat fee model** for all ShieldedPayment (Orchard) transactions:

- **Base Fee**: 10 drops (0.00001 XRP)
- **Same fee for all transaction types**:
  - Normal Payment: 10 drops
  - ShieldedPayment (t→z, z→z, z→t): 10 drops
- **No complexity scaling**: Fee doesn't increase with transaction size or computational cost

### Code Location
- Fee calculation: [Transactor.cpp:270-278](../src/xrpld/app/tx/detail/Transactor.cpp#L270)
- ShieldedPayment: [ShieldedPayment.cpp](../src/xrpld/app/tx/detail/ShieldedPayment.cpp)

---

## Zcash Fee Model Comparison

### ZIP-317: Action-Based Fee Structure

Zcash uses a sophisticated fee model that scales with transaction complexity:

```
Fee = MARGINAL_FEE × max(GRACE_ACTIONS, logicalActionCount)
```

**Constants**:
- `MARGINAL_FEE` = 5000 zatoshi (0.00005 ZEC)
- `GRACE_ACTIONS` = 2 (minimum fee baseline)
- `MINIMUM_FEE` = 10000 zatoshi (0.0001 ZEC)

**Logical Action Calculation** ([zip317.cpp](../zcash-reference/zcash/src/zip317.cpp)):
```cpp
logicalActionCount =
    max(tx_in_count, tx_out_count) +           // Transparent I/O
    2 × joinSplitCount +                       // JoinSplit operations
    max(saplingSpendCount, saplingOutputCount) + // Sapling
    orchardActionCount;                        // Orchard Actions
```

### Example Fee Calculations

| Transaction Type | Logical Actions | Zcash Fee | PostFiat Fee |
|------------------|-----------------|-----------|--------------|
| Normal Payment (1 in, 1 out) | 2 (grace) | 10000 zatoshi | 10 drops |
| Orchard z→z (1 action) | 2 (grace) | 10000 zatoshi | 10 drops |
| Orchard z→z (2 actions) | 2 | 10000 zatoshi | 10 drops |
| Orchard z→z (5 actions) | 5 | **25000 zatoshi** | 10 drops |
| Complex Orchard (10 actions) | 10 | **50000 zatoshi** | 10 drops |

---

## Cost Analysis

### Computational Cost Comparison

**Orchard Proof Verification**:
- **Halo2 proof verification**: ~50-100ms (depends on hardware)
- **RedPallas signature verification**: ~1-2ms per action
- **Nullifier checks**: Database lookups per spend

**Standard Payment Verification**:
- **ECDSA signature**: ~0.1ms
- **Account balance check**: Simple database lookup

**Ratio**: Orchard verification is **100-1000× more expensive** than standard payment verification.

### Current Fee Disparity

At current prices (approximate):
- 1 XRP = $0.50
- 1 ZEC = $30.00

| Metric | PostFiat | Zcash | Ratio |
|--------|----------|-------|-------|
| **Orchard Fee (USD)** | $0.000005 | $0.003 | 600× cheaper |
| **Orchard Fee (native)** | 10 drops | 10000 zatoshi | 1000× less |

---

## Potential Issues

### 1. Network Spam Vulnerability

**Problem**: Low fees could enable spam attacks using complex Orchard transactions.

**Attack Vector**:
- Attacker creates large Orchard bundles (10+ actions)
- Costs same 10 drops but requires significant validator resources
- Could cause network slowdown with minimal cost

**Mitigation**: Implement action-based fee multiplier.

### 2. Unfair Resource Pricing

**Problem**: Users creating simple vs. complex Orchard transactions pay the same.

**Example**:
- User A: z→z with 1 action → 10 drops
- User B: z→z with 10 actions → 10 drops
- User B consumes 10× validation resources but pays same fee

### 3. Economic Misalignment

**Problem**: Validator incentives don't align with validation cost.

**Current State**:
- Validators earn same fee for all transactions
- No economic incentive to prioritize cheaper-to-validate transactions
- Complex Orchard transactions may be deprioritized

---

## Proposed Solutions

### Option 1: Fixed Multiplier (Simple)

**Implementation**: All ShieldedPayment transactions pay `N × baseFee`

```cpp
XRPAmount ShieldedPayment::calculateBaseFee(
    ReadView const& view,
    STTx const& tx,
    beast::Journal j)
{
    // Orchard transactions pay 10× base fee
    return view.fees().base * 10;
}
```

**Pros**:
- ✅ Simple to implement
- ✅ Accounts for higher validation cost
- ✅ Still much cheaper than Zcash

**Cons**:
- ❌ Doesn't scale with complexity
- ❌ Still allows spam with large bundles

**Suggested Multiplier**: 10× (100 drops = 0.0001 XRP)

### Option 2: Action-Based Scaling (Zcash-like)

**Implementation**: Fee scales with number of Orchard actions

```cpp
XRPAmount ShieldedPayment::calculateBaseFee(
    ReadView const& view,
    STTx const& tx,
    beast::Journal j)
{
    auto bundle = getBundle(tx);
    if (!bundle)
        return view.fees().base;

    auto actionCount = bundle->getActionCount();
    auto graceActions = 2; // Minimum

    // Fee = baseFee × max(graceActions, actionCount)
    return view.fees().base * std::max(graceActions, actionCount);
}
```

**Pros**:
- ✅ Fair pricing based on complexity
- ✅ DoS protection (large bundles cost proportionally more)
- ✅ Follows Zcash proven model
- ✅ Economic incentives align with costs

**Cons**:
- ❌ More complex implementation
- ❌ Need to expose action count from OrchardBundle

**Example Fees** (with this model):

| Actions | Fee (drops) | Fee (XRP) |
|---------|-------------|-----------|
| 1 | 20 (grace) | 0.00002 |
| 2 | 20 | 0.00002 |
| 5 | 50 | 0.00005 |
| 10 | 100 | 0.0001 |

### Option 3: Hybrid Approach

**Implementation**: Base multiplier + action scaling

```cpp
const size_t ORCHARD_BASE_MULTIPLIER = 5;
const size_t ORCHARD_ACTION_MULTIPLIER = 2;

XRPAmount ShieldedPayment::calculateBaseFee(
    ReadView const& view,
    STTx const& tx,
    beast::Journal j)
{
    auto bundle = getBundle(tx);
    if (!bundle)
        return view.fees().base;

    auto actionCount = bundle->getActionCount();

    // Fee = (5 + 2 × actionCount) × baseFee
    return view.fees().base * (ORCHARD_BASE_MULTIPLIER +
                               ORCHARD_ACTION_MULTIPLIER * actionCount);
}
```

**Pros**:
- ✅ Base fee accounts for proof verification overhead
- ✅ Scaling accounts for action complexity
- ✅ More granular control

**Example Fees**:

| Actions | Calculation | Fee (drops) |
|---------|-------------|-------------|
| 1 | (5 + 2×1) × 10 | 70 |
| 2 | (5 + 2×2) × 10 | 90 |
| 5 | (5 + 2×5) × 10 | 150 |
| 10 | (5 + 2×10) × 10 | 250 |

---

## Implementation Requirements

### Code Changes Required

1. **Add `calculateBaseFee()` override** in ShieldedPayment class:
   - Location: [ShieldedPayment.h](../src/xrpld/app/tx/detail/ShieldedPayment.h)
   - Override `Transactor::calculateBaseFee()`

2. **Expose action count** from OrchardBundle:
   - Location: [OrchardBundle.h](../include/xrpl/protocol/OrchardBundle.h)
   - Add `getActionCount()` method
   - May require Rust FFI bridge update

3. **Update fee estimation** in RPC handlers:
   - Location: [OrchardPreparePayment.cpp](../src/xrpld/rpc/handlers/OrchardPreparePayment.cpp)
   - Calculate and display estimated fee based on action count

4. **Add tests** for fee calculation:
   - Unit tests for different action counts
   - Integration tests for fee payment from shielded pool

### Rust FFI Bridge Changes

If implementing action-based fees, need to expose action count:

```rust
// In orchard-postfiat/src/ffi/bridge.rs
pub fn orchard_bundle_get_action_count(bundle_bytes: &[u8]) -> Result<usize, String> {
    let bundle = parse_bundle(bundle_bytes)?;
    Ok(bundle.actions().len())
}
```

---

## Backward Compatibility

### Amendment Gating

Fee changes should be gated by an amendment to ensure network consensus:

**Option A**: Add to existing OrchardPrivacy amendment
- Requires OrchardPrivacy not yet activated
- Changes take effect when OrchardPrivacy activates

**Option B**: Create new amendment `OrchardFees`
- Requires OrchardPrivacy to be enabled first
- Allows phased rollout
- More flexible deployment

### Migration Path

1. **Before Amendment**: All ShieldedPayment uses 10 drops
2. **After Amendment**: Fee calculation switches to new model
3. **Wallets**: Update fee estimation in UI/API

---

## Recommendations

### Short Term (Current Implementation)

**Keep flat fee model** but document the limitation:
- ✅ Simple and working
- ✅ Allows initial OrchardPrivacy testing
- ❌ Not production-ready for high-volume usage

### Medium Term (6-12 months)

**Implement Option 1 (Fixed Multiplier)**:
- Easy implementation (1-2 days work)
- Provides spam protection
- Reasonable fee structure
- **Suggested: 10× multiplier** (100 drops = 0.0001 XRP)

### Long Term (1-2 years)

**Implement Option 2 (Action-Based)**:
- Full Zcash ZIP-317 compatibility
- Fair resource pricing
- Optimal economic model
- Requires more infrastructure (action count exposure)

---

## References

### Zcash Documentation

- **ZIP-317**: https://zips.z.cash/zip-0317
- **Fee Calculation Code**: [zcash/src/zip317.cpp](../zcash-reference/zcash/src/zip317.cpp)
- **Fee Constants**: [zcash/src/zip317.h](../zcash-reference/zcash/src/zip317.h)

### PostFiat Code

- **Current Fee Logic**: [Transactor.cpp:270-278](../src/xrpld/app/tx/detail/Transactor.cpp#L270)
- **ShieldedPayment**: [ShieldedPayment.cpp](../src/xrpld/app/tx/detail/ShieldedPayment.cpp)
- **OrchardBundle**: [OrchardBundle.h](../include/xrpl/protocol/OrchardBundle.h)

### Related Documents

- [Orchard Implementation Status](./OrchardImplementationStatus.md)
- [Orchard Wallet Integration](./ORCHARD_WALLET_INTEGRATION.md)
- [Orchard Value Balance](./OrchardValueBalance.md)

---

## Open Questions

1. **What action count limit should be enforced?**
   - Zcash has no hard limit, relies on fees
   - Should PostFiat set a max (e.g., 100 actions per transaction)?

2. **Should fee multiplier be configurable?**
   - Via amendment parameters?
   - Via validator voting (like reserve fees)?

3. **How to handle fee payment from shielded pool?**
   - Current: valueBalance must cover fee
   - With higher fees: Ensure sufficient valueBalance

4. **Network consensus on fee structure?**
   - Needs community discussion
   - Validator feedback on preferred model

---

## Action Items

- [ ] Community discussion on fee model preference
- [ ] Validator feedback on resource costs
- [ ] Performance benchmarking of Orchard validation
- [ ] Design amendment for fee changes
- [ ] Implement chosen solution (likely Option 1 short-term)
- [ ] Add fee estimation to wallet UIs
- [ ] Document fee structure for developers

---

**Last Updated**: 2025-12-22
**Status**: Discussion / Planning Phase
**Next Steps**: Community feedback + validator input on fee model
