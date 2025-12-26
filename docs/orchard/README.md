# Orchard Privacy Documentation

This directory contains comprehensive documentation for the Orchard privacy feature implementation in PostFiat. The implementation is ~90% complete with real Halo2 zero-knowledge proofs and full wallet integration.

## 📋 Quick Navigation

### 🚀 Getting Started
- **[OrchardQuickStart.md](OrchardQuickStart.md)** - Start here! Quick overview of what works, current status, and key concepts
- **[OrchardImplementationStatus.md](OrchardImplementationStatus.md)** - Complete status report with detailed progress tracking (~90% complete)

### 🔧 Technical Reference
- **[OrchardRustCppInterface.md](OrchardRustCppInterface.md)** - FFI interface specification (30+ functions)
- **[OrchardTransactionVerification.md](OrchardTransactionVerification.md)** - 3-stage verification pipeline (preflight/preclaim/doApply)
- **[OrchardValueBalance.md](OrchardValueBalance.md)** - Comprehensive value balance system guide (510 lines)
- **[OrchardValueBalanceImplemented.md](OrchardValueBalanceImplemented.md)** - Value balance implementation summary

### 💰 Fee Design
- **[ORCHARD_FEE_CONSIDERATIONS.md](ORCHARD_FEE_CONSIDERATIONS.md)** - Future fee structure enhancements (action-based scaling)
- **[OrchardFeeStrategy.md](OrchardFeeStrategy.md)** - Zcash fee payment model analysis

### 💼 Wallet Integration
- **[ORCHARD_WALLET_INTEGRATION.md](ORCHARD_WALLET_INTEGRATION.md)** - Server-side wallet status (75% complete)

### 🧪 Testing
- **[ORCHARD_MANUAL_TESTING.md](ORCHARD_MANUAL_TESTING.md)** - Comprehensive manual testing procedures (~2800 lines)

### 📚 Historical Documentation
- **[historical/](historical/)** - Archived phase documents (Phase 1-4 completion summaries)

---

## Current Implementation Status

**Overall Progress: ~90% Complete** (as of December 2024)

### ✅ What's Working
- **Real Orchard Cryptography** - Halo2 proofs, note encryption, bundle building
- **All Transaction Types** - t→z (shield), z→z (private transfer), z→t (unshield)
- **Transaction Processing** - Full validation pipeline with fee handling
- **Ledger Integration** - Anchor tracking, nullifier tracking, commitment persistence
- **Server-Side Wallet (75%)** - Viewing keys, note scanning, balance calculation, payment preparation
- **RPC Methods** - `orchard_wallet_add_key`, `orchard_scan_balance`, `orchard_prepare_payment`
- **Testing** - 166 tests passing across 10 test cases

### ⏳ Remaining Work (Final 10%)
- Automatic note decryption during ledger processing
- Wallet persistence (save/load from disk)
- Witness updates for existing notes when new commitments added

---

## Document Relationships

```
OrchardQuickStart.md (START HERE)
    ├─> OrchardImplementationStatus.md (detailed status)
    │   └─> ORCHARD_WALLET_INTEGRATION.md (wallet specifics)
    │
    ├─> OrchardValueBalance.md (comprehensive guide)
    │   └─> OrchardValueBalanceImplemented.md (implementation)
    │
    ├─> OrchardRustCppInterface.md (FFI specification)
    │   └─> OrchardTransactionVerification.md (validation pipeline)
    │
    ├─> ORCHARD_FEE_CONSIDERATIONS.md (future enhancements)
    │   └─> OrchardFeeStrategy.md (Zcash analysis)
    │
    └─> ORCHARD_MANUAL_TESTING.md (testing procedures)
```

---

## Key Features

### Privacy Features
- **Shielded Transactions** - Hide amounts and recipients using zero-knowledge proofs
- **Viewing Keys** - Optional transparency with full viewing keys
- **Memo Field** - Encrypted messages attached to shielded notes

### Transaction Types

| Type | Description | Fields |
|------|-------------|--------|
| **t→z** | Shield funds into privacy pool | `sfAccount`, `sfAmount`, `sfOrchardBundle` |
| **z→z** | Private shielded transfer | `sfOrchardBundle` only |
| **z→t** | Unshield funds to transparent | `sfDestination`, `sfAmount`, `sfOrchardBundle` |

### Fee Payment Flexibility
- Fees can be paid from **transparent** balance (traditional)
- Fees can be paid from **shielded pool** (fully private!)
- Uses Zcash's value balance model

---

