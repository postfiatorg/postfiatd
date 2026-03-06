# Dynamic UNL: Phased Validator Scoring Design

Validators progressively take over UNL scoring from the foundation across three phases: foundation scores alone (Phase 1), validators verify the foundation's work (Phase 2), validators prove they did the work honestly (Phase 3). By Phase 3, the foundation is no longer the authority — validators collectively determine the UNL.

---

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| LLM execution | Local inference on open-weight model | No cloud API dependency, no proprietary model lock-in, anyone can reproduce |
| Who scores | Foundation at launch, validators at end-state | Ship fast, decentralize progressively |
| Validator hardware | GPU sidecar (separate from validator VPS) | Consensus stability unaffected by scoring workload |
| GPU requirement | One mandatory GPU type for all scorers | Guarantees determinism, Layer 2 implicitly verifies compliance |
| Proof method | Two-layer: output convergence + proof-of-logits | Layer 1 proves agreement, Layer 2 proves honesty |
| Data integrity | Transparency (published on IPFS, cross-checkable) | Data is public and verifiable — validators can cross-check against their own observations |
| Scoring rewards | None — XRPL model | Validators participate for network alignment, not payment |
| Bootstrap incentive | One-time validator grants (USDC + PFT token warrant) | Covers initial hardware/infrastructure costs |

---

## Model Selection and Pinning

The foundation selects an open-weight model suitable for validator scoring. The model must be:

