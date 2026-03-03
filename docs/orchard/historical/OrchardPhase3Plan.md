# Orchard Phase 3: Real Cryptography Implementation Plan

**Goal**: Replace stub implementations with real Zcash Orchard cryptography

**Status**: 🚧 In Progress

---

## Overview

Phase 3 involves replacing our stub `OrchardBundle` implementation with real Orchard cryptography from the Zcash `orchard` crate. This is the most complex phase as it involves:

1. Understanding Zcash's Orchard bundle format
2. Integrating Halo2 proof verification
3. Handling cryptographic types safely across FFI
4. Implementing serialization/deserialization

---

## Current State

### What We Have (Stubs)

```rust
pub struct OrchardBundle {
    data: Vec<u8>,              // Just stores raw bytes
    num_actions: usize,         // Hardcoded to 0
    value_balance: i64,         // Hardcoded to 0
    anchor: [u8; 32],           // All zeros
    nullifiers: Vec<[u8; 32]>,  // Empty
}
```

**Problems**:
- Doesn't parse real Orchard bundles
- Proof verification always returns `true`
- No real cryptographic operations
- Can't construct or validate real transactions

### What We Need (Real)

```rust
pub struct OrchardBundle {
    inner: Option<orchard::Bundle<...>>,  // Real Zcash bundle
    // Cached/derived fields for FFI
}
```

**Features**:
- Parse real Orchard bundle format (ZIP-225)
- Verify Halo2 proofs correctly
- Extract nullifiers, anchors, value balance
- Support transaction building (future)

---

## Implementation Strategy

### Approach 1: Wrapper Around `orchard::Bundle` ✅ (Recommended)

**Idea**: Keep real `orchard::Bundle` internally, expose via our interface

**Pros**:
- Uses battle-tested Zcash implementation
- Automatic updates when Zcash updates
- Minimal code to maintain
- Correct by construction

**Cons**:
- Need to handle Option<Bundle> (bundle can be None)
- Some impedance mismatch with FFI
- Larger binary size (full Orchard)

**Implementation**:
```rust
pub struct OrchardBundle {
    // The real Zcash bundle (if present)
    inner: Option<orchard::Bundle<Authorized, Amount>>,

    // Cached for FFI (computed once, reused)
    cached_nullifiers: Option<Vec<[u8; 32]>>,
    cached_value_balance: Option<i64>,
}
```

### Approach 2: Custom Implementation ❌ (Not Recommended)

**Idea**: Implement our own Orchard bundle parsing/verification

**Pros**:
- Full control
- Smaller binary (only what we need)

**Cons**:
- Massive security risk (crypto is hard!)
- Months of development
- Need security audits
- Bug compatibility with Zcash

**Verdict**: Don't do this. Use Zcash's implementation.

---

## Implementation Plan

### Step 1: Update Dependencies ✅

Already have in Cargo.toml:
```toml
orchard = "0.7"           # Orchard protocol
halo2_proofs = "0.3"      # ZK proofs
pasta_curves = "0.5"      # Elliptic curves
group = "0.13"            # Group operations
```

May need to add:
```toml
zcash_primitives = "0.13"  # For Amount type
zcash_note_encryption = "0.4"  # For note encryption (future)
```

### Step 2: Understand Orchard Bundle Structure

From `orchard` crate:

```rust
pub struct Bundle<T: Authorization, V> {
    // Orchard-specific transaction data
    actions: Vec<Action<A>>,
    flags: Flags,
    value_balance: V,  // ValueSum (can be positive/negative/zero)
    anchor: Anchor,    // Merkle tree root
    authorization: T,  // Proof + signatures
}

// Action = one spend + one output
pub struct Action<A> {
    // Spend part
    cv: ValueCommitment,       // Value commitment
    nullifier: Nullifier,      // Spend nullifier
    rk: VerificationKey,       // Randomized verification key

    // Output part
    cmx: ExtractedNoteCommitment,  // Note commitment
    ephemeral_key: EphemeralKey,   // Encryption key
    enc_ciphertext: [u8; 580],     // Encrypted note
    out_ciphertext: [u8; 80],      // Encrypted outgoing viewing key

    // Authorization (proof + signature)
    authorization: A,
}

pub struct Authorized {
    proof: Proof,              // Halo2 proof
    binding_signature: Signature,  // Binding signature
}
```

