# Historical Orchard Documentation

This directory contains archived documentation from the Orchard privacy implementation phases. These documents are preserved for historical reference but are **no longer current**.

## Archived Documents

### OrchardPrivacyAmendment.md (Phase 1)
Early documentation of the OrchardPrivacy amendment definition. This phase defined the basic transaction type and amendment structure.

**Status:** Completed November 2025

### OrchardPhase2Complete.md
Documents the completion of the Rust/C++ FFI bridge skeleton with stub implementations.

**Status:** Completed November 2025 - All stubs have been replaced with real Orchard cryptography

### OrchardPhase3Plan.md
Planning document for implementing real Orchard cryptography including Halo2 proofs, note encryption, and bundle building.

**Status:** Completed - Real cryptography is now fully implemented and tested

### OrchardPhase4Complete.md
Documents the initial ShieldedPayment transactor implementation with basic validation.

**Status:** Completed November 2025 - Transaction processing has been substantially enhanced since

## Current Documentation

For up-to-date Orchard implementation status, please see:

- **[OrchardImplementationStatus.md](../OrchardImplementationStatus.md)** - Master status document (~90% complete)
- **[OrchardQuickStart.md](../OrchardQuickStart.md)** - Quick reference guide
- **[ORCHARD_WALLET_INTEGRATION.md](../ORCHARD_WALLET_INTEGRATION.md)** - Current wallet integration status

## Why These Were Archived

These documents described:
- Stub/placeholder implementations that have been replaced
- Planning for work that is now complete
- Early phase milestones that are now superseded

The information they contain is still technically accurate for the codebase state at the time they were written, but they can be confusing when mixed with current documentation since they describe an earlier, incomplete implementation.

---

*Last updated: December 26, 2024*
