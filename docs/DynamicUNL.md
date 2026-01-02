# Dynamic UNL (Validator List) Feature

## Overview

The Dynamic UNL feature enables automatic updates to the Unique Node List (UNL) without requiring code changes or node restarts. It combines the convenience of off-chain hosting with the security of on-chain verification.

## How It Works

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Dynamic UNL Update Flow                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. Foundation publishes UNL JSON to HTTPS endpoint                         │
│     └──> https://postfiat.org/index.json                                    │
│                                                                             │
│  2. Foundation sends tx with hash in memo                                   │
│     └──> From: master_account  To: memo_account                             │
│     └──> Memo: {"hash": "...", "effectiveLedger": 12800, "sequence": 42}    │
│                                                                             │
│  3. Nodes process the hash transaction                                      │
│     └──> UNLHashWatcher validates and stores pending update                 │
│                                                                             │
│  4. Nodes fetch UNL from URL                                                │
│     └──> ValidatorSite downloads and parses JSON                            │
│     └──> Computes sha512Half hash                                           │
│     └──> Verifies against on-chain hash                                     │
│                                                                             │
│  5. At flag ledger (every 256 ledgers)                                      │
│     └──> Pending update becomes active                                      │
│     └──> New UNL takes effect deterministically                             │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Configuration

Add the following sections to your node's configuration file:

```ini
# URL(s) to fetch the validator list from
[validator_list_sites]
https://postfiat.org

# Publisher's master public key (for signature verification AND on-chain tx)
# The on-chain sender account is automatically derived from this key
[validator_list_keys]
ED2677ABFFD1B33AC6FBC3062B71F1E8397C1505E1C42C64D11AD1B28FF73F4734

# On-chain hash publisher destination account
[unl_hash_publisher]
memo_account=rPT1Sjq2YGrBMTttX4GZHjKu9dyfzbpAYe
```

### Configuration Parameters

| Parameter | Description |
|-----------|-------------|
| `validator_list_sites` | HTTPS URL(s) where the UNL JSON is hosted |
| `validator_list_keys` | Publisher's master public key (hex). The on-chain sender account is derived from this key. |
| `memo_account` | Destination account for hash transactions |

## On-Chain Memo Format

The hash publication transaction must include a memo with the following JSON structure:

```json
{
  "hash": "A1B2C3D4...",
  "effectiveLedger": 12800,
  "sequence": 42,
  "version": 1
}
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `hash` | string | 64-character hex string (sha512Half of UNL JSON) |
| `effectiveLedger` | uint32 | Flag ledger sequence when update takes effect |
| `sequence` | uint32 | Monotonically increasing counter (prevents replays) |
| `version` | uint32 | Format version (currently 1) |

## Dynamic UNL JSON Format (Score-Based Selection)

When using the score-based validator selection feature, publishers host a JSON file with validators and their scores. The `DynamicUNLManager` component processes this data and selects the top validators by score.

### JSON Format

```json
{
  "validators": [
    {"pubkey": "ED7A82...", "score": 95},
    {"pubkey": "ED3B91...", "score": 92},
    {"pubkey": "EDF4C2...", "score": 88},
    {"pubkey": "ED1D5A...", "score": 85},
    {"pubkey": "ED9E7F...", "score": 78}
  ],
  "version": 1
}
```

### Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `validators` | array | Yes | Array of validator objects |
| `validators[].pubkey` | string | Yes | Hex-encoded Ed25519 public key (e.g., "ED...") |
| `validators[].score` | uint32 | Yes | Score (higher = better). Used for ranking validators. |
| `version` | uint32 | Yes | Format version for future compatibility (currently 1) |

### Validator Selection

The `DynamicUNLManager` selects validators based on their scores:

1. **Sorting**: Validators are sorted by score in descending order (highest first)
2. **Selection**: The top N validators are selected, where N = `MAX_UNL_VALIDATORS`
3. **Maximum**: Currently, `MAX_UNL_VALIDATORS = 35`

```cpp
// TODO: Research optimal UNL size for network security, decentralization and performance
static constexpr std::uint32_t MAX_UNL_VALIDATORS = 35;
```

### Processing Flow

```
Publisher JSON → sha512Half → Hash published on-chain
                     ↓
