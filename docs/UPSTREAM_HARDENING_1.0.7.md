# Upstream hardening backports (1.0.7, testnet/devnet)

1.0.5 and 1.0.6 closed the manifest flood (`MANIFEST_FLOOD_HOTFIX.md`). Our
code line is otherwise still XRP Ledger 3.1.2 (merged 2026-03-23). Between
3.1.2 and 3.3.0 upstream shipped a set of small fixes of the same family: a
peer sends malformed or oversized input and the node crashes, wastes memory,
or relays junk. 1.0.7 carries those fixes over one by one, adapted to our
names and layout, without the full upstream merge (772 commits, the rippled
to xrpld rename and the module moves), which stays a separate project.

Each entry names the upstream commit as it appears in the XRPLF/rippled
repository. Everything from 1.0.5 and 1.0.6 is unchanged.

## What 1.0.7 changes

| Fix | Upstream | What it prevents |
|-----|----------|------------------|
| Oversized ping frames rejected at the header (`maximumPingMessageSize`, 1 KiB) | `6c793edf7` (3.3.0) | A peer sends a huge "ping" and the node allocates and decompresses up to 64 MiB for it. Only the ping type is affected; other messages keep the general frame limit. |
| Validations must arrive in canonical field order | `7d3611df2` (3.3.0) | The relay suppression key is the hash of the received bytes. A validation re-encoded with reordered fields hashed differently and was relayed again as new, so one signed validation could be turned into a flood. `STValidation::DeserializeOptions` replaces the old `bool checkSignature` argument; peer input passes `requireCanonicalOrder = true`, and a non-canonical validation is charged `feeInvalidData` and dropped. |
| Close-time offset from a weighted median | `981c25693` (3.3.0) | The node's clock offset came from the mean of peer close times, so a minority of peers reporting far-off times moved it. `medianCloseOffset` in `ConsensusTypes.h` takes the lower weighted median instead. Ties among close-time votes now resolve to the earlier time (`Consensus::updateOurPositions`). |
| Malformed ledger replay responses handled | `4a9ee54c8` (3.3.0) | A truncated ledger header or a bad SHAMap node in a `TMProofPathResponse` or `TMReplayDeltaResponse` threw out of the message handler. Both handlers now return `ReplayMsgStatus`: `BadData` (the peer said it cannot answer, `feeInvalidData`) or `Malformed` (no honest peer sends this, new `feeMalformedData`, 2000). Hash and key fields are also checked for their exact size before use; our 3.1.2 code did not check them. |
| Lock when reading a peer's closed ledger hash | `d60955e2f` (3.3.0) | `Peer::getClosedLedgerHash` returned a reference to a field written under `recentLock_` by another thread. It now returns a copy taken under the lock. |
| Nodes missing from both backends re-stored during online_delete | `a0fd1cce5`, #7763 (3.3.0) | During the online_delete rotation a node reachable from the validated state map could be present in neither backend (its only copy was in an archive removed earlier, and clean nodes are never rewritten). `SHAMapStoreImp::copyNode` now re-stores such a node from memory, and while a rotation is in flight every archive-served read is copied forward into the writable backend (`DatabaseRotating::setRotationInFlight`). Our validators run `online_delete`, so this is a resync-avoidance fix for them. |
| JSON array size limit in requests | `377b155dd` (3.1.3) | A single RPC request could carry arbitrarily large arrays (`Indexes`, `Paths`, `Memos`, ...). Arrays over `maxSTParsedJSONArraySize` (512) elements per field are refused with `invalidParams` before parsing. Public RPC and WebSocket nodes are the ones exposed. |
| Peer endpoint verification with stricter public-address rules (`[overlay] verify_endpoints`, default 1) | `f511eeb27` (3.1.3) | Addresses learned from peers are checked before entering the peer table. `is_public` now also excludes CGNAT (100.64/10), link-local, documentation, benchmarking and reserved IPv4 ranges, and loopback, link-local, ULA, 6to4, Teredo and documentation IPv6 ranges. Setting `verify_endpoints = 0` disables the check and logs a warning; only for local test networks. |

Left out on purpose: the get-object-by-hash limits (1.0.5 has its own),
upstream's manifest cache changes (1.0.6 has its own design), the websocket
subscription cleanup rework (1,300 lines, RPC nodes only, a later candidate),
and everything tied to features that are off on our networks (Lending,
Vaults, Permissioned Domains, MPT, Batch, Delegation).

## Behaviour to be aware of

- Every rippled and postfiatd version serializes validations canonically, so
  the canonical-order requirement drops nothing from honest peers of any
  version. It only stops replayed re-encodings.
- A 1.0.4 peer advertising an endpoint in one of the newly excluded ranges
  (for example a CGNAT address) is no longer added to the peer table. Public
  hosts are unaffected.
- IPv6 peers become reachable for the first time. The old IPv6 `is_private`
  test was wrong and classified every global-unicast address as private, so
  IPv6 endpoints learned from peers were always dropped and an IPv6
  `[overlay] public_ip` was rejected. With the corrected test an IPv6
  endpoint enters the peer table and can be dialled like an IPv4 one.
- The online_delete rescue logs `copyNode: re-stored node missing from both
  backends` at warning level when it fires, and `Rotating: copied forward N
  archive-served reads` at the end of a rotation. Both are expected to be
  rare; frequent occurrences point at an unhealthy store.
- No protocol message format changes, no amendment, no config change
  required. `verify_endpoints` is documented in `cfg/postfiatd-example.cfg`.

## Verification

Build with tests enabled, then run the 1.0.5/1.0.6 suites plus:

```sh
postfiatd --unittest=ripple.overlay.manifest_flood      # adds the ping limit case
postfiatd --unittest=ripple.protocol.STValidation       # canonical field order
postfiatd --unittest=ripple.protocol.STObject
postfiatd --unittest=ripple.protocol.STParsedJSON       # array size limit
postfiatd --unittest=ripple.consensus.Consensus         # median close offset
postfiatd --unittest=ripple.app.LedgerReplay            # runs LedgerReplayer too: malformed sizes, truncated header
postfiatd --unittest=ripple.app.SHAMapStore
postfiatd --unittest=ripple.overlay.reduce_relay
postfiatd --unittest=ripple.peerfinder.PeerFinder
postfiatd --unittest=beast.beast.IPEndpoint             # reserved address ranges
```

The PR check runs the same list. The online_delete rescue has no unit test;
upstream shipped it without one as well. Its behaviour is observable through
the two log lines above on a node that runs `online_delete`.

## Rollout

Same gates as 1.0.6: green CI on the exact head, image digests recorded, a
fresh non-validator started against the live hubs (it must reach `full`, keep
its peers, and show no `Validation: Exception` warnings from honest peers),
then hubs one at a time with 1.0.6 as the rollback target, then validators.
Nothing here needs a second restart or a wallet backup.