### Step 3: Implement Bundle Parsing

**From bytes** (transaction deserialization):

```rust
impl OrchardBundle {
    pub fn parse(data: &[u8]) -> Result<Self, String> {
        if data.is_empty() {
            return Ok(Self {
                inner: None,
                cached_nullifiers: None,
                cached_value_balance: None,
            });
        }

        // Parse using Zcash's format
        let mut reader = std::io::Cursor::new(data);

        match orchard::Bundle::read(&mut reader) {
            Ok(bundle) => {
                Ok(Self {
                    inner: Some(bundle),
                    cached_nullifiers: None,
                    cached_value_balance: None,
                })
            }
            Err(e) => Err(format!("Failed to parse Orchard bundle: {:?}", e))
        }
    }
}
```

**Challenge**: `orchard::Bundle::read()` requires specific types:
- `V` (value type) - need to use `orchard::value::ValueSum`
- `T` (authorization) - need `Authorized` (with proof + signature)

### Step 4: Implement Bundle Serialization

**To bytes** (transaction serialization):

```rust
impl OrchardBundle {
    pub fn serialize(&self) -> Vec<u8> {
        match &self.inner {
            None => Vec::new(),
            Some(bundle) => {
                let mut buf = Vec::new();
                bundle.write(&mut buf).expect("serialization cannot fail");
                buf
            }
        }
    }
}
```

### Step 5: Implement Value Balance

**Extract from bundle**:

```rust
impl OrchardBundle {
    pub fn value_balance(&self) -> i64 {
        // Return cached if available
        if let Some(vb) = self.cached_value_balance {
            return vb;
        }

        match &self.inner {
            None => 0,
            Some(bundle) => {
                // ValueSum is in "zatoshis" (smallest unit)
                // Convert to drops (XRP smallest unit)
                // Both are i64 with same semantics
                let value_sum = bundle.value_balance();

                // Cache it
                self.cached_value_balance = Some(*value_sum);
                *value_sum
            }
        }
    }
}
```

**Note**: Zcash uses `ValueSum` which is `i64` representing zatoshis. PostFiat uses drops. They're semantically equivalent!

### Step 6: Implement Nullifier Extraction

**Extract from actions**:

```rust
impl OrchardBundle {
    pub fn nullifiers(&self) -> Vec<[u8; 32]> {
        // Return cached if available
        if let Some(ref nfs) = self.cached_nullifiers {
            return nfs.clone();
        }

        match &self.inner {
            None => Vec::new(),
            Some(bundle) => {
                let nullifiers: Vec<[u8; 32]> = bundle
                    .actions()
                    .iter()
                    .map(|action| action.nullifier().to_bytes())
                    .collect();

                // Cache it
                self.cached_nullifiers = Some(nullifiers.clone());
                nullifiers
            }
        }
    }
}
```

### Step 7: Implement Anchor Extraction

**Extract from bundle**:

```rust
impl OrchardBundle {
    pub fn anchor(&self) -> [u8; 32] {
        match &self.inner {
            None => [0u8; 32],
            Some(bundle) => bundle.anchor().to_bytes(),
        }
    }
}
```

### Step 8: Implement Proof Verification

**The critical part** - verify Halo2 ZK proof:

```rust
use orchard::circuit::VerifyingKey;

impl OrchardBundle {
    pub fn verify_proof(&self, sighash: &[u8; 32]) -> bool {
        match &self.inner {
            None => false,  // No bundle = invalid
            Some(bundle) => {
                // Get the verifying key (computed once, cached globally)
                let vk = VerifyingKey::build();

                // Verify the proof
                match bundle.verify_proof(&vk, sighash) {
                    Ok(()) => true,
                    Err(e) => {
                        eprintln!("Proof verification failed: {:?}", e);
                        false
                    }
                }
            }
        }
    }
}
```