Node fetches JSON from HTTP
                     ↓
DynamicUNLManager.parseUNLData(json)
                     ↓
DynamicUNLManager.verifyHash(via UNLHashWatcher)
                     ↓
DynamicUNLManager.selectTopValidators()
                     ↓
Top N validators applied to UNL
```

### Example

Given 5 validators with scores:
- `ED_VALIDATOR_D` = 95
- `ED_VALIDATOR_B` = 92
- `ED_VALIDATOR_E` = 88
- `ED_VALIDATOR_A` = 85
- `ED_VALIDATOR_C` = 78

If `MAX_UNL_VALIDATORS = 3`, the selected UNL would be:
1. `ED_VALIDATOR_D` (score 95)
2. `ED_VALIDATOR_B` (score 92)
3. `ED_VALIDATOR_E` (score 88)

## Amendment

This feature is controlled by the `DynamicUNL` amendment:
- **Feature Name**: `featureDynamicUNL`
- **Default Vote**: Yes
- **Location**: `include/xrpl/protocol/detail/features.macro`

The feature will only function when the amendment is enabled on the network.

## Security Considerations

### Signature Verification
The UNL JSON is cryptographically signed by the publisher's key. Nodes verify this signature before accepting the list.

### On-Chain Hash Verification
Even with a valid signature, nodes verify that the UNL's hash matches the hash published on-chain. This ensures:
- The UNL was explicitly authorized by the on-chain transaction
- No man-in-the-middle attacks on the HTTPS fetch
- Deterministic agreement on which UNL version is active

### Monotonic Sequence
The sequence number must always increase. This prevents:
- Replay attacks using old valid transactions
- Rollback to previous UNL versions

### Flag Ledger Application
Updates only take effect at flag ledgers (every 256 ledgers). This ensures:
- All nodes apply updates at the same ledger
- Deterministic network-wide behavior
- Time for nodes to fetch and verify the new UNL

### Key Compromise Mitigation
If the publisher key is compromised:
1. The existing manifest revocation system can invalidate the key
2. Nodes can be updated with a new trusted publisher key
3. Consider using a multi-sig publisher account for enhanced security

## Troubleshooting

### UNL Not Updating

1. **Check amendment status**: Ensure `DynamicUNL` amendment is enabled
   ```
   $ ./rippled feature DynamicUNL
   ```

2. **Check configuration**: Verify all sections are properly configured
   ```
   $ ./rippled server_info
   ```

3. **Check logs**: Look for `UNLHashWatcher` and `ValidatorSite` messages
   ```
   grep -E "UNLHashWatcher|ValidatorSite" debug.log
   ```

### Hash Mismatch Errors

If you see "on-chain hash mismatch" errors:
- The fetched UNL doesn't match the on-chain hash
- Check if the HTTPS endpoint has the correct version
- Verify the hash transaction is from the correct publisher

### Sequence Rejection

If updates are being rejected due to sequence:
- Ensure the publisher is using monotonically increasing sequences
- Check `UNLHashWatcher: Rejecting update with sequence` in logs

## File Reference

| File | Purpose |
|------|---------|
| `src/xrpld/app/misc/UNLHashWatcher.h` | UNLHashWatcher class declaration |
| `src/xrpld/app/misc/UNLHashWatcher.cpp` | UNLHashWatcher implementation |
| `src/xrpld/app/misc/DynamicUNLManager.h` | DynamicUNLManager class declaration (score-based selection) |
| `src/xrpld/app/misc/DynamicUNLManager.cpp` | DynamicUNLManager implementation |
| `src/xrpld/app/misc/detail/ValidatorSite.cpp` | On-chain hash verification and Dynamic UNL integration |
| `src/xrpld/app/ledger/detail/BuildLedger.cpp` | Flag ledger application |
| `src/xrpld/app/main/Application.cpp` | Component initialization |
| `src/xrpld/core/ConfigSections.h` | Config section definitions |
| `include/xrpl/protocol/detail/features.macro` | Amendment definition |
| `src/test/app/DynamicUNL_test.cpp` | Integration tests for Dynamic UNL feature |


