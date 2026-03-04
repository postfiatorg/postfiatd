# Dynamic UNL: Distributed Nodes + Local Inference Design

Multiple independent Oracle Nodes each run the same open-weight LLM on their own hardware, prove computation via proof-of-logits, and cross-verify each other. Final scores are aggregated via median. This is Approach 4 in [Approaches.md](Approaches.md) — it combines the distributed trust of Approach 2 with the computational proofs of Approach 3.

---

## How This Differs from Design_DistributedCloudAPI

Design_DistributedCloudAPI (Approach 2) uses cloud LLM APIs with TLSNotary proofs. Each Oracle Node calls a cloud API, and the foundation's TLSNotary notary server proves the call happened.

This design replaces cloud APIs with local inference and replaces TLSNotary proofs with proof-of-logits. Oracle Nodes run the model themselves and publish cryptographic commitments of the model's internal outputs (logits). Other nodes spot-check each other's commitments.

Key differences:

| Aspect | Approach 2 (Cloud API) | Approach 4 (Local Inference) |
|--------|----------------------|----------------------------|
| LLM execution | Cloud API (each Oracle Node calls API) | Local GPU (each Oracle Node runs model) |
| Proof method | TLSNotary (MPC-TLS on API call) | Proof-of-logits (cryptographic commitment of model internals) |
| Proof verification | Foundation's TLSNotary notary | Cross-node spot-checking (any node or observer) |
| Foundation notary dependency | Yes — foundation operates TLSNotary notary (censorship vector) | No — removed entirely |
| Cloud API dependency | Yes — all nodes depend on API provider availability | No — self-contained |
| Hardware requirements | VPS with HTTPS capability | GPU hardware or cloud GPU rental |
| What proof demonstrates | "An API was called" | "This computation was performed" |

Everything else from Approach 2 carries over: PFT bond, commit-reveal, quorum rules, median aggregation, liveness requirements. See [Design_DistributedCloudAPI.md](Design_DistributedCloudAPI.md) for full details on these mechanisms.

Data collection remains identical across all approaches — foundation with Opacity attestation. See [Approaches.md](Approaches.md) Common Infrastructure.

---

## Model Selection and Pinning

Same process as Approach 3 (see [Design_FoundationLocalHardware.md](Design_FoundationLocalHardware.md)). The foundation selects an open-weight model, pins the exact version, and publishes the weight file hash (SHA-256).

**All Oracle Nodes must use the same model version.** This is enforced by:

- The scoring round announcement includes the required model version and weight hash
- Logit commitments are only valid if produced by the correct model (spot-checks against the wrong model produce mismatches)
- Oracle Nodes that submit commitments from a different model are detected during cross-verification

Model changes follow the same protocol upgrade process — announced in advance, community downloads new weights, change takes effect at a specific round number.

---

## Oracle Node Structure

Carried from Approach 2 with hardware requirement changes.

### Registration