**Performance**: Proof verification is expensive (~1-2 seconds). Need to:
1. Cache verifying key (computed once at startup)
2. Consider batch verification for multiple bundles

---

## Key Challenges

### Challenge 1: Type Parameters

`orchard::Bundle` is generic:
```rust
Bundle<T: Authorization, V>
```

**Solutions**:
- For `T`: Use `Authorized` (bundle with proof + signature)
- For `V`: Use `orchard::value::ValueSum` (i64 wrapper)

**In code**:
```rust
type RealBundle = orchard::Bundle<orchard::bundle::Authorized, orchard::value::ValueSum>;
```

### Challenge 2: Serialization Format

Zcash uses specific byte format (ZIP-225). Must match exactly.

**Format**:
```
[nActions: CompactSize]
[actions: Action[nActions]]
[flags: byte]
[valueBalance: i64]
[anchor: [u8; 32]]
[proof: [u8; ...]]  # Variable length Halo2 proof
[bindingSig: [u8; 64]]
```

**Solution**: Use `orchard::Bundle::read()` and `::write()` directly.

### Challenge 3: Proof Verification Cost

Halo2 proof verification is slow (~1-2 seconds per bundle).

**Solutions**:
1. **Verify in preclaim** (before consensus) - Already doing this ✅
2. **Batch verification**: Verify multiple proofs together
3. **Cache verification results**: Store (bundle_hash → verified) mapping
4. **Parallel verification**: Use multiple cores

**Future optimization**:
```rust
pub fn verify_proofs_batch(
    bundles: &[(OrchardBundle, [u8; 32])],  // (bundle, sighash) pairs
) -> Vec<bool> {
    // Batch verify all proofs together
    // ~30% faster than individual verification
}
```

### Challenge 4: Empty Bundles

PostFiat allows transactions without Orchard bundles. Must handle:

```rust
impl OrchardBundle {
    pub fn is_present(&self) -> bool {
        self.inner.is_some()
    }
}
```

### Challenge 5: FFI Boundary

Can't pass `orchard::Bundle` directly to C++. Must:
1. Keep bundle in Rust
2. Expose only primitives (i64, [u8; 32], etc.)
3. Compute derived fields once, cache them

---

## Testing Strategy

