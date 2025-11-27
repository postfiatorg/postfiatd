# Orchard Privacy Feature - Rust/C++ Interface Design

This document defines the interface boundaries between the Rust Orchard implementation and the C++ PostFiat codebase.

## Overview

The Orchard privacy feature implementation follows the Zcash reference design, with:
- **Rust module**: Core Orchard/Halo2 cryptographic operations
- **C++ integration**: Transaction processing and ledger state management
- **cxx bridge**: Type-safe FFI boundary

## Architecture

```
┌─────────────────────────────────────────────────┐
│            C++ PostFiat Core                    │
│  - Transaction validation                       │
│  - Ledger state management                      │
│  - Network protocol                             │
└──────────────────┬──────────────────────────────┘
                   │ cxx bridge
┌──────────────────▼──────────────────────────────┐
│         Rust Orchard Module                     │
│  - Halo2 zero-knowledge proofs                  │
│  - Action circuit validation                    │
│  - Note encryption/decryption                   │
│  - Nullifier derivation                         │
│  - Merkle tree operations                       │
└─────────────────────────────────────────────────┘
```

## Transaction Type: ttSHIELDED_PAYMENT

### C++ Transaction Structure

```cpp
// Transaction fields (from transactions.macro)
TRANSACTION(ttSHIELDED_PAYMENT, 72, ShieldedPayment, Delegation::notDelegatable, ({
    {sfDestination, soeOPTIONAL},    // For z->t unshielding
    {sfAmount, soeOPTIONAL},         // For t->z shielding or z->t unshielding
    {sfOrchardBundle, soeOPTIONAL},  // Serialized Orchard bundle (VL field)
}))
```

### Transaction Modes

1. **Transparent to Shielded (t→z)**: Shield funds into privacy pool
   - Has `sfAmount` (source from transparent balance)
   - Has `sfOrchardBundle` with encrypted outputs
   - No `sfDestination` (funds go to shielded addresses in bundle)

2. **Shielded to Shielded (z→z)**: Private transfer
   - Only `sfOrchardBundle` (contains both spends and outputs)
   - No transparent fields

3. **Shielded to Transparent (z→t)**: Unshield funds
   - Has `sfDestination` and `sfAmount`
   - Has `sfOrchardBundle` with spends (proofs)

## Rust Module Structure

```
orchard-postfiat/
├── Cargo.toml
├── src/
│   ├── lib.rs              # Main library entry, cxx bridge definitions
│   ├── bundle.rs           # OrchardBundle implementation
│   ├── action.rs           # Action (spend + output) handling
│   ├── note.rs             # Note encryption/decryption
│   ├── nullifier.rs        # Nullifier derivation and checking
│   ├── tree.rs             # Merkle tree operations
│   ├── proof.rs            # Halo2 proof generation/verification
│   └── ffi/
│       ├── mod.rs          # FFI module
│       └── bridge.rs       # C++ bridge functions
└── build.rs                # Build script for cxx
```

## C++ Headers

```
include/xrpl/protocol/
├── OrchardBundle.h         # C++ wrapper for Orchard bundle
└── ShieldedAddress.h       # Orchard address handling

src/xrpld/app/tx/detail/
├── ShieldedPayment.h       # Transaction handler
└── ShieldedPayment.cpp     # Implementation
```

## Interface Definitions (cxx bridge)

### Rust Side (lib.rs)

```rust
#[cxx::bridge]
mod ffi {
    // Opaque Rust types exposed to C++
    extern "Rust" {
        type OrchardBundle;
        type OrchardAction;
        type OrchardProof;

        // Bundle operations
        fn orchard_bundle_parse(data: &[u8]) -> Result<Box<OrchardBundle>>;
        fn orchard_bundle_serialize(bundle: &OrchardBundle) -> Vec<u8>;
        fn orchard_bundle_is_valid(bundle: &OrchardBundle) -> bool;
        fn orchard_bundle_get_value_balance(bundle: &OrchardBundle) -> i64;
        fn orchard_bundle_get_anchor(bundle: &OrchardBundle) -> [u8; 32];
        fn orchard_bundle_get_nullifiers(bundle: &OrchardBundle) -> Vec<[u8; 32]>;
        fn orchard_bundle_num_actions(bundle: &OrchardBundle) -> usize;

        // Proof verification
        fn orchard_verify_bundle_proof(
            bundle: &OrchardBundle,
            sighash: &[u8; 32]
        ) -> bool;

        // Batch verification
        fn orchard_batch_verify_init() -> Box<OrchardBatchVerifier>;
        fn orchard_batch_verify_add(
            verifier: &mut OrchardBatchVerifier,
            bundle: &OrchardBundle,
            sighash: &[u8; 32]
        );
        fn orchard_batch_verify_finalize(verifier: Box<OrchardBatchVerifier>) -> bool;
    }
}
```

### C++ Side (OrchardBundle.h)