- Bond **100,000 PFT** on-chain (locked while active, returned in full on deregistration — not slashable)
- Link a **Task Node address** (on-chain address associated with the operator's Task Node identity)
- Complete a **test round** demonstrating proof-of-logits capability (successfully produce and submit valid logit commitments using the pinned model)

### Liveness Requirements

- Oracle Nodes must participate in at least 1 of the last 4 scoring rounds
- Failure to meet liveness requirements results in the node being marked inactive
- Inactive nodes are excluded from quorum calculations
- Inactive nodes can reactivate by participating in the next scoring round

### Quorum Rules

- Minimum valid submissions required: **max(5, 2/3 of active Oracle Nodes)**
- If quorum is not met, the scoring round fails and the previous UNL remains in effect
- Only submissions that pass cross-verification (valid logit commitments) count toward quorum

### Deregistration

- Oracle Node submits a deregistration transaction
- PFT bond is unlocked and returned in full
- Node is removed from the active registry

---

## Proof-of-Logits Protocol (Distributed Version)

Each Oracle Node produces logit commitments during local inference. After all nodes reveal, cross-node verification detects dishonest submissions.

### During Inference

Each Oracle Node independently:

1. Loads the pinned model (verified by weight hash)
2. Loads the published data snapshot (verified by IPFS CID from round announcement)
3. Runs inference with temperature 0, greedy decoding, deterministic settings
4. At each token position, computes `SHA-256(logits_at_position_i)` and saves the hash
5. Produces the full logit commitment (ordered list of position hashes) alongside the generated output

### Submission

Logit commitments are included in the Oracle Node's reveal payload:

- Scores (Step 1 + Step 2 results with reasoning)
- Full logit commitment for Step 1 inference
- Full logit commitment for Step 2 inference
- Model version and weight hash used

All published to IPFS. The IPFS CID is submitted on-chain during the reveal phase.

### Cross-Node Verification

After all reveals are published:

1. Any node (or anyone with GPU access) can spot-check any other node's logit commitments
2. The verifier picks a random token position from the target node's commitment
3. The verifier runs one forward pass up to that position using the same model and input
4. The verifier compares their logit hash with the target node's published hash
5. Verification results are published alongside the aggregation report

**Verification scheduling:** Cross-verification happens during the aggregation window. The foundation's aggregation service performs a minimum number of spot-checks per Oracle Node submission. Additional spot-checks by other Oracle Nodes or community members are welcome but not required.

### Mismatch Handling

If a spot-check fails (hashes don't match beyond the tolerance threshold):

- The submission is **flagged** in the aggregation report with evidence (position checked, expected hash, actual hash)
- The flagged submission is **excluded from aggregation** — its scores do not contribute to the median
- The Oracle Node is **not slashed** — its PFT bond remains intact and is returned on deregistration
- Exclusion from aggregation is the penalty: the node loses its voice in that round's scoring

**Why no slashing:**

- Cross-hardware determinism is not perfectly solved — false positives are possible (see Determinism section)
- Slashing on potentially false positives would discourage participation
- Exclusion from aggregation is sufficient deterrence — a node that consistently fails verification gains nothing from participating
- The bond's purpose is Sybil resistance (preventing cheap fake nodes), not punishment for computational mismatches

**Repeated failures:** A node that fails verification in multiple consecutive rounds is likely dishonest or running on incompatible hardware. The community can track failure rates via the published aggregation reports.

---

## Hardware Requirements and Cloud GPU Option

Oracle Node operators need GPU hardware capable of running the pinned model. They do **not** need to own hardware — cloud GPU rental is a viable option.

### Hardware Tiers by Model Size

| Model Size | Minimum GPU | VRAM Required | Cloud GPU Option | Approx. Cloud Cost |
|-----------|------------|--------------|-----------------|-------------------|
| 7B-13B | RTX 4090, RTX 3090 | 16-24GB | RunPod (consumer tier) | ~$0.50-1/hr |
| 13B-32B | A100 40GB, L40S | 40-48GB | RunPod (datacenter tier) | ~$1-2/hr |
| 70B+ | A100 80GB, H100 | 80GB+ | Lambda, RunPod (enterprise) | ~$5-15/hr |

### Per-Round Cost Estimates

Assuming a scoring round takes 10-30 minutes of GPU time (model loading + two inference passes + logit commitment generation):

| Model Size | Per-Round Cost (Cloud) | Per-Week Cost (Weekly Rounds) |
|-----------|----------------------|------------------------------|
| 7B-13B | ~$0.10-0.50 | ~$0.40-2.00 |
| 13B-32B | ~$0.20-1.00 | ~$0.80-4.00 |
| 70B+ | ~$1.00-7.50 | ~$4.00-30.00 |

These costs must be offset by Task Node rewards. If the model is small enough (7B-13B), running an Oracle Node is accessible to anyone willing to rent a cloud GPU for under $1/round.

### Cloud GPU Providers

- **RunPod**: On-demand GPU rental, wide range of GPU types, ~$0.50-15/hr. Supports persistent storage for model weights.
- **Lambda**: Higher-end GPUs (H100, A100 80GB), ~$5-15/hr. Better for 70B+ models.
- **Vast.ai**: Marketplace model, cheapest prices, variable reliability.
- **Own hardware**: Amortizes over many rounds. A consumer RTX 4090 (~$2,000) running weekly 7B inference pays for itself in under a year vs. cloud rental.

---

## Scoring Round Lifecycle

Adapted from Approach 2. Phases are the same, but Oracle Nodes run local inference instead of calling a cloud API, and cross-verification replaces TLSNotary proof verification.

```
Phase 1: Data Snapshot (Foundation)
│   Foundation collects validator data (Opacity-attested)
│   Publishes snapshot + proofs → IPFS
│
↓
Phase 2: Round Announcement (Foundation, On-Chain)
│   Foundation publishes on-chain transaction:
│   - Snapshot IPFS CID
│   - Round number
│   - Required model version + weight hash
│   - Phase deadlines (commit, reveal, verification)
│
↓
Phase 3: Local Inference (Each Oracle Node, Independent)
│   Each Oracle Node:
│   ├── Fetches snapshot from IPFS
│   ├── Verifies Opacity proofs on data
│   ├── Loads pinned model (verified by weight hash)
│   ├── Runs Step 1 inference (individual scoring)
│   │   └── Saves logit hashes at every token position
│   ├── Runs Step 2 inference (diversity adjustment)
│   │   └── Saves logit hashes at every token position
│   └── Prepares submission (scores + logit commitments)
│
↓
Phase 4: Commit (Each Oracle Node, On-Chain)
│   Each Oracle Node submits:
│   sha256(scores_json + logit_commitments_hash + salt + round_number)
│   Deadline: commit window closes
│
↓
Phase 5: Reveal (Each Oracle Node, IPFS + On-Chain)
│   Each Oracle Node publishes to IPFS:
│   - Full scores (Step 1 + Step 2 with reasoning)
│   - Logit commitments (Step 1 + Step 2)
│   - Model version + weight hash
│   - Salt
│   Submits IPFS CID on-chain
│
↓
Phase 6: Cross-Verification + Aggregation (Foundation + Anyone)
│   ├── Verify each reveal matches its commit hash
│   ├── Spot-check logit commitments (random positions per Oracle Node)
│   ├── Flag and exclude submissions with failed spot-checks
│   ├── Compute median score per validator across valid submissions
│   ├── Rank validators by median score
│   └── Select top 35 (MAX_UNL_VALIDATORS)
│
↓
Phase 7: Publication (Foundation, IPFS + On-Chain)
    ├── Aggregation report (median scores, verification results, quorum info)
    ├── Final UNL JSON → IPFS + HTTPS endpoint
    └── UNL hash + audit trail CID + sequence → on-chain transaction
```

### Timing

Phase durations carry from Approach 2. The main difference is Phase 3: local inference may take longer than a cloud API call (minutes vs. seconds), but this is bounded by model size and hardware. The commit window must account for the slowest reasonable Oracle Node hardware.

---

## What Gets Published (Every Scoring Round)

| Artifact | Where Published |
|----------|----------------|
| Data snapshot (validator profiles) | IPFS |
| Opacity proofs of data collection | IPFS |
| Scoring configuration (model version, weight hash, prompt versions) | IPFS |
| Round announcement (snapshot CID, round number, model version, deadlines) | On-chain transaction |
| Each Oracle Node's commit hash | On-chain transaction (one per Oracle Node) |
| Each Oracle Node's reveal (scores + logit commitments) | IPFS (one CID per Oracle Node, submitted on-chain) |
| Cross-verification results (spot-checks performed, pass/fail per Oracle Node) | IPFS |
| Aggregation report (median scores, included/excluded nodes, quorum info) | IPFS |
| Final UNL JSON | IPFS + HTTPS endpoint |
| UNL hash + audit trail IPFS CID + sequence + config version | On-chain transaction |

---

## Determinism Prerequisite

The same challenge as Approach 3, but harder. Multiple independent operators with potentially different hardware make determinism harder to guarantee.

See [ResearchStatus.md](research/ResearchStatus.md) open questions #2 and #5.

**Why this is harder than Approach 3:**

- In Approach 3, the foundation controls both inference and verification hardware — it can standardize on a specific GPU type
- In Approach 4, Oracle Node operators are independent — requiring all operators to use the same GPU type is more restrictive and limits participation
- Different GPU types produce different floating-point results, leading to different logits and potential false positive verification failures

**Possible mitigations:**

| Mitigation | Trade-off |
|-----------|-----------|
| Require specific GPU type | Limits who can participate. More restrictive than Approach 3 because it applies to all Oracle Nodes, not just verifiers. |
| Tolerance thresholds | Accept logit hashes within a similarity bound. Requires quantized/rounded logit hashing. Reduces sensitivity but enables hardware diversity. |
| GPU-class grouping | Verify only within the same GPU class (e.g., all A100 nodes verify each other). Requires enough nodes per class for meaningful verification. |
| Optimistic verification with generous threshold | Default to "pass" unless logits diverge dramatically. Catches outright fraud (wrong model, wrong input) but not subtle manipulation. |

The tolerance threshold approach is likely the most practical — it enables hardware diversity while still detecting dishonest nodes that use the wrong model or fabricate outputs.

---

## Trust Model Summary

The strongest trust model of all four approaches. No single scorer, no cloud API dependency, and computation is mathematically verifiable.

| Property | Approach 4 |
|----------|-----------|
| Scorers | Multiple independent Oracle Nodes |
| LLM execution | Local (no API dependency) |
| Proof | Proof-of-logits (each node commits to model internals) |
| Verification | Cross-node spot-checking + community verification |
| Aggregation | Median across valid submissions |
| Single point of failure | None for scoring. Foundation still collects data (Opacity-attested). |

**To manipulate the scoring outcome, an attacker must:**

1. Control a majority of Oracle Nodes (each bonded with 100,000 PFT)
2. Have all controlled nodes produce valid-looking logit commitments (using the correct model — fabricated commitments fail spot-checks)
3. Coordinate scores across controlled nodes to shift the median
4. Do all of this without being detected by honest nodes' cross-verification

This is the highest bar of any approach. The combination of economic bonds (Sybil resistance) + logit commitments (computational honesty) + median aggregation (outlier resistance) provides defense in depth.

**Remaining centralization:** The foundation still controls data collection. This is Opacity-attested and consistent across all approaches. The aggregation step is deterministic (median computation) — anyone can recompute it from the published Oracle Node submissions.

---

## Cost Profile

| Cost Category | Bearer | Estimate |
|--------------|--------|----------|
| Data collection (Opacity) | Foundation | Per-round Opacity fees |
| Aggregation service | Foundation | Operational cost |
| Cross-verification (minimum spot-checks) | Foundation | GPU cost for spot-checking |
| IPFS pinning | Foundation | Commodity cost |
| GPU hardware/rental | Oracle Node operators | $0.10-7.50 per round (see Hardware section) |
| PFT bond | Oracle Node operators | 100,000 PFT (locked, returned on deregistration) |
| Model storage | Oracle Node operators | 14-140GB per model version |

The foundation's costs are lower than any other approach — no API fees, no GPU inference for scoring (only for verification spot-checks). Oracle Node operators bear the GPU costs, which must be offset by Task Node rewards.

---

## What Needs to Be Built

Everything from Approach 2 (distributed coordination) plus everything from Approach 3 (logit tooling):

**From Approach 2:**
- Oracle Node registration with PFT bond (on-chain)
- Commit-reveal protocol (on-chain transactions)
- Quorum rules and enforcement
- Median aggregation service
- Liveness tracking
- Oracle Node client software

**From Approach 3:**
- Logit commitment generation during inference
- Logit commitment serialization and IPFS publication
- Verification CLI tool (spot-check any node's commitments)

**New for Approach 4:**
- Cross-node verification scheduling (which nodes verify which, how many spot-checks per submission)
- Verification result aggregation and publication
- Tolerance threshold calibration (if using approximate matching)
- Oracle Node client integration: local inference + logit commitment + commit-reveal in a single workflow
- Model distribution infrastructure (Oracle Nodes need to download and verify large weight files)