### Unit Tests (Rust)

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_valid_bundle() {
        // Use real Zcash test vectors
        let bundle_bytes = include_bytes!("../test_vectors/bundle1.bin");
        let bundle = OrchardBundle::parse(bundle_bytes).unwrap();

        assert!(bundle.is_present());
        assert!(bundle.is_valid());
        assert_eq!(bundle.num_actions(), 2);
    }

    #[test]
    fn test_verify_proof() {
        let bundle_bytes = include_bytes!("../test_vectors/bundle1.bin");
        let bundle = OrchardBundle::parse(bundle_bytes).unwrap();

        let sighash = [0u8; 32];  // From test vector
        assert!(bundle.verify_proof(&sighash));
    }

    #[test]
    fn test_nullifiers() {
        let bundle_bytes = include_bytes!("../test_vectors/bundle1.bin");
        let bundle = OrchardBundle::parse(bundle_bytes).unwrap();

        let nullifiers = bundle.nullifiers();
        assert_eq!(nullifiers.len(), 2);
        // Check against known nullifiers from test vector
    }
}
```

### Integration Tests (C++)

Test the FFI boundary:

```cpp
TEST_F(OrchardBundle_test, parse_and_verify) {
    // Load test vector
    auto bundleData = loadTestVector("bundle1.bin");

    // Parse in Rust
    auto bundle = OrchardBundleWrapper::parse(bundleData);
    ASSERT_TRUE(bundle.has_value());

    // Verify proof
    uint256 sighash;  // From test vector
    ASSERT_TRUE(bundle->verifyProof(sighash));

    // Check properties
    ASSERT_EQ(bundle->numActions(), 2);
    ASSERT_GT(bundle->getValueBalance(), 0);
}
```

---

## Zcash Test Vectors

Use official Zcash test vectors from:
- `orchard/test_vectors/` in Zcash repository
- Or generate with Zcash tools

**Example test vector structure**:
```json
{
  "bundle_hex": "0x...",
  "sighash": "0x...",
  "expected_nullifiers": ["0x...", "0x..."],
  "expected_anchor": "0x...",
  "expected_value_balance": -100000000,
  "proof_valid": true
}
```

---

## Performance Targets

| Operation | Target | Notes |
|-----------|--------|-------|
| Parse bundle | < 1ms | Should be fast |
| Serialize bundle | < 1ms | Should be fast |
| Verify proof (single) | 1-2s | Halo2 is expensive |
| Verify proof (batch) | ~0.7s each | 30% speedup |
| Extract nullifiers | < 0.1ms | Simple field access |
| Extract value balance | < 0.1ms | Simple field access |

---

## Security Considerations

### Critical: Proof Verification

**MUST** verify proofs correctly:
```rust
// NEVER do this:
pub fn verify_proof(&self, _sighash: &[u8; 32]) -> bool {
    true  // ❌ SECURITY HOLE!
}

// ALWAYS do this:
pub fn verify_proof(&self, sighash: &[u8; 32]) -> bool {
    let vk = VerifyingKey::build();
    bundle.verify_proof(&vk, sighash).is_ok()  // ✅ Real verification
}
```

### Critical: Nullifier Uniqueness

Nullifiers MUST be unique (checked in C++ preclaim):
```cpp
for (auto const& nf : bundle->getNullifiers()) {
    if (ctx.view.exists(keylet::orchardNullifier(nf))) {
        return tefORCHARD_DUPLICATE_NULLIFIER;  // ✅ Reject double-spend
    }
}
```

### Critical: Value Conservation

Value balance MUST match transaction fields:
```rust
// In parse: verify value_balance matches actions
let computed_balance = actions.iter()
    .map(|a| a.value_commitment().value())
    .sum();
assert_eq!(computed_balance, bundle.value_balance());
```

---

## Implementation Checklist

- [ ] Add type aliases for Bundle types
- [ ] Implement real parsing with `orchard::Bundle::read()`
- [ ] Implement real serialization with `orchard::Bundle::write()`
- [ ] Extract value balance from bundle
- [ ] Extract anchor from bundle
- [ ] Extract nullifiers from actions
- [ ] Implement proof verification with VerifyingKey
- [ ] Add caching for expensive operations
- [ ] Handle empty bundles (None case)
- [ ] Add error handling for parse failures
- [ ] Write unit tests with test vectors
- [ ] Write integration tests for FFI
- [ ] Measure performance
- [ ] Add batch verification (optional optimization)

---

## Next Steps

1. **Start with parsing**: Get `orchard::Bundle::read()` working
2. **Test with vectors**: Use Zcash test vectors to validate
3. **Implement verification**: Get real proof verification working
4. **Optimize later**: Start simple, optimize after correctness

---

## Resources

- [Orchard Book](https://zcash.github.io/orchard/) - Orchard specification
- [orchard crate docs](https://docs.rs/orchard/) - Rust API reference
- [ZIP-225](https://zips.z.cash/zip-0225) - Orchard bundle format
- [Halo2 Book](https://zcash.github.io/halo2/) - Proof system
- [Zcash test vectors](https://github.com/zcash/zcash-test-vectors) - Official test data

---

**Status**: Ready to begin implementation
**Estimated Effort**: 2-3 days for basic implementation, 1 week for optimization
**Risk Level**: Medium (crypto integration is complex but well-documented)
