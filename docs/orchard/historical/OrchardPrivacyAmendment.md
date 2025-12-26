# Orchard Privacy Amendment - Step 1 Complete

## Overview

This document summarizes the initial step of adding Zcash Orchard/Halo2 privacy features to PostFiat. This implementation will enable shielded transactions with zero-knowledge proofs, allowing users to send and receive funds privately.

## What Was Done

### 1. Amendment Definition

**File**: [include/xrpl/protocol/detail/features.macro](../include/xrpl/protocol/detail/features.macro#L36)

```cpp
// Enables Orchard/Halo2 privacy features with shielded transactions
XRPL_FEATURE(OrchardPrivacy, Supported::no, VoteBehavior::DefaultNo)
```

- **Status**: `Supported::no` (development mode)
- **Voting**: `DefaultNo` (requires explicit validator voting)
- **Variable**: `featureOrchardPrivacy` (auto-generated for use in code)

### 2. Transaction Type

**File**: [include/xrpl/protocol/detail/transactions.macro](../include/xrpl/protocol/detail/transactions.macro#L534)

```cpp
TRANSACTION(ttSHIELDED_PAYMENT, 72, ShieldedPayment, Delegation::notDelegatable, ({
    {sfDestination, soeOPTIONAL},
    {sfAmount, soeOPTIONAL},
    {sfOrchardBundle, soeOPTIONAL},
}))
```

- **Type ID**: 72 (`ttSHIELDED_PAYMENT`)
- **Class**: `ShieldedPayment` (to be implemented)
- **Delegation**: Not delegatable (privacy transactions must be direct)

#### Transaction Modes

| Mode | Description | Fields |
|------|-------------|--------|
| **t→z** | Transparent to shielded (shield funds) | `sfAmount`, `sfOrchardBundle` (outputs only) |
| **z→z** | Shielded to shielded (private transfer) | `sfOrchardBundle` (spends + outputs) |
| **z→t** | Shielded to transparent (unshield funds) | `sfDestination`, `sfAmount`, `sfOrchardBundle` (spends only) |

### 3. Field Definition

**File**: [include/xrpl/protocol/detail/sfields.macro](../include/xrpl/protocol/detail/sfields.macro#L280)

```cpp
TYPED_SFIELD(sfOrchardBundle, VL, 32)
```

- **Type**: Variable Length (VL) - for serialized binary data
- **Field ID**: 32
- **Purpose**: Contains serialized Orchard bundle (actions, proofs, encrypted notes)

### 4. Interface Documentation

**File**: [docs/OrchardRustCppInterface.md](OrchardRustCppInterface.md)

Comprehensive documentation covering:
- Architecture overview
- Rust module structure
- C++ wrapper classes
- cxx bridge interface definitions
- Data flow diagrams
- Security considerations
- Implementation phases
- Build system integration

## Design Principles

### 1. Zcash Compatibility

The implementation closely follows the Zcash Orchard specification:
- Same cryptographic primitives (Pallas/Vesta curves, Halo2)
- Same bundle structure (actions, proofs, value balance)
- Same serialization format (compatible with Zcash tools)

### 2. Rust/C++ Separation

Clear separation of concerns:
- **Rust**: All cryptographic operations, proof generation/verification
- **C++**: Transaction processing, ledger state, network protocol
- **cxx bridge**: Type-safe FFI with zero-cost abstractions

### 3. Security First

- All proofs verified in Rust (leveraging Zcash's audited code)
- Nullifier uniqueness checked in C++ ledger state
- No trusted setup required (Halo2 uses transparent setup)
- Full replay protection via transaction hash

### 4. Progressive Implementation

Development divided into clear phases:
1. ✅ **Interface Definition** (current step)
2. Rust module skeleton
3. Core Orchard implementation
4. C++ integration
5. Testing & validation

## Next Steps

### Phase 2: Rust Module Skeleton

1. **Create Rust crate structure**
   ```
   orchard-postfiat/
   ├── Cargo.toml
   ├── build.rs
   └── src/
       ├── lib.rs
       ├── bundle.rs
       └── ffi/
   ```

2. **Define cxx bridge**
   ```rust
   #[cxx::bridge]
   mod ffi {
       extern "Rust" {
           type OrchardBundle;
           fn orchard_bundle_parse(data: &[u8]) -> Result<Box<OrchardBundle>>;
           // ... more functions
       }
   }
   ```

3. **Implement stub functions**
   - Return empty/default values
   - Verify FFI compiles correctly
   - Test C++ can call Rust

4. **Build system integration**
   - Add Cargo build to CMake
   - Link Rust library to rippled
   - Verify compilation works

### Phase 3: Core Implementation

1. Port Orchard circuit from zcash-reference
2. Implement Halo2 proof system integration
3. Note encryption/decryption
4. Merkle tree operations
5. Key derivation

### Phase 4: C++ Integration

1. Create `OrchardBundle` wrapper class
2. Implement `ShieldedPayment` transactor:
   - `preflight()`: Basic validation
   - `preclaim()`: Check nullifiers, anchor
   - `doApply()`: Update ledger state
3. Add ledger object types:
   - `ltORCHARD_ANCHOR`: Merkle tree states
   - `ltORCHARD_NULLIFIER`: Spent notes
4. RPC handlers for shielded operations

### Phase 5: Testing

1. Unit tests (Rust)
2. Integration tests (C++)
3. Performance benchmarks
4. Network simulation
5. Security audit preparation

## File Modifications Summary

| File | Change | Lines |
|------|--------|-------|
| `include/xrpl/protocol/detail/features.macro` | Added OrchardPrivacy amendment | 1 |
| `include/xrpl/protocol/detail/transactions.macro` | Added ttSHIELDED_PAYMENT | 8 |
| `include/xrpl/protocol/detail/sfields.macro` | Added sfOrchardBundle | 1 |
| `docs/OrchardRustCppInterface.md` | Created interface specification | 400+ |
| `docs/OrchardPrivacyAmendment.md` | Created summary document | This file |

## Testing the Changes

### Compilation Check

```bash
cd /home/korisnik/postfiatd
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

This should compile successfully with the new amendment, transaction type, and field definitions.

### Verify Definitions

```bash
# Check amendment is registered
grep -n "OrchardPrivacy" include/xrpl/protocol/detail/features.macro

# Check transaction type is registered
grep -n "ttSHIELDED_PAYMENT" include/xrpl/protocol/detail/transactions.macro

# Check field is registered
grep -n "sfOrchardBundle" include/xrpl/protocol/detail/sfields.macro
```

## Important Notes

### Current Status

⚠️ **Development Mode**: The amendment is marked as `Supported::no`, meaning:
- The code is not yet functional
- The amendment will not appear in validator voting
- Attempting to use `ttSHIELDED_PAYMENT` will fail (no implementation)

### Before Production

Before changing to `Supported::yes`:
1. Complete all implementation phases
2. Comprehensive testing
3. Security audit
4. Community review
5. Testnet deployment

### Amendment Activation

When ready for activation:
1. Change `Supported::no` → `Supported::yes` in features.macro
2. Deploy to validators
3. Validators vote via config:
   ```
   [amendments]
   OrchardPrivacy
   ```
4. 67% threshold for 2 weeks triggers activation

## References

- **Zcash Protocol Spec**: https://zips.z.cash/protocol/protocol.pdf
- **Orchard Book**: https://zcash.github.io/orchard/
- **Halo 2**: https://zcash.github.io/halo2/
- **PostFiat Amendment Guide**: [AmendmentDevelopment.md](AmendmentDevelopment.md)
- **Zcash Reference Code**: `/home/korisnik/postfiatd/zcash-reference/`

## Questions?

For implementation questions or design discussions, refer to:
- [OrchardRustCppInterface.md](OrchardRustCppInterface.md) - Detailed interface spec
- Zcash reference implementation in `zcash-reference/` directory
- PostFiat development documentation
