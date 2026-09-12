# Manifest flood hotfix (1.0.5 testnet/devnet)

This is a network-ingress/peer-framing fix, not a consensus amendment.
Do not consider the incident resolved until upgraded nodes can form new
connections, validators are current, and the affected fleet has been upgraded.

## Changes

- Validator manifests received through the overlay must have an exactly listed
  master key before signature verification or cache insertion. The local
  configured validator is included in that list. Publisher manifests continue
  through the separate authenticated validator-list path.
- Incoming batches are limited to 500 entries and 512 KiB; individual serialized
  manifests are limited to 4 KiB. Oversized batches are rejected before queuing
  manifest jobs. Unlisted/invalid batches incur resource charges, enforced on
  the peer strand. Warnings identify the delivering node key and endpoint,
  which identifies a relay, not necessarily the original attacker.
- Initial sync looks up only currently listed keys and splits on both entry
  count and encoded byte size. It does not scan or serialize the full cache.
  Validator-list membership changes immediately affect subsequent syncs.
- Outbound frames at or above 64 MiB throw before allocation or serialization.
  Incoming normal frames have the matching exclusive size limit.
- For recovery from poisoned older peers, a connection can discard ONE
  oversized manifest frame (uncompressed or LZ4 legacy framing), bounded to
  128 MiB of declared wire and uncompressed length and a 60-second drain
  deadline. It streams past the frame using the normal small read buffer;
  it never decompresses, verifies, stores, or relays its contents. A repeated
  oversized dump, excessive length, or expired active drain is rejected.
  Other message types retain the strict framing parser.
- Invalid-header warnings include the first eight available bytes and peer
  identity. Legacy-discard warnings report the declared byte counts.

## Revocation retention: no automatic destructive cleanup

The old shutdown path saves **all revocations**, not just listed-validator
manifests. A clean restart can therefore reload a poisoned cache.

Do not erase all unlisted revocations on startup or validator-list changes:
that can forget genuine revocations during list expiry/removal and allow old
keys/manifests to be accepted if they are listed again. This hotfix prevents
legacy entries from being transmitted or expanded by unlisted peer input, but
does not promise to remove historical junk from disk or memory.

Any destructive cleanup needs an independently reviewed classification and a
wallet database backup. Retain legitimate revocations and validator/node keys.
Do not delete wallet.db as a recovery procedure.

## Verification

Build with tests enabled, then run:

```sh
postfiatd --unittest=ripple.overlay.manifest_flood
postfiatd --unittest=ripple.app.Manifest
postfiatd --unittest=ripple.app.ValidatorList
postfiatd --unittest=ripple.overlay.compression
```

The regression suite covers batch-count/byte limits, a 300,000-entry batch
using a repeated valid revocation, filtering a legacy cache, chunked listed
sync, key rotation/revocation preservation, a changed list with an unchanged
cache, the 64 MiB framing boundary, fragmented headers, uncompressed/compressed
legacy drains, a subsequent ping frame, repeated dumps, size limits and timeout.
The repeated-entry test is not a 300,000-distinct-key stress test.

Before restarting any existing validator, run a fresh **non-validator**
candidate with empty state and no publicly exposed admin port. It must reach
`full` against the existing unpatched fleet, maintain peer connections, and
follow advancing validated ledgers. Verify that the legacy-discard warnings
stop after initial connection and that malformed-header disconnects do not
continue. This mixed-version check is essential; two clean patched peers
alone do not prove recovery from the incident.

## Local verification (2026-09-12 UTC)

- Release build, assertions disabled: five focused suites, **22,102 checks,
  zero failures** (manifest flood 17,448; Manifest 309; ValidatorList 4,234;
  compression 104; ResourceManager 7).
- An untouched v1.0.4 source build reproduced the same 50 ValidatorList failures
  as the initial patched build: inherited assertions used upstream's 80%
  instead of this release line's already-shipped 67% quorum. The test-only
  correction also preserves the existing 60% floor. Production
  `ValidatorList.cpp` is unchanged.
- Fresh non-validator candidate reached `full` against unpatched 1.0.4 hubs.
  Each sent an **86,682,074-byte** uncompressed legacy manifest frame; the
  candidate discarded it without losing the connection.
- At 14:23:42 UTC: `full`, two peers, validated ledger 6,243,859, age three
  seconds, quorum 13. These are canary results, not a completed fleet rollout.
- A separate 1.0.4 container, explicitly connected after freeing the canary's
  per-IP peer slot, activated and then failed with
  `No message of desired type` at 14:23:48 UTC.
- Canary networking was deliberately private (`peer_private=1`), which disables
  automatic connections. Test connections were made through the local admin
  `connect` RPC; no admin or peer port was published on the host.

## Rollout

1. Preserve incident logs and a consistent wallet database backup before
   replacement. The existing entrypoint can truncate logs above 500 MB.
2. Record the exact source commit, binary SHA-256 and image digest. Do not
   confuse an image tag with the binary actually running.
3. Canary a fresh non-validator as above. Then update one testnet hub at a
   time, checking ledger freshness, proposing/full state, stable peers and
   remaining quorum after each change. Stop the rollout if those checks fail.
4. Verify a new external connection to each upgraded hub. Upgrade isolated
   validators next, then the remaining fleet. Preserve enough active
   validators throughout; never restart all hubs/validators together.
5. Monitor all UNL validators, not only hub/RPC health. One recovered node or
   a pushed commit does not close the P0.

The testnet and devnet 1.0.4 code lines can take this 1.0.5 patch. The separate
mainnet branch identifies as 2.5.1 and has extensive unrelated differences:
backport and test the relevant changes on that branch. Do not deploy the
testnet/devnet binary to mainnet simply by swapping configuration files.
