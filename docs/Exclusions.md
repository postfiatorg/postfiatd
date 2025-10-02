# Validator Exclusion List System Documentation

## Overview

The Validator Exclusion List system allows validators to exclude specific accounts from participating in the network. This is a consensus-based mechanism where an account is only excluded if a sufficient percentage of validators (67% by default) agree to exclude it. The system supports two methods for managing exclusion lists:

1. **Static Configuration** - Exclusions defined in the validator's configuration file
2. **Remote Fetching** - Dynamic exclusion lists fetched from remote sources and cryptographically verified

## System Architecture

### Key Components

1. **ExclusionManager** - Core component that maintains the consensus view of excluded accounts
2. **ValidatorExclusionManager** - Manages a validator's own exclusion list and rate-limits changes
3. **RemoteExclusionListFetcher** - Fetches and verifies signed exclusion lists from remote sources
4. **AccountSet Transaction** - On-chain mechanism for validators to update their exclusion lists
5. **exclusion_info RPC** - Query interface to inspect current exclusion states

## Method 1: Configuration File-Based Exclusions

### Configuration Format

Add exclusions directly to your validator's configuration file:

```ini
[validator_exclusions]
# List of account addresses to exclude
# One address per line
rN7n7otQDd6FczFgLdSqtcsAUxDkw6fzRH
rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn
rJ6Bq42segvi7djJmt3QV3cRaCNnfVvTaS
```

### How It Works

1. On startup, the validator reads the `[validator_exclusions]` section
2. These addresses are stored in `ValidatorExclusionManager`
3. When the first ledger is validated, the manager compares the configured list with the on-chain state
4. Differences are queued as pending changes
5. Changes are applied gradually (one every 10 ledgers) to avoid network disruption

### Characteristics

- **Persistent**: Remains in effect until manually changed
- **Simple**: No external dependencies or signatures required
- **Static**: Requires restart to update
- **Individual**: Each validator manages their own list

## Method 2: Remote Fetcher with Signed Lists

### Configuration

Configure remote sources in the validator configuration file:

```ini
[validator_exclusions_sources]
# Format: url|public_key
https://example.com/exclusions.json|nHUeeJCSY2dM71oxM8Cgjouf5ekTuev2mwDpc374aLMxzDLXNmjf
https://backup.com/list.json|nHB9CJAWyB4rj91VRWn96DkukG4bwdtyTh5VKm3mxZrMT5jST5

[validator_exclusions_interval]
# How often to fetch updates (in seconds)
300  # Default: 5 minutes
```

### Remote List JSON Format

```json
{
  "version": "1.0",
  "timestamp": "2024-01-15T10:30:00Z",
  "issuer_address": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
  "exclusions": [
    {
      "address": "rN7n7otQDd6FczFgLdSqtcsAUxDkw6fzRH",
      "reason": "Malicious activity detected",
      "date_added": "2024-01-10"
    },
    {
      "address": "rf1BiGeXwwQoi8Z2ueFYTEXSwuJYfV2Jpn",
      "reason": "Spam transactions",
      "date_added": "2024-01-12"
    }
  ],
  "signature": {
    "algorithm": "ed25519",
    "public_key": "nHUeeJCSY2dM71oxM8Cgjouf5ekTuev2mwDpc374aLMxzDLXNmjf",
    "signature": "3045022100a1b2c3d4e5f6..."
  }
}
```

### How Remote Fetching Works

1. **Startup**: `RemoteExclusionListFetcher` starts when the validator initializes
2. **Periodic Fetching**: Lists are fetched every `validator_exclusions_interval` seconds
3. **Verification**: Each fetched list is cryptographically verified using the configured public key
4. **Consolidation**: All verified lists are combined into a single exclusion set
5. **Update Detection**: When changes are detected, they're queued for gradual application
6. **Reason Storage**: Exclusion reasons are stored in memory (not on-chain) and available via RPC

### Security Features

- **Cryptographic Verification**: All lists must be signed with Ed25519 keys
- **All-or-Nothing Initial Fetch**: On first startup, ALL configured sources must be accessible
- **Partial Updates Allowed**: After initial fetch, system continues with available sources
- **Signature Validation**: Invalid signatures cause the list to be rejected

## Python Script for Creating Signed Exclusion Lists

### Script: `sign_exclusion_list.py`

Located at: `/scripts/sign_exclusion_list.py`

### Prerequisites

```bash
pip install nacl base58
```

### Usage

```bash
python3 sign_exclusion_list.py \
  --secret-key YOUR_ED25519_SECRET_KEY_HEX \
  --input exclusions.json \
  --output signed_exclusions.json
```

### Input Format (exclusions.json)

```json
{
  "version": "1.0",
  "timestamp": "2024-01-15T10:30:00Z",
  "issuer_address": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
  "exclusions": [
    {
      "address": "rN7n7otQDd6FczFgLdSqtcsAUxDkw6fzRH",
      "reason": "Security violation",
      "date_added": "2024-01-10"
    }
  ]
}
```

### How the Script Works

1. **Reads** the input JSON file containing the exclusion list
2. **Creates** a canonical message for signing:
   - Format: `v1:version:timestamp:issuer_address:sorted_addresses`
   - Addresses are sorted alphabetically for consistency