- Publicly available with downloadable weights
- Runnable on the mandatory GPU type
- Capable of producing consistent validator scores (verified by benchmark — see [ResearchStatus.md](research/ResearchStatus.md) open question #4)

**Pinning:**

- A full **execution manifest** is published with every scoring round, covering: HuggingFace snapshot revision, all weight shard hashes (safetensors format), tokenizer files, config files, prompt template version, inference engine version (SGLang), attention backend, dtype, quantization mode, container image digest, CUDA/driver version
- All validators must use the exact pinned manifest — verified by comparing their local manifest against the published one
- Prompt strings are constructed directly, not via chat-template defaults (upstream template changes are a silent divergence risk)
- Model changes are treated as protocol upgrades: announced in advance, validators download new weights, change takes effect at a specific round number

**Target model class:** 7B-32B parameter open-weight models (Qwen 3.5, Llama 4, DeepSeek, or equivalent). Large enough for quality scoring, small enough to run on commodity GPU hardware.

---

## Data Collection

The foundation collects validator performance data and publishes it as a structured snapshot.

**Data sources:**

| Source | Data | Verifiability | Published to IPFS? |
|--------|------|---------------|-------------------|
| VHS API | Agreement scores, uptime, latency, peer connections, server version, manifests, amendment voting, fee votes, domain verification | On-chain and public API — validators can cross-check | Yes |
| ASN (WHOIS/RIR) | AS number, ISP name, organization (e.g., "DigitalOcean") | Public data — anyone can query | Yes |
| MaxMind GeoIP2 API | Continent, country, city | Third-party — independently queryable | No (EULA restricts republishing; used internally for scoring context only) |
| On-chain identity records | Verification status, entity type, domain attestation (no PII) | Directly verifiable from ledger | Yes |

**Snapshot publication:**

- All data is compiled into a normalized JSON snapshot (the scorer's input)
- Raw API responses are archived alongside the snapshot for audit verification (`raw/` directory)
- Snapshot + raw evidence are pinned to IPFS at a defined block height
- IPFS CID is published on-chain
- Validators fetch the snapshot by CID and verify it against the on-chain hash
- MaxMind geolocation data is provided to the LLM during scoring but excluded from the IPFS publication (EULA restriction)

The snapshot is deterministic — all validators use the same data because they all fetch the same IPFS content by CID.

---

## Scoring Mechanism

The LLM ingests all candidate validator data packets in a single prompt (sorted deterministically by master public key) and outputs:

- A score (0-100) for each candidate with written reasoning
- The scoring considers: consensus performance, operational reliability, software diligence, historical track record, network participation, identity and reputation
- **Observer-dependent metrics** (latency, peer count, topology) receive low weight relative to objective metrics (agreement scores, uptime, server version). VHS observes the network from a single vantage point — these metrics reflect VHS's view, not universal truth.
- The scoring prompt explicitly enumerates diversity dimensions with weighting guidance:
  - Country concentration
  - ASN (autonomous system) concentration
  - Cloud provider / datacenter concentration
  - Operator concentration (how many validators the same entity runs)

**UNL inclusion rule (mechanical, not LLM-decided):**

1. If a candidate scores above the cutoff threshold → eligible
2. If 35 or fewer candidates clear the cutoff → all eligible candidates are on the UNL
3. If more than 35 clear the cutoff → top 35 by rank score are included
4. All others are alternates, ranked in order
5. **Churn control:** a challenger only replaces an incumbent UNL validator if the challenger's score exceeds the incumbent's by at least X points (configurable). This prevents UNL oscillation from minor score fluctuations between rounds. The gap value is determined empirically during testnet operation.

**Configuration:**

- Temperature 0, greedy decoding (always select highest-probability token)
- Structured JSON output mode
- Deterministic inference settings enabled (see Determinism section)
- Prompt is versioned and published in the open-source scoring repository

---

## Hardware Architecture

Validators keep their existing VPS for consensus operations. GPU scoring runs as a **separate sidecar process** on separate hardware.

```
┌─────────────────────┐         ┌─────────────────────-┐
│   Validator (VPS)   │         │   GPU Sidecar        │
│                     │   API   │                      │
│  - Consensus        │◄───────►│  - Load pinned model │
│  - Ledger sync      │         │  - Run scoring       │
│  - UNL management   │         │  - Generate logits   │
│  - Hash publication │         │  - Return results    │
│                     │         │                      │
│  Runs 24/7          │         │  Runs per round      │
│  Cheap VPS          │         │  Mandatory GPU type  │
└─────────────────────┘         └─────────────────────-┘
```

**Why a sidecar:**

- Consensus stability is unaffected — GPU crashes don't take down the validator
- Validators keep running on cheap VPS ($20-50/month)
- GPU is only needed for the few minutes of each scoring round
- Cloud GPU rental (RunPod) makes this accessible without hardware ownership
- If the sidecar fails, the validator just misses that scoring round

**Mandatory GPU type:** All validators must use the same GPU model for scoring. This guarantees deterministic logit output across all validators (see Determinism section). The specific GPU type will be determined by benchmark testing. Layer 2 (proof-of-logits) implicitly verifies GPU compliance — wrong hardware produces wrong logit hashes.

**Setup target:** One-command install script that:

1. Checks for the required GPU (fails with clear message if wrong)
2. Pulls the pinned model weights (verified by SHA-256 hash)
3. Configures the inference engine with deterministic settings
4. Connects to the validator via local API
5. Total setup time: under 30 minutes with a guide

---

## Phase 1: Foundation Scoring (Testnet Launch)

The foundation runs the scoring model on GPU infrastructure and publishes the UNL. Validators accept it.

**What happens:**

1. Foundation collects data, publishes snapshot to IPFS
2. Foundation runs pinned open-weight model on mandatory GPU type (RunPod or owned hardware)
3. Foundation publishes scores, reasoning, and UNL to IPFS
4. Foundation publishes UNL hash + IPFS CID on-chain
5. Validators fetch UNL, verify hash, apply to consensus

**Trust model:** High trust in the foundation. Everything is published and auditable, but the foundation is the sole scorer. Anyone can re-run the model to check the foundation's work, but there is no protocol-level enforcement.

**What ships:** The existing node-side components (UNLHashWatcher, DynamicUNLManager), the scoring pipeline running on foundation infrastructure, and the IPFS publication system.

---

## Phase 2: Validator Verification — Layer 1 (Output Convergence)

Validators run the scoring model locally and verify that their results match the foundation's.

**What happens:**

1. Foundation publishes data snapshot and scoring round announcement (same as Phase 1)
2. Each validator's GPU sidecar runs the same model with the same prompt and data
3. Each validator produces their own scored UNL
4. Each validator publishes a hash of their scored output on-chain
5. Output hashes are compared across all validators

**Convergence check:**

- All hashes match → UNL is confirmed, foundation was honest
- A validator's hash diverges → that validator is flagged (misconfigured hardware, wrong model version, or tampering)

**Important:** During Phase 2, the foundation's UNL remains authoritative. Validators are verifying in shadow mode. Convergence data is collected and monitored but not yet binding. This phase proves that the system works before handing over authority.

**Commit-reveal required:** Validators must not see each other's output hashes before publishing. Otherwise, a lazy validator could copy someone else's hash without running the model. Validators commit their hash first, then reveal after the commit window closes.

**What ships:** Validator GPU sidecar setup script, scoring client integrated with the validator, on-chain commit-reveal for output hashes, convergence monitoring dashboard.

---

## Phase 3: Full Verification — Layer 2 (Proof of Logits)

Validators prove they actually ran the correct model, not just that they agree on the output.

**Why Layer 2 is needed:** Layer 1 alone is insufficient. Validators could achieve hash convergence by copying each other's output without running the model. Layer 2 proves computational integrity — each validator actually performed the inference.

**What happens:**

1. During inference, each validator's GPU sidecar computes `SHA-256(logits)` at every token position
2. The ordered list of logit hashes (the **logit commitment**) is published alongside the scored output
3. After all validators reveal, any validator (or external party) can spot-check any other validator:
   - Pick a random token position K
   - Re-run one forward pass up to position K with the same model and input
   - Compare the logit hash at position K
4. If hashes match → validator ran the correct model at that position
5. If hashes don't match → validator cheated (wrong model, wrong input, or fabricated output)

**Why this is efficient:** Generating the full scored UNL may take 500-2,000 tokens of inference. Spot-checking one position costs milliseconds — one forward pass at one position. Checking 5-10 random positions gives high statistical confidence.

**What mandatory GPU guarantees:** Because all validators use the same GPU type with the same deterministic inference settings, logit output is identical across machines. A validator on wrong hardware produces wrong logit hashes and gets caught. The GPU requirement is self-enforcing — no separate hardware attestation needed.

**Mismatch handling:**

- Flagged validator is excluded from that round's convergence check
- Repeated failures indicate dishonesty or misconfiguration
- No slashing — exclusion from the round is the penalty

**The transition:** Once both layers are running and convergence is consistently proven, the converged validator UNL replaces the foundation-published UNL as the authoritative source. The foundation becomes one validator among many, not the sole authority.

**What ships:** Logit commitment generation in the inference engine, logit hash publication alongside scored output, cross-validator spot-check tooling, verification result aggregation.

---

## Scoring Round Lifecycle (Phase 3, Full System)

```
1. Data Snapshot (Foundation)
│   Collect validator data, publish to IPFS
│   Publish snapshot CID + round number on-chain
│
↓
2. Local Inference (Each Validator, Independent)
│   GPU sidecar:
│   ├── Fetch snapshot from IPFS
│   ├── Load pinned model (verified by weight hash)
│   ├── Run scoring prompt (temperature 0, greedy, deterministic)
│   ├── Save logit hash at every token position
│   └── Produce scored UNL + logit commitment
│
↓
3. Commit (Each Validator, On-Chain)
│   Publish domain-separated hash (binary encoding with fixed-width fields)
│
↓
4. Reveal (Each Validator, IPFS + On-Chain)
│   Publish to IPFS: scored output + logit commitment + salt
│   Submit IPFS CID on-chain
│
↓
5. Verification + Convergence (Any Validator / External Party)
│   Layer 1: Compare output hashes — do all validators agree?
│   Layer 2: Spot-check logit commitments — did each validator run the model?
│
↓
6. UNL Publication (Deterministic)
│   If convergence confirmed: converged UNL is the authoritative UNL
│   Publish final UNL hash + audit trail CID on-chain
```

---

## Determinism

Cross-hardware determinism is the prerequisite for both layers. It is solved by requiring one mandatory GPU type.

**Why one GPU type works:** Floating-point non-determinism comes from different hardware architectures using different operation ordering in parallel computations. Same GPU model + same driver + same CUDA version + same inference engine + deterministic mode = identical logits.

**Inference engine:** SGLang with `--enable-deterministic-inference` is the primary inference engine across all phases. SGLang's deterministic path is explicitly documented and works on a broader range of GPUs than alternatives. vLLM is kept as a documented fallback only if SGLang proves unsuitable during reproducibility testing.

**For our use case** (one scoring prompt per round, not a throughput service), the performance overhead of deterministic mode (~34%) is irrelevant.

**All hashed artifacts use canonical JSON serialization** (RFC 8785 / JCS) to ensure identical content produces identical hashes regardless of serialization implementation. Standard JSON is non-deterministic in key ordering, whitespace, and number formatting.

**What needs empirical testing** (see [ResearchStatus.md](research/ResearchStatus.md)) — a reproducibility harness must be built and run before Phase 2, measuring:

1. Does the same model + prompt + input produce identical output text across instances of the mandatory GPU type?
2. Do logit hashes match at every token position with deterministic mode enabled?
3. What is the convergence rate on the final UNL inclusion list (the actual deliverable)?
4. Are results consistent across: same worker, different workers, same GPU type, different datacenters, warm vs cold starts?

The harness results are a hard gate for Phase 2 entry (>99% output equality required). If output text converges but logit hashes don't perfectly match, Layer 1 works without Layer 2. If both converge, the full system works.

---

## Trust Model Progression

| Phase | Trust Model | Foundation Role | Validator Role |
|-------|------------|-----------------|---------------|
| 1 | Trust the foundation | Sole scorer, publishes UNL | Accept published UNL |
| 2 | Verify the foundation | Scores and publishes, remains authoritative | Run model locally, publish output hash, flag discrepancies |
| 3 | Replace the foundation | One validator among many | Score independently, prove computation, collective UNL |

By Phase 3, no single entity controls the UNL. Every validator independently computes it. Every computation is verifiable. The foundation's only remaining privileged role is data collection — and even that is published transparently.

---

## Community Impact

**Phase 1:** Same as any centralized system — community can audit published data and reasoning but cannot participate. Trust depends on foundation reputation.

**Phase 2:** Validators become active verifiers. Any validator catching a discrepancy between their result and the foundation's has public, mathematical evidence. The community can track convergence rates across validators. Trust shifts from "trust the foundation" to "trust the math."

**Phase 3:** The strongest community trust model. Every validator computes the UNL independently and proves it. Community members with GPU access can verify any validator's work. The open-weight model and published prompts mean the entire system is inspectable — no black boxes.

**Hardware barrier:** Validators need GPU access for Phase 2+. Cloud GPU rental (RunPod, ~$1-2/hr for a few minutes per round) keeps costs low. The one-command setup script keeps the barrier to entry technical-skill-wise low.

---

## Institutional Impact

**Phase 1:** Simple and auditable. One accountable entity scores validators transparently. Easy to explain to regulators.

**Phase 2:** Institutions running validators can independently verify the foundation's work. Mathematical verification strengthens the governance narrative.

**Phase 3:** Strongest regulatory positioning. "Multiple independent parties run the same AI model, prove their computation mathematically, and independently arrive at the same validator list." No single entity controls the outcome. Institutions can participate directly by running a validator with a GPU sidecar.

**The phased approach helps institutions:** conservative institutions can engage at Phase 1 (simple, centralized, auditable) while technically sophisticated institutions can participate fully at Phase 3.

---

## Cost Profile

| Cost | Phase 1 | Phase 2 | Phase 3 |
|------|---------|---------|---------|
| Foundation GPU | ~$1-2/round | ~$1-2/round | Same (foundation is now a regular validator) |
| Foundation data collection | Operational cost | Same | Same |
| Foundation IPFS pinning | Commodity | Same | Same |
| Validator GPU | None | ~$0.10-1.00/round (cloud) | Same + logit computation overhead |
| Validator VPS | Existing cost | Same | Same |
| Scoring rewards | None | None | None (XRPL model) |

**Bootstrap:** One-time validator grants (USDC for hardware/infrastructure + PFT token warrant) to cover initial setup costs for the founding validator set.

---

## What Needs to Be Built

**Phase 1 (ship first):**
- Scoring pipeline: data collection → open-weight model inference → score computation → UNL generation
- IPFS publication of snapshot, scores, reasoning, and UNL
- On-chain UNL hash publication (already designed — UNLHashWatcher, DynamicUNLManager)

**Phase 2 (after Phase 1 is live):**
- Validator GPU sidecar: inference engine with pinned model, connected to validator via local API
- One-command setup script (GPU check, model download, engine configuration)
- On-chain commit-reveal for output hashes
- Convergence monitoring (compare hashes across validators, track divergence rates)

**Phase 3 (after Phase 2 proves convergence):**
- Logit commitment generation during inference (hash logits at every token position)
- Logit commitment publication alongside scored output
- Cross-validator spot-check tooling (pick position, re-run one forward pass, compare hash)
- Verification result publication
- Protocol transition: converged UNL replaces foundation-published UNL as authoritative source