```cpp
#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <optional>
#include "rust/cxx.h"

namespace orchard {
    // Forward declarations from Rust
    class OrchardBundle;
    class OrchardBatchVerifier;
}

namespace ripple {

/**
 * C++ wrapper for Orchard bundle.
 * Handles serialization and validation of Orchard shielded operations.
 */
class OrchardBundleWrapper
{
private:
    rust::Box<orchard::OrchardBundle> inner_;

public:
    OrchardBundleWrapper();
    ~OrchardBundleWrapper();

    // Parse from serialized data
    static std::optional<OrchardBundleWrapper> parse(Slice const& data);

    // Serialize to bytes
    Blob serialize() const;

    // Get value balance (net flow in/out of shielded pool)
    std::int64_t getValueBalance() const;

    // Get anchor (Merkle tree root)
    uint256 getAnchor() const;

    // Get all nullifiers (for double-spend checking)
    std::vector<uint256> getNullifiers() const;

    // Get number of actions
    size_t numActions() const;

    // Verify the bundle's proof
    bool verifyProof(uint256 const& sighash) const;
};

/**
 * Batch verifier for multiple Orchard bundles.
 * More efficient than verifying bundles individually.
 */
class OrchardBatchVerifier
{
private:
    rust::Box<orchard::OrchardBatchVerifier> inner_;

public:
    OrchardBatchVerifier();
    ~OrchardBatchVerifier();

    // Add bundle to batch
    void add(OrchardBundleWrapper const& bundle, uint256 const& sighash);

    // Verify all bundles in batch
    bool verify();
};

} // namespace ripple
```

## Data Flow

### Transaction Submission

1. **Client** → Creates shielded transaction with Orchard wallet
2. **Client** → Submits transaction with serialized `OrchardBundle`
3. **C++ (preflight)** → Basic validation (fee, signature)
4. **C++ → Rust** → Parse bundle: `orchard_bundle_parse()`
5. **Rust** → Validate bundle structure
6. **Rust → C++** → Return validation result
7. **C++ (preclaim)** → Check nullifiers not in ledger
8. **C++ (doApply)** → Apply transaction to ledger

### Proof Verification

```
C++ Transaction          Rust Orchard Module
    │                           │
    ├─ Parse bundle ───────────>│
    │                           ├─ Deserialize
    │                           ├─ Extract actions
    │<─ Return parsed ──────────┤
    │                           │
    ├─ Get nullifiers ─────────>│
    │<─ Return nullifiers ──────┤
    │                           │
    ├─ Verify proof ───────────>│
    │                           ├─ Halo2 verification
    │                           ├─ Check constraints
    │<─ Return valid/invalid ───┤
    │                           │
```

## Ledger State Requirements

### New Ledger Objects

1. **OrchardAnchor** (Merkle tree roots)
   - Stores valid Orchard commitment tree states
   - Pruned after certain ledger age

2. **OrchardNullifier** (spent note tracking)
   - Stores nullifiers to prevent double-spends
   - Never pruned (permanent)

### Ledger Object Types

```cpp
// In LedgerFormats.h
enum LedgerEntryType {
    // ... existing types ...
    ltORCHARD_ANCHOR = ???,
    ltORCHARD_NULLIFIER = ???,
};
```

## Amendment

```cpp
// In features.macro
XRPL_FEATURE(OrchardPrivacy, Supported::no, VoteBehavior::DefaultNo)
```

When enabled:
- Allows `ttSHIELDED_PAYMENT` transactions
- Enables Orchard bundle validation
- Maintains Orchard Merkle tree state
- Tracks nullifiers

## Security Considerations

1. **Proof Verification**: All Halo2 proofs MUST be verified in Rust
2. **Nullifier Uniqueness**: C++ MUST check nullifiers against ledger state
3. **Value Balance**: C++ MUST validate net value flow is correct
4. **Anchor Validity**: C++ MUST check anchor exists in recent ledger history
5. **Replay Protection**: Transaction hash includes Orchard bundle

## Implementation Phases

### Phase 1: Interface Definition (Current)
- [x] Define amendment
- [x] Define transaction type
- [x] Define SField for OrchardBundle
- [x] Document Rust/C++ interface

### Phase 2: Rust Module Skeleton
- [ ] Create Rust crate structure
- [ ] Define cxx bridge
- [ ] Implement stub functions
- [ ] Build system integration

### Phase 3: Core Orchard Implementation
- [ ] Port Orchard circuit from zcash
- [ ] Implement Halo2 proof system
- [ ] Note encryption/decryption
- [ ] Merkle tree operations

### Phase 4: C++ Integration
- [ ] OrchardBundle wrapper class
- [ ] ShieldedPayment transactor
- [ ] Ledger state objects
- [ ] RPC handlers

### Phase 5: Testing & Validation
- [ ] Unit tests (Rust)
- [ ] Integration tests (C++)
- [ ] Proof verification benchmarks
- [ ] Network simulation

## Dependencies

### Rust Crates
- `halo2_proofs` - Zero-knowledge proof system
- `orchard` - Zcash Orchard implementation
- `cxx` - C++/Rust FFI
- `group` - Elliptic curve operations
- `pasta_curves` - Pallas/Vesta curves

### C++ Libraries
- Existing PostFiat/rippled infrastructure
- No new external C++ dependencies

## Build System

```toml
# Cargo.toml
[package]
name = "orchard-postfiat"
version = "0.1.0"
edition = "2021"

[dependencies]
orchard = "0.7"
halo2_proofs = "0.3"
cxx = "1.0"

[build-dependencies]
cxx-build = "1.0"
```

```cmake
# CMakeLists.txt addition
add_subdirectory(orchard-postfiat)
target_link_libraries(rippled PRIVATE orchard-postfiat)
```

## References

- [Zcash Orchard Specification](https://zips.z.cash/protocol/protocol.pdf)
- [Halo 2 Documentation](https://zcash.github.io/halo2/)
- [cxx - Safe FFI](https://cxx.rs/)
- Zcash Reference: `/home/korisnik/postfiatd/zcash-reference/`
