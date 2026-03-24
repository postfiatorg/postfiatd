# Upstream Merge Analysis: rippled 3.0.0 → 3.1.2

**Date:** 2026-03-23
**Current postfiatd base:** 3.0.0 (commit `46167f7c54`, tag `upgrade-checkpoint-3.0.0`)
**Target:** 3.1.2 (tag `3.1.2`)
**Total upstream commits:** 21

---

## Commit Analysis

### 3.1.0 (14 commits)

| # | Commit | Description | Category | Recommendation | Rationale |
|---|--------|-------------|----------|----------------|-----------|
| 1 | `8990c45c40` | Set version to 3.1.0-b1 | Version bump | **Override** | PostFiat maintains its own version string in BuildInfo.cpp. Upstream version bumps are overridden automatically. |
| 2 | `d25915ca1d` | Fix: Reorder Batch Preflight Errors | Bug fix | **Include** | Fixes error ordering in Batch transactions. Low risk — 2 files (Batch.cpp + test). Note: Batch is later disabled in 3.1.1, but the code remains and the fix is valid for when Batch is re-enabled. |
| 3 | `f17e476f7a` | Fix: Inner batch transactions never have valid signatures | Bug fix + new amendment | **Include** | Introduces `fixBatchInnerSigs` amendment and hardens Batch transaction signature validation. Touches Transactor.cpp, apply.cpp, NetworkOPs.cpp, features.macro, tests. The amendment is disabled in 3.1.1 alongside Batch itself. |
| 4 | `418ce68302` | VaultClawback: Burn shares of an empty vault | Feature fix | **Include** | Fixes a stuck-vault scenario where a vault with 0 assets can't be deleted because users hold worthless shares. Touches VaultClawback.cpp, InvariantCheck.cpp/h, test. Important for protocol correctness. |
| 5 | `91d239f10b` | Improve and fix bugs in Lending Protocol (XLS-66) | Major bug fixes | **Include** | Largest commit (52 files, +3153/-709). Fixes overpayment asserts, overpayment calculations, management fee double-counting, LoanBrokerSet limits, permission checks, pseudo-account restrictions, vault asset cap enforcement, minimum grace period validation. Critical for protocol correctness. Touches lending/vault transaction processors, Credit.h/cpp, View.h/cpp, Flow.cpp, DirectStep.cpp, StrandFlow.h, XRPEndpointStep.cpp, applySteps.cpp, tests. |
| 6 | `1ead5a7ac1` | Expand Number to support full integer range | Core library | **Include** | Major refactor of the Number class (used throughout lending/vault math). Refactors internals from int64 to uint64+sign flag, adds semi-automatic rounding via `STTakesAsset`, introduces new `sMD_NeedsAsset` SField metadata. Touches Number.h/cpp, STNumber, STAmount, STObject, SField, STTakesAsset (new file), all loan/vault transactors, applySteps.cpp, tests. Required by commits 5 and 8. |
| 7 | `919ded6694` | Change LendingProtocol feature to Supported::yes | Amendment status | **Include** | Promotes LendingProtocol to supported. 1 file (features.macro). |
| 8 | `564a35175e` | Set version to 3.1.0-rc1 | Version bump | **Override** | Same as #1. |
| 9 | `ecfe43ece7` | Fix dependencies so clio can use libxrpl | Build fix | **Include** | Adds `#include <functional>` to Number.h. Net effect after revert+reapply (#10, #11): 1 line added. |
| 10 | `c894cd2b5f` | Revert "Fix dependencies so clio can use libxrpl" | Revert | **Include** | Reverted and reapplied — net zero with #9 and #11. Comes in as part of the tag merge automatically. |
| 11 | `8c573cd0bb` | Fix dependencies so clio can use libxrpl (re-applied) | Build fix | **Include** | Final state: `#include <functional>` present in Number.h. |
| 12 | `e3644f265c` | Fix: Remove DEFAULT fields that change to default in associateAsset | Bug fix | **Include** | Fixes STObject/STAmount/STTakesAsset behavior when DEFAULT fields are set to their default value during associateAsset. Prevents serialization issues. 5 files including tests. |
| 13 | `69dda6b34c` | Set version to 3.1.0-rc2 | Version bump | **Override** | Same as #1. |
| 14 | `d325f20c76` | Set version to 3.1.0 | Version bump | **Override** | Same as #1. |

### 3.1.1 (4 commits)

| # | Commit | Description | Category | Recommendation | Rationale |
|---|--------|-------------|----------|----------------|-----------|
| 15 | `6a5f269020` | Disable featureBatch and fixBatchInnerSigs amendments | Amendment rollback | **Include** | Upstream pulled back Batch support (Supported::yes → Supported::no) and disabled the new fixBatchInnerSigs. 1 file (features.macro). This is the final desired state for these amendments. |
| 16 | `a6d1e2cc7c` | CI: Update prepare-runner action for macOS | CI only | **Irrelevant** | PostFiat has its own CI workflows — this touches upstream-only YAML files (`reusable-build-test-config.yml`, `upload-conan-deps.yml`). Will merge cleanly with no impact. |
| 17 | `61481ff61d` | Set version to 3.1.1-rc1 | Version bump | **Override** | Same as #1. |
| 18 | `c5988233d0` | Set version to 3.1.1 | Version bump | **Override** | Same as #1. |

### 3.1.2 (3 commits)

| # | Commit | Description | Category | Recommendation | Rationale |
|---|--------|-------------|----------|----------------|-----------|
| 19 | `0e3600a18f` | Refactor: Improve exception handling | Code quality | **Include** | Replaces broad `catch(...)` with specific exception types, improves `try/catch` scoping across 14 files (AutoSocket.h, ApplyStateTable.cpp, ApplyView.cpp, OpenView.cpp, RawStateTable.cpp, View.cpp, Ledger.cpp, RPCCall.cpp, TransactionSign.cpp, LedgerEntry.cpp, Subscribe.cpp, tests). No functional change — purely defensive code quality. |
| 20 | `ecc58740d0` | Set version to 3.1.2-rc1 | Version bump | **Override** | Same as #1. |
| 21 | `3ba3fcff4c` | Set version to 3.1.2 | Version bump | **Override** | Same as #1. |

---

## Summary by Category

| Category | Count | Action |
|----------|-------|--------|
| Lending Protocol bug fixes | 3 (#4, #5, #12) | Include — critical protocol correctness |
| Core library (Number expansion) | 1 (#6) | Include — dependency for lending fixes |
| Batch transaction fixes | 2 (#2, #3) | Include — code stays, amendment disabled |
| Amendment status changes | 2 (#7, #15) | Include — LendingProtocol→supported, Batch→disabled |
| Exception handling refactor | 1 (#19) | Include — code quality, no functional change |
| Build fix (clio/libxrpl) | 3 (#9, #10, #11) | Include — net 1 line, harmless |
| CI (macOS build) | 1 (#16) | Irrelevant — PostFiat's own CI unaffected |
| Version bumps | 8 (#1,8,13,14,17,18,20,21) | Override — keep PostFiat version string |

**Substantive changes included:** 12 of 21 commits carry meaningful code changes.
**No commits excluded** — since we're merging the tag, all come in. The version bumps are overridden (we keep our own version), and the CI change is inert.

---

## Conflict Zone Analysis

### 1. `include/xrpl/protocol/detail/features.macro`

**PostFiat changes (3.0.0 → main):**
- Added `PF_AccountExclusion` and `PF_ValidatorVoteTracking` (between `AMMClawbackRounding` and `TokenEscrow`)
- Changed `EnforceNFTokenTrustlineV2` DefaultNo → DefaultYes
- Changed `AMMv1_3` DefaultNo → DefaultYes

**Upstream changes (3.0.0 → 3.1.2):**
- Added `fixBatchInnerSigs` (new line, after LendingProtocol)
- Changed `LendingProtocol` Supported::no → Supported::yes
- Changed `Batch` Supported::yes → Supported::no
- Changed `SingleAssetVault` Supported::no → Supported::yes

**Conflict risk:** Medium. Both sides modify lines near the top of the file. The PF_ features are inserted between existing lines that upstream also modifies.

**Resolution strategy:** Keep all PostFiat additions (PF_ features, DefaultYes overrides) AND apply all upstream changes (fixBatchInnerSigs, LendingProtocol→yes, Batch→no, SingleAssetVault→yes). No ID collisions — these are independent features.

### 2. `include/xrpl/protocol/detail/sfields.macro`

**PostFiat changes (3.0.0 → main):**
- Added `sfVoteCount` (UINT32, 201)
- Added `sfValidationHash` (UINT256, 201)
- Added `sfValidatorPublicKey` (VL, 201)
- Added `sfExclusionAdd` (ACCOUNT, 201)
- Added `sfExclusionRemove` (ACCOUNT, 202)
- Added `sfExclusionEntry` (OBJECT, 201)
- Added `sfExclusionList` (ARRAY, 201)

**Upstream changes (3.0.0 → 3.1.2):**
- Renamed `sfPreviousPaymentDate` → `sfPreviousPaymentDueDate` (UINT32, 57)
- Added `sMD_NeedsAsset | sMD_Default` metadata to 15 existing NUMBER fields (sfAssetsAvailable through sfManagementFeeOutstanding)

**Conflict risk:** Low. PostFiat's additions are at the end of each type section (field ID 201/202) while upstream modifies fields in the middle (IDs 2-17, 57). Different lines, different field IDs.

**Resolution strategy:** Keep all PostFiat field additions AND apply upstream's rename and metadata additions. Verify no field ID collisions (there are none — PostFiat uses 201/202, upstream modifies existing IDs 2-17 and 57).

### 3. `include/xrpl/protocol/detail/ledger_entries.macro`

**PostFiat changes (3.0.0 → main):**
- Added `sfExclusionList` to AccountRoot (soeOPTIONAL)
- Added `ltVALIDATOR_VOTE_STATS` at 0x008A with full definition

**Upstream changes (3.0.0 → 3.1.2):**
- Renamed `sfPreviousPaymentDate` → `sfPreviousPaymentDueDate` in ltLOAN definition

**Conflict risk:** Very low. PostFiat's changes are in AccountRoot and a new ledger entry at 0x008A. Upstream's change is in ltLOAN. Different objects, different lines.

**Resolution strategy:** Keep both. No overlap.

### 4. `src/libxrpl/protocol/BuildInfo.cpp`

**PostFiat:** Version string is `"3.0.0"`
**Upstream:** Version string goes from `"3.0.0"` → `"3.1.0-b1"` → ... → `"3.1.2"`

**Resolution:** Reject upstream version. Set to `"3.1.2"` or whatever you specify — needs your decision on whether to bump PostFiat's version to match upstream or keep a separate scheme.

---

## Files Modified by Upstream (No PostFiat Changes — Clean Merge Expected)

These files have no PostFiat-specific modifications since 3.0.0 and should merge without conflicts:

**Core library:**
- `include/xrpl/basics/Number.h` — Major refactor (uint64+sign flag, new ctors)
- `src/libxrpl/basics/Number.cpp` — Corresponding implementation
- `include/xrpl/protocol/STNumber.h`, `src/libxrpl/protocol/STNumber.cpp`
- `include/xrpl/protocol/STAmount.h`, `src/libxrpl/protocol/STAmount.cpp`
- `include/xrpl/protocol/STObject.h`, `src/libxrpl/protocol/STObject.cpp`
- `include/xrpl/protocol/STTakesAsset.h` (new file), `src/libxrpl/protocol/STTakesAsset.cpp` (new file)
- `include/xrpl/protocol/SField.h`, `include/xrpl/protocol/Protocol.h`
- `include/xrpl/protocol/IOUAmount.h`, `src/libxrpl/protocol/IOUAmount.cpp`
- `include/xrpl/protocol/Issue.h`, `src/libxrpl/protocol/Issue.cpp`
- `include/xrpl/protocol/MPTIssue.h`
- `include/xrpl/protocol/AmountConversions.h`
- `include/xrpl/protocol/SystemParameters.h`
- `src/libxrpl/protocol/Rules.cpp`

**Lending/Vault transaction processors:**
- `src/xrpld/app/tx/detail/LoanSet.cpp`, `LoanManage.cpp`, `LoanPay.cpp`, `LoanDelete.cpp`
- `src/xrpld/app/tx/detail/LoanBrokerSet.cpp`, `LoanBrokerSet.h`, `LoanBrokerDelete.cpp`
- `src/xrpld/app/tx/detail/LoanBrokerCoverDeposit.cpp`, `LoanBrokerCoverWithdraw.cpp`, `LoanBrokerCoverClawback.cpp`
- `src/xrpld/app/tx/detail/VaultCreate.cpp`, `VaultSet.cpp`, `VaultDelete.cpp`
- `src/xrpld/app/tx/detail/VaultDeposit.cpp`, `VaultWithdraw.cpp`, `VaultClawback.cpp`, `VaultClawback.h`
- `src/xrpld/app/tx/detail/InvariantCheck.cpp`, `InvariantCheck.h`
- `src/xrpld/app/tx/detail/applySteps.cpp`
- `src/xrpld/app/tx/detail/Transactor.cpp`

**Payment engine:**
- Credit.h/cpp, Flow.cpp, DirectStep.cpp, StrandFlow.h, XRPEndpointStep.cpp

**Batch transactions:**
- `src/xrpld/app/tx/detail/Batch.cpp`, `src/xrpld/app/tx/detail/apply.cpp`
- `src/xrpld/app/misc/NetworkOPs.cpp`

**Exception handling (3.1.2):**
- `include/xrpl/net/AutoSocket.h`
- `src/libxrpl/ledger/ApplyStateTable.cpp`, `ApplyView.cpp`, `OpenView.cpp`, `RawStateTable.cpp`, `View.cpp`
- `src/xrpld/app/ledger/Ledger.cpp`
- `src/xrpld/rpc/detail/RPCCall.cpp`, `TransactionSign.cpp`
- `src/xrpld/rpc/handlers/LedgerEntry.cpp`, `Subscribe.cpp`

**Tests (all clean merge — PostFiat has no overlapping test changes):**
- Vault_test.cpp, Batch_test.cpp, Number_test.cpp, STNumber_test.cpp
- Loan_test.cpp, LoanBroker_test.cpp, LendingHelpers_test.cpp
- AMM_test.cpp, AMMExtended_test.cpp, AMMClawback_test.cpp
- EscrowToken_test.cpp, MPToken_test.cpp, IOUAmount_test.cpp
- GetAggregatePrice_test.cpp, Config_test.cpp, reduce_relay_test.cpp, STParsedJSON_test.cpp

**CI (irrelevant to PostFiat):**
- `.github/workflows/reusable-build-test-config.yml`, `upload-conan-deps.yml`

---

## Risk Assessment

| Risk | Level | Mitigation |
|------|-------|------------|
| Macro file merge conflicts | Medium | Well-understood — 3 files with clear resolution strategies above |
| Number class refactor breaks PF code | Very Low | PostFiat-specific code (ValidatorVoteTracker, ExclusionManager) does not use the Number class |
| Lending/Vault changes affect PF transaction flow | Very Low | PostFiat's custom tx types (ValidatorVote, Exclusions) are independent of lending/vault code paths |
| applySteps.cpp conflict | Low | PostFiat has no changes to this file since 3.0.0 |
| BuildInfo.cpp version string | None | Manual override — deterministic |

---

## Decision Required

1. **Version string in BuildInfo.cpp:** Set to `"3.1.2"` to match upstream, or keep a separate PostFiat versioning scheme?
2. **Proceed with merge?** All 21 commits are substantive or harmless. No commits warrant exclusion.