## Building and Testing

### Build PostFiat with Orchard
```bash
# Build Rust crate
cd orchard-postfiat
cargo build --release

# Build PostFiat
cd ..
mkdir .build && cd .build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target postfiatd -j4
```

### Run Orchard Tests
```bash
# From build directory
./postfiatd --unittest=ripple.rpc.OrchardFullFlow

# Or run all tests
./postfiatd --unittest
```

### Manual Testing
See [ORCHARD_MANUAL_TESTING.md](ORCHARD_MANUAL_TESTING.md) for comprehensive RPC testing procedures.

---

## RPC Methods

### orchard_wallet_add_key
Add a full viewing key to the server-side wallet.

```json
{
  "method": "orchard_wallet_add_key",
  "params": [{
    "full_viewing_key": "HEXSTRING"
  }]
}
```

### orchard_scan_balance
Scan ledger for notes belonging to viewing keys and calculate balance.

```json
{
  "method": "orchard_scan_balance",
  "params": [{
    "full_viewing_key": "HEXSTRING",
    "ledger_index_min": 1,
    "ledger_index_max": 100
  }]
}
```

### orchard_prepare_payment
Prepare a shielded payment transaction (t→z, z→z, or z→t).

```json
{
  "method": "orchard_prepare_payment",
  "params": [{
    "payment_type": "t_to_z",  // or "z_to_z" or "z_to_t"
    "amount": "1000000000",
    "source_account": "rALICE...",  // for t→z
    "recipient": "ORCHARD_ADDRESS",  // for t→z, z→z
    "spending_key": "HEXSTRING",  // for z→z, z→t
    "destination_account": "rBOB..."  // for z→t
  }]
}
```

---

## Architecture Overview

```
┌─────────────────────────────────────────────────┐
│            C++ PostFiat Core                    │
│  - Transaction validation (3-stage pipeline)    │
│  - Ledger state (anchors, nullifiers)           │
│  - RPC handlers                                 │
│  - Server-side wallet                           │
└──────────────────┬──────────────────────────────┘
                   │ cxx bridge (30+ FFI functions)
┌──────────────────▼──────────────────────────────┐
│         Rust Orchard Module                     │
│  - Halo2 zero-knowledge proofs                  │
│  - Note encryption/decryption                   │
│  - Nullifier derivation                         │
│  - Merkle tree operations                       │
│  - Bundle building and parsing                  │
│  - Wallet state management                      │
└─────────────────────────────────────────────────┘
```

---

## Security Features

### Double-Spend Prevention
- Nullifier tracking in ledger state
- Duplicate nullifier rejection (`tefORCHARD_DUPLICATE_NULLIFIER`)
- Comprehensive test coverage including double-spend attempts

### Proof Verification
- Real Halo2 proof verification for all transactions
- Batch verification support for efficient block validation
- Anchor validation against recent ledger history

### Privacy Guarantees
- Shielded amounts (hidden transaction values)
- Shielded recipients (hidden destination addresses)
- Viewing key decryption (optional transparency)
- Memo encryption (private messages)

---

## Next Steps

### For Developers
1. Read [OrchardQuickStart.md](OrchardQuickStart.md) for overview
2. Check [OrchardImplementationStatus.md](OrchardImplementationStatus.md) for current status
3. See [OrchardRustCppInterface.md](OrchardRustCppInterface.md) for FFI details
4. Review [ORCHARD_MANUAL_TESTING.md](ORCHARD_MANUAL_TESTING.md) for testing

### For Users
1. Wait for automatic note decryption implementation (final 10%)
2. Wallet persistence will enable saving keys between restarts
3. Full production release pending final wallet features

---

## References

### External Documentation
- [Zcash Orchard Specification](https://zips.z.cash/protocol/protocol.pdf)
- [Halo 2 Book](https://zcash.github.io/halo2/)
- [ZIP-317: Fee Structure](https://zips.z.cash/zip-0317)

### Source Code
- Rust Module: [orchard-postfiat/](../../orchard-postfiat/)
- C++ Integration: [src/xrpld/app/tx/detail/ShieldedPayment.cpp](../../src/xrpld/app/tx/detail/ShieldedPayment.cpp)
- RPC Handlers: [src/xrpld/rpc/handlers/](../../src/xrpld/rpc/handlers/)
- Tests: [src/test/rpc/OrchardFullFlow_test.cpp](../../src/test/rpc/OrchardFullFlow_test.cpp)

---

*Last updated: December 26, 2024*
*Implementation status: ~90% complete*
