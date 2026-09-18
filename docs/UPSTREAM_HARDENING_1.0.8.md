# Upstream hardening backports (1.0.8, testnet/devnet)

1.0.7 carried over the peer-input hardening from XRP Ledger 3.1.3 to 3.3.0
(`UPSTREAM_HARDENING_1.0.7.md`). Upstream 3.4.0 (2026-09-17) shipped the
next batch of the same family. 1.0.8 carries over the five that apply to our
code line, which is otherwise still XRP Ledger 3.1.2, again one by one and
without the full upstream merge.

Each entry names the upstream commit as it appears in the XRPLF/rippled
repository. Everything from 1.0.5 to 1.0.7 is unchanged.

## What 1.0.8 changes

| Fix | Upstream | What it prevents |
|-----|----------|------------------|
| Variable-length field bound (`Serializer::maxVLLength`, 918,744) | `00eeb0a` (3.4.0) | A three-byte length header can state up to 929,984 bytes, but the encoder writes at most 918,744 and throws above that. A validation whose signature field sat in that gap decoded fine, and checking its signature re-serialized it inside `STValidation::isValid`, which is `noexcept`: the throw ended the process. Any peer could send one such message (about 920 KB, far below the frame limit) and stop every node that received it. The decoder now refuses the gap, and `isValid` catches and logs instead of terminating. |
| `TMGetLedger` node ID count capped at `hardMaxReplyNodes` (12,288) | `6099940` (3.4.0) | The reply loop stops only once the reply is full, so a request for nodes the server does not have cost one store lookup per ID, however many the message carried. Over the limit is charged `feeInvalidData` and dropped. |
| Unknown protobuf fields discarded after parsing | `0db7b76` (3.4.0) | Protobuf keeps fields it does not know and writes them back on re-serialization, so padding a peer added to a message was relayed to every other peer at no cost to the sender. |
| Fee for undeserializable transactions | `9aebb5e` (3.4.0), first half | A `TMTransaction` that fails to deserialize was logged but not charged, so junk cost the sender nothing. It now charges `feeInvalidData`. The second half of that commit, a cap on the `TMTransactions` batch, is not taken: tx reduce-relay is off in our configs and the handler rejects the message before the loop. |
| SHAMap node ID depth clamped | `#7941` (3.4.0) | `selectBranch` indexes the key byte at `depth / 2`; a depth-64 ID reads one byte past a 32-byte key. Node IDs come off the wire on the ledger data path. The constructor, `createID` and `selectBranch` now clamp to the tree, and `deserializeSHAMapNodeID` still refuses depths past 64. |

Left out on purpose:

- `eae0a35`, hardened hash on `STPathElement`: upstream's pathfinder
  deduplicates through a hash set keyed by a weak hash. Our 3.1.2 pathfinder
  still uses the linear `addUniquePath` check, so the collision problem does
  not exist here.
- `3e4e56d`, exception-safe `calculateBaseFee`: the only throwing fee
  calculation is `LoanPay`, which returns early when the loan does not exist.
  `LendingProtocol` is not enabled on any of our networks. Needed when it is.
- `#7940`, proof path checks: only reachable with `[ledger_replay]`, which
  is off in every config, and the peer handler rejects those messages when
  it is off.
- RPC input validation (`#7717`, `#7725`, `#7655`, `#7728`, `#7971`,
  `#7987`): the RPC layer catches exceptions and returns an error.
- Peer protocol version changes (`#7432`, `#6353`), the `fixCleanup3_4_0`
  and `LendingProtocolV1_1` amendments, and everything tied to features that
  are off on our networks.

## Behaviour to be aware of

- No protocol message format changes, no amendment, no config change
  required. A rejected oversized validation is logged by the 1.0.7 handler as
  `Validation: Exception, len>918744` and charged `feeInvalidData`; honest
  validations are about 200 bytes and never come near the bound.
- A peer asking for more than 12,288 node IDs in one `TMGetLedger` is logged
  as `TMGetLedger: Too many ledger node IDs`. Honest peers never do.
- Unknown-field padding no longer changes the bytes a node relays. Honest
  peers of any version send no unknown fields.

## Verification

Build with tests enabled, then run the 1.0.5 to 1.0.7 suites plus:

```sh
postfiatd --unittest=ripple.protocol.Serializer        # length bound, negative length
postfiatd --unittest=ripple.protocol.STValidation      # adds the oversized-field case
postfiatd --unittest=ripple.overlay.manifest_flood     # adds the unknown-field case
postfiatd --unittest=ripple.shamap.SHAMapNodeID        # depth clamp (clamp cases run without asserts)
```

The PR check runs the same list. The `TMGetLedger` cap and the transaction
fee have no unit test; both sit in `PeerImp` message handlers that the test
harness does not drive, and upstream shipped them with a harness we do not
have.

## Rollout

Same gates as 1.0.7: green CI on the exact head, image digests recorded, a
fresh non-validator started against the live hubs (it must reach `full`,
keep its peers, and show no `Validation: Exception` warnings from honest
peers), then hubs one at a time with 1.0.7 as the rollback target, then
validators. Nothing here needs a second restart, a wallet backup, or a config
change.