3. **Signs** the message using Ed25519 with your secret key
4. **Derives** the public key from the secret key
5. **Encodes** the public key in XRPL format (Base58 with 'n' prefix)
6. **Adds** the signature to the JSON
7. **Writes** the signed JSON to the output file

### Key Generation

To generate an Ed25519 key pair for signing:

```python
import nacl.signing
import nacl.encoding

# Generate a new signing key
signing_key = nacl.signing.SigningKey.generate()

# Get the secret key in hex (32 bytes)
secret_key_hex = signing_key.encode(encoder=nacl.encoding.HexEncoder).decode('ascii')
print(f"Secret key (keep private): {secret_key_hex}")

# The public key will be automatically derived and encoded by the script
```

## Consensus Mechanism

### How Exclusion Consensus Works

1. **Vote Collection**: Each validator maintains their own exclusion list on-chain
2. **Vote Counting**: The ExclusionManager counts how many validators exclude each address
3. **Threshold Calculation**: Default threshold is 67% (minCONSENSUS_PCT)
4. **Consensus Determination**: An address is considered excluded if votes >= threshold
5. **Real-time Updates**: The consensus view updates as validators change their lists

### Rate Limiting

To prevent network disruption, changes are rate-limited:
- **One change per 10 ledgers**: Maximum rate of updates
- **Gradual application**: Large list changes are queued and applied over time
- **Add before remove**: When replacing lists, additions happen before removals

## RPC Interface: exclusion_info

### Query All Validators

```bash
curl -X POST http://localhost:5005 -d '{
  "method": "exclusion_info",
  "params": [{}]
}'
```

### Query Specific Validator

```bash
curl -X POST http://localhost:5005 -d '{
  "method": "exclusion_info",
  "params": [{
    "validator": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
  }]
}'
```

### Response Format

```json
{
  "validators": {
    "rValidator1": {
      "exclusion_list": [
        {
          "address": "rExcludedAccount1",
          "reason": "Malicious activity",
          "date_added": "2024-01-10"
        }
      ],
      "exclusion_count": 1
    }
  },
  "excluded_accounts": {
    "rExcludedAccount1": {
      "exclusion_count": 5,
      "percentage": 71,
      "meets_threshold": true,
      "reason": "Malicious activity",
      "date_added": "2024-01-10"
    }
  },
  "total_validators": 7,
  "consensus_threshold": 5,
  "consensus_percentage": 67
}
```

## Operational Workflow

### Setting Up Exclusion Lists

1. **Choose Method**:
   - Use configuration file for simple, static lists
   - Use remote fetcher for dynamic, managed lists

2. **For Configuration File**:
   ```ini
   [validator_exclusions]
   rBadAccount1
   rBadAccount2
   ```

3. **For Remote Lists**:
   - Set up a web server to host the JSON file
   - Create and sign the exclusion list using the Python script
   - Configure validators to fetch from your server
   - Update the list as needed and re-sign

### Best Practices

1. **Security**:
   - Keep signing keys secure and never expose them
   - Use HTTPS for remote list hosting
   - Regularly rotate signing keys

2. **Coordination**:
   - Coordinate with other validators before major exclusions
   - Document reasons for exclusions
   - Consider the consensus threshold when planning exclusions

3. **Monitoring**:
   - Regularly check `exclusion_info` RPC to verify exclusions
   - Monitor logs for fetch failures or signature errors
   - Set up alerts for consensus threshold changes

4. **Updates**:
   - Plan exclusion list updates during low-traffic periods
   - Test changes on testnet first
   - Keep backup sources for remote lists

## Troubleshooting

### Common Issues

1. **Reasons not showing in RPC**:
   - Verify remote lists are being fetched successfully
   - Check that the JSON includes reason fields
   - Ensure the RemoteExclusionListFetcher is running
   - Look for "Updated ExclusionManager with X exclusion reasons" in logs

2. **Remote lists not updating**:
   - Check network connectivity to source URLs
   - Verify signature is valid
   - Ensure public key in config matches signing key
   - Check logs for fetch errors

3. **Exclusions not taking effect**:
   - Verify consensus threshold is met
   - Check that enough validators are excluding the account
   - Use `exclusion_info` RPC to see vote counts

### Log Messages to Monitor

```
RemoteExclusionListFetcher: Successfully fetched and verified from <url>
ValidatorExclusionManager: Updated ExclusionManager with X exclusion reasons
ExclusionManager: Rebuilding exclusion cache from ledger
ValidatorExclusionManager: X pending changes queued
```

## Architecture Details

### Data Flow

```
Configuration File ─┐
                    ├─> ValidatorExclusionManager ─> AccountSet TX ─> Ledger
Remote Sources ─────┘                                                    │
     │                                                                   │
     └─> ExclusionManager <──────────────────────────────────────────────┘
              │
              └─> exclusion_info RPC
```

### Storage

- **On-chain**: Account addresses only (no reasons or metadata)
- **In-memory**: Reasons, dates, and other metadata from remote sources
- **Configuration**: Static exclusion lists and remote source URLs

## Security Considerations

1. **No On-chain Reasons**: Exclusion reasons are never stored on-chain to prevent legal issues
2. **Signature Verification**: All remote lists must be cryptographically signed
3. **Consensus Requirement**: No single validator can exclude an account
4. **Rate Limiting**: Prevents rapid changes that could destabilize the network
5. **All-or-Nothing Initial Fetch**: Ensures consistency on startup
