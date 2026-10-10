# Upstream hardening backports (1.0.9, testnet/devnet)

1.0.8 carried over the peer-input hardening from XRP Ledger 3.4.0
(`UPSTREAM_HARDENING_1.0.8.md`). Upstream 3.4.1 is an emergency release:
binaries on 2026-09-25, source and the public disclosure on 2026-10-09
(https://xrpl.org/blog/2026/vulnerabilitydisclosurereport-bug-20261009). It
contains two fixes. 1.0.9 carries over the one that applies to our code line,
which is otherwise still XRP Ledger 3.1.2, again by hand and without the full
upstream merge.

Each entry names the upstream commit as it appears in the XRPLF/rippled
repository. Everything from 1.0.5 to 1.0.8 is unchanged.

## What 1.0.9 changes

| Fix | Upstream | What it prevents |
|-----|----------|------------------|
| Payment engine overflow (critical) | `578224f` (3.4.1) | The payment engine adds up the amounts of the offers a payment consumes with plain 64-bit arithmetic: the running totals in `flow()` (`StrandFlow.h`) and the per-step total in `BookStep.cpp`. A sender who places a few hundred specially priced offers and sends one payment through them can make the total wrap, so the sender is charged the wrapped amount while every offer owner is paid in full and native coin is created. The `XRPNotCreated` invariant counted with the same 64-bit accumulator and wrapped with it. Upstream says the bug may date back to 2015; it ships as a plain code change with no amendment. Now every strand and book total is checked (`checkedAdd` in `MathUtilities.h`, `checkedStepAdd` in `Steps.h`) and an overflow ends the strand as `tecPATH_DRY`; the invariant counts in 128 bits; the XRP branch of `accountSendMultiIOU` and the accumulators of `rippleSendMultiMPT` return `tecINTERNAL` instead of wrapping. Those last two paths sit behind LendingProtocol, SingleAssetVault and MPTokensV1, none of them enabled on our networks; they are taken for parity with upstream. |

Left out on purpose:

- `19c94c7`, Batch inner transactions must be wrapped in `RawTransaction`
  (fixBatchV1_2): only reachable when Batch is enabled. Batch is
  `Supported::no` in our `features.macro`, so `Transactor::invokePreflight`
  rejects every `ttBATCH` with `temDISABLED` before the inner transactions
  are looked at, and the amendment is not even listed by `feature` on devnet
  or testnet. Our Batch code is also the 3.1.2 shape (no
  `getBatchTransactions`), so the patch would not apply as is. If Batch is
  ever enabled, take the whole 3.3.0 to 3.4.1 Batch line together.
- `cd005ff`, packaging URL, and `00e6407`, the version bump.
- For the next batch, not in 3.4.1: upstream `develop` merged `60195e6`
  "Fix MPT partial payment overflow" (#8302, 2026-10-06), described by
  upstream as not exploitable. MPTokensV1 is not enabled on our networks.

## Layout differences

Upstream 3.4.1 moved the payment engine into `libxrpl`; our tree is still
the 3.1.2 layout, and the port maps as follows. Namespace `xrpl` upstream is
`ripple` here.

| Upstream (3.4.1) | Here |
|------------------|------|
| `include/xrpl/tx/paths/detail/Steps.h`, `StrandFlow.h` | `src/xrpld/app/paths/detail/Steps.h`, `StrandFlow.h` |
| `src/libxrpl/tx/paths/BookStep.cpp` | `src/xrpld/app/paths/detail/BookStep.cpp` |
| `include/xrpl/tx/invariants/InvariantCheck.h`, `src/libxrpl/tx/invariants/InvariantCheck.cpp` | `src/xrpld/app/tx/detail/InvariantCheck.h`, `InvariantCheck.cpp` |
| `src/libxrpl/ledger/helpers/TokenHelpers.cpp`: `accountSendMultiIOU`, `directSendNoLimitMultiMPT` | `src/libxrpl/ledger/View.cpp`: `accountSendMultiIOU`, `rippleSendMultiMPT` |
| `include/xrpl/basics/MathUtilities.h` | same path; `checkedSub` is carried for parity with upstream and is unused there as well |

One code adaptation: the IOU branch of `checkedStepAddOpt` uses
`IOUAmount::operator+=`, because our `IOUAmount` has no free `operator+`.
Both of its paths (Number switchover and legacy) throw on overflow instead
of wrapping, which is what upstream relies on.

## Behaviour to be aware of

- No protocol message format changes, no amendment, no config change
  required.
- Transaction results change without an amendment, exactly as upstream did
  it: a payment whose offer-crossing totals would overflow 64 bits no longer
  corrupts balances. The overflowing strand is dropped, and the payment ends
  as `tecPATH_DRY`, or `tecPATH_PARTIAL` when other strands had already
  delivered part of the amount. A 1.0.8 node and a 1.0.9 node disagree only
  on such a transaction, which is the attack itself, so a mixed-version
  network is safe for honest traffic, but the window should be short. Dynamic
  UNL scoring treats 1.0.9 as the minimum safe version once the foundation
  nodes run it (`MINIMUM_SAFE_VERSION` in dynamic-unl-scoring).
- If the invariant fires, the transaction is `tecINVARIANT_FAILED` (fee only)
  and the log carries `Invariant failed: XRP net change was positive:` with
  the full-width total.

## Verification

Build with tests enabled, then run the 1.0.5 to 1.0.8 suites plus:

```sh
postfiatd --unittest=ripple.app.Invariants          # adds the 2^64-drop credit case
postfiatd --unittest=ripple.app.PayStrand           # adds the checked-addition case
postfiatd --unittest=ripple.app.Flow
postfiatd --unittest=ripple.app.ReducedOffer
postfiatd --unittest=ripple.app.OfferBaseUtil
postfiatd --unittest=ripple.app.OfferWTakerDryOffer
postfiatd --unittest=ripple.app.OfferWOSmallQOffers
postfiatd --unittest=ripple.app.OfferWOFillOrKill
postfiatd --unittest=ripple.app.OfferWOPermDEX
postfiatd --unittest=ripple.app.OfferAllFeatures
```

The PR check runs the same list. Upstream shipped this fix without a unit
test (3.4.1 only adds Batch tests), so the cases are ours: the checked
helpers are proven by compile-time asserts in `MathUtilities.h`; the
`PayStrand` case checks the strand addition on each amount type at the
64-bit edge; the `Invariants` case credits exactly 2^64 drops across 185
account roots, which a 64-bit accumulator wraps to zero and the 128-bit one
reports as positive. An end-to-end reproduction through real offers is not
possible in the harness: a single amount is capped at 10^17 drops and the
public disclosure does not give the offer construction.

## Rollout

Same gates as 1.0.8: green CI on the exact head, image digests recorded,
devnet validators one at a time and then the devnet RPC node, each confirmed
`proposing` or `full` with its peers before the next, then testnet on
instruction with 1.0.8 as the rollback target. Nothing here needs a second
restart, a wallet backup, or a config change.
