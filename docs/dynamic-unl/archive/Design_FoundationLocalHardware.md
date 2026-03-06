# Dynamic UNL: Foundation + Local Inference Design

The foundation runs an open-weight LLM on its own hardware and proves computation via proof-of-logits. This is Approach 3 in [Approaches.md](Approaches.md) — it replaces cloud API dependency with local inference and uses cryptographic commitments of model internals to prove computation.

---

## How This Differs from Design_FoundationCloudAPI

Design_FoundationCloudAPI (Approach 1) uses a cloud LLM API. This design replaces that with local inference — the foundation runs the model on its own hardware and publishes cryptographic commitments of the model's internal outputs (logits) at every token position. Anyone with GPU access can spot-check any position by re-running a single forward pass and comparing hashes. The proof says: "this specific computation was performed with this specific model."

Key differences:

| Aspect | Approach 1 (Cloud API) | Approach 3 (Local Inference) |
|--------|----------------------|----------------------------|
| LLM execution | Cloud API (Anthropic, OpenAI) | Foundation-owned or rented GPU |
| Proof method | None (trust foundation) | Proof-of-logits (cryptographic commitment of model internals) |
| What proof demonstrates | N/A | "This computation was performed" |
| Verification | Read published reasoning on IPFS | Anyone with compatible GPU can spot-check (~0.1% of inference cost) |
| Cherry-picking resistance | Weak — foundation can call API multiple times, publish preferred result | Stronger — re-running from scratch and committing different logits requires full re-computation, detectable if anyone checks |
| Model constraint | Any model (proprietary or open-weight) | Open-weight models only (verifiers must run the same model) |
| External dependency | Cloud API availability | None (self-contained) |

Everything else — data collection, on-chain publication, node-side components, identity gate, IPFS audit trail — is identical. See [Approaches.md](Approaches.md) Common Infrastructure.

---

## Model Selection and Pinning

The foundation selects an open-weight model suitable for the validator scoring task. The model must be publicly available with downloadable weights.

**Pinning process:**

1. Foundation evaluates candidate open-weight models against the scoring benchmark (see [ResearchStatus.md](research/ResearchStatus.md) open question #4)
2. Foundation selects a model and pins the exact version
3. The weight file hash (SHA-256) is published alongside the scoring configuration
4. All scoring rounds reference the pinned model version and weight hash

**Model changes** are treated like protocol upgrades:

- Announced in advance with the new model version and weight hash
- Community has time to download and prepare verification tooling
- Change takes effect at a specific scoring round number
- Previous rounds remain verifiable against the old model

**Governance transfer:** Model selection authority transfers to community governance over time (see [Approaches.md](Approaches.md) Governance section). The process is the same regardless of who makes the decision — select, pin, publish hash, announce.

---

## Inference Execution

The foundation runs the pinned model on GPU hardware it owns or rents. The scoring pipeline is identical to Approach 1 (two-step LLM scoring with the same prompts and data), except the LLM call is a local inference call instead of a cloud API call.

**Configuration:**

- Temperature 0, greedy decoding (select the highest-probability token at each position)
- Structured JSON output mode for score formatting
- Deterministic settings where supported by the inference framework (e.g., SGLang deterministic mode)

**Hardware tiers by model size:**

| Model Size | Example Hardware | Cloud GPU Rental |
|-----------|-----------------|-----------------|
| 7B-13B | Single consumer GPU (RTX 4090, 24GB VRAM) | ~$0.50-1/hr (RunPod) |
| 13B-32B | Single datacenter GPU (A100 40GB, L40S) | ~$1-2/hr (RunPod) |
| 70B+ | Multi-GPU or high-VRAM (A100 80GB, H100) | ~$5-15/hr (RunPod, Lambda) |

LLM output is inherently non-deterministic even with temperature 0 and greedy decoding — floating-point arithmetic produces slightly different results across runs and hardware. This is accepted. What matters is that the logit commitments prove the foundation used the declared model with the declared input, not that the output is perfectly reproducible.

---

## Proof-of-Logits Protocol

Proof-of-logits allows anyone to verify that the foundation ran the declared model on the declared input, without re-running the entire inference.

### During Inference

At each token position during generation, the foundation:

1. Runs the forward pass to produce logits (the raw output vector over the vocabulary)
2. Computes a hash of the full logit vector: `SHA-256(logits_at_position_i)`
3. Saves the hash alongside the generated token

The result is an ordered list of hashes — one per generated token — called the **logit commitment**.

### Publication

After scoring completes, the foundation publishes to IPFS:

- The full logit commitment (ordered list of position hashes)
- The generated output (scores + reasoning)
- The input data (snapshot, already published separately)
- The model version and weight hash

The IPFS CID is included in the on-chain publication alongside the UNL hash.

### Spot-Check Verification

Anyone can verify any token position:

1. Pick a random token position `i` from the logit commitment
2. Load the same model (verified by weight hash)
3. Feed the same input (verified from published snapshot)
4. Run the model forward through position `i` (requires the KV-cache up to that point)
5. Compute `SHA-256(logits_at_position_i)` from the local run
6. Compare with the published hash at position `i`

**If hashes match:** The foundation used the declared model with the declared input at that position.

**If hashes don't match:** The foundation was dishonest — it used a different model, different input, or fabricated the commitment. This is publicly reportable.

### Cost of Verification

A single spot-check requires one forward pass up to position `i`. For a response of 1,000 tokens, checking one random position costs on average ~0.1% of full inference (the expected cost of running to a random midpoint). Multiple spot-checks increase confidence linearly.

### What Proof-of-Logits Does NOT Prove

- It does not prove the foundation only ran the model once (cherry-picking is still possible — the foundation could run inference multiple times and publish the preferred result's commitment)
- It does not prove the foundation used the best available data (data integrity is handled by publishing the snapshot to IPFS for cross-checking)
- It does not guarantee deterministic reproduction (see Determinism section below)

Cherry-picking is harder than in Approach 1 because each run requires full GPU inference (not a cheap API call) and each run produces a completely new logit commitment. The foundation cannot mix and match — it must commit to a single complete run.

---

## Community Verification Model

There is no formal on-chain challenge or slashing mechanism. Verification is community-driven:

- Anyone with compatible GPU hardware can spot-check any scoring round
- The foundation publishes a **verification CLI tool** that automates the process: download model, load snapshot, pick random positions, compare hashes
- Mismatches are raised publicly (forum, GitHub, on-chain memo)
- The foundation's reputation depends on consistent, honest commitments

**Why no on-chain slashing:**

- The foundation is the single scorer — there is no bond to slash against (unlike Approach 2/4 where Oracle Nodes bond PFT)
- Community auditing provides sufficient deterrence: a single verified mismatch destroys the foundation's credibility
- On-chain dispute resolution adds complexity without proportional benefit in a single-scorer model

**Verification barrier:** Spot-checking requires GPU access and the ability to load the model. This means most community members cannot verify directly. However:

- Any single verifier catching dishonesty is sufficient — the result is publicly visible
- Institutions, researchers, and technically sophisticated community members serve as auditors
- The barrier is lower than running a full Oracle Node (Approach 2/4) — verification is a one-time check, not continuous operation
- Cloud GPU providers (RunPod, Lambda) make verification accessible without hardware ownership

---

## Data Collection

Data collection is identical across all approaches. The foundation collects validator performance data from VHS, MaxMind GeoIP2, and SumSub. The resulting data snapshot is published to IPFS for anyone to inspect and cross-check.

See [Approaches.md](Approaches.md) Common Infrastructure for details.

---

## What Gets Published (Every Scoring Round)

| Artifact | Where Published |
|----------|----------------|
| Data snapshot (validator profiles) | IPFS |
| Data collection methodology and sources | IPFS |
| Scoring configuration (model version, weight hash, prompt versions) | IPFS |
| Logit commitment (ordered list of position hashes) | IPFS |
| LLM output (scores + reasoning for Step 1 and Step 2) | IPFS |
| Final UNL JSON | IPFS + HTTPS endpoint |
| UNL hash + audit trail IPFS CID + sequence + config version | On-chain transaction |

The logit commitment is the key addition compared to Approach 1.

---

## Determinism Prerequisite

Reliable spot-check verification requires that two machines running the same model on the same input produce sufficiently similar logits. Current research shows:

- Same GPU type + same software: achievable with deterministic inference settings
- Different GPU types: ~93% correlation in controlled tests — insufficient for reliable verification
- Target: >0.99 logit correlation for spot-check verification to work without false positives

This is an active research area. See [ResearchStatus.md](research/ResearchStatus.md) open questions #2 and #5.

**Workarounds if perfect determinism is not achievable:**

| Workaround | Trade-off |
|-----------|-----------|
| Standardize GPU type | Foundation and verifiers must use the same GPU model. Limits who can verify but guarantees match. |
| Tolerance thresholds | Accept logit hashes that are "close enough" (e.g., cosine similarity > 0.999). Requires a different hashing scheme (hash of quantized/rounded logits). |
| Optimistic verification | Assume honest unless challenged. Only flag mismatches that exceed a generous threshold. Reduces false positives at the cost of reduced sensitivity. |
| Publish raw logits | Instead of hashing, publish the actual logit vectors (large but eliminable via quantization). Verifiers compare vectors directly with a tolerance. |

The choice of workaround depends on determinism research results. The architecture supports any of these — only the hashing/comparison step changes.

---

## Trust Model Summary

The foundation is the single scorer — the same level of centralization as Approach 1. The difference is in what the proof demonstrates:

| Property | Approach 1 (Cloud API) | Approach 3 (Local Inference) |
|----------|----------------------|----------------------------|
| Proof type | "An API was called" | "This computation was performed" |
| Cherry-picking | Easy — API calls are cheap, call multiple times | Harder — each run requires full GPU inference |
| Fabrication | Not provable | Impossible (logit commitment prevents) |
| Verification cost | None (trust foundation) | ~0.1% of inference cost per spot-check |
| Verification barrier | None (read published reasoning) | GPU access required |
| External dependency | Cloud API | None |

The trust model is: **centralized scorer with mathematically verifiable computation, community-audited.** Stronger than "centralized scorer with API call proof" (Approach 1), but weaker than "distributed scorers" (Approach 2/4).

---

## Community Impact

Proof-of-logits gives the community something Approach 1 cannot: the ability to independently verify that the foundation actually performed the computation it claims. In Approach 1, community members can read the published reasoning but cannot independently verify the computation. Here, anyone with a GPU can download the open-weight model, re-run a spot-check, and mathematically confirm the foundation was honest.

**The hardware barrier is the main limitation.** Most community members do not have GPU access. This creates a two-tier community: those who can verify (GPU owners, cloud GPU renters, institutions) and those who must trust the verifiers. In practice, this is acceptable — a single honest verifier catching a mismatch is enough to expose dishonesty, and the result is publicly visible to everyone.

**Community perception is likely positive but cautious.** The open-weight model and published logit commitments signal strong transparency intent. The community can read every score's reasoning, inspect the model, and audit the prompts — all impossible with proprietary cloud APIs. However, the foundation remains the single scorer. Community members who want distributed control (multiple independent scorers) will see this as an improvement over Approach 1 but not a final destination. Framing Approach 3 as a stepping stone toward Approach 4 helps manage this expectation.

**Participation opportunities are limited.** Community members cannot score — only audit. This is the same as Approach 1. The difference is that auditing is more meaningful here (mathematical verification vs. reading published reasoning). For community members who want active participation in scoring, Approach 2 or 4 is necessary.

---

## Institutional Impact

Institutions that value mathematical rigor will find this approach more compelling than Approach 1. The proof-of-logits model is conceptually similar to financial auditing — the foundation publishes its work, and anyone can spot-check it. Institutions with GPU resources (universities, research labs, large validators) can run independent audits.

**The single-scorer centralization is still a concern.** Institutions evaluating PostFiat for regulatory compliance or governance purposes may flag that one entity controls all scoring decisions, even if those decisions are verifiable. This is the same objection as Approach 1, softened by stronger proofs but not eliminated.

**The unproven technology is a risk factor.** Proof-of-logits is not battle-tested at scale. Institutions that prefer conservative, proven mechanisms may view simpler auditable systems (Approach 1/2) as more reliable. The determinism problem adds uncertainty — if spot-checks produce false positives, the verification story weakens. Institutions will want to see the determinism research resolved before committing confidence to this approach.

**Regulatory clarity is straightforward.** One accountable entity (the foundation) makes all scoring decisions. This is simple to explain to regulators. The verifiability of computation is an additional positive — it demonstrates that the foundation cannot fabricate results, which strengthens the governance narrative.

**Best for:** technically sophisticated institutions that value auditability and are comfortable with newer cryptographic proof methods. Less suitable for institutions that prioritize operational simplicity or battle-tested mechanisms.

---

## Cost Profile

| Cost Category | Bearer | Estimate |
|--------------|--------|----------|
| GPU hardware/rental | Foundation | $0.50-15/hr depending on model size |
| Data collection | Foundation | Operational cost |
| IPFS pinning | Foundation | Commodity cost |
| Infrastructure (servers, monitoring) | Foundation | Operational cost |
| LLM API fees | None | Replaced by local inference |
| Verification | Community (optional) | ~0.1% of inference cost per spot-check |

The foundation bears all scoring costs. There are no per-round API fees — the primary recurring cost is GPU compute time.

---

## What Needs to Be Built

**Logit commitment tooling:**
- Inference wrapper that captures logit vectors at each token position during generation
- Hash computation pipeline (SHA-256 of logit vectors)
- Commitment serialization and IPFS publication

**Verification tooling:**
- CLI tool that automates spot-check verification: download model, load input, pick random positions, compare hashes
- Support for the chosen workaround (tolerance thresholds, standardized GPU, etc.)
- Clear pass/fail output with evidence for public reporting

**Scoring pipeline adaptation:**
- Replace cloud API call with local inference call in the existing scoring pipeline
- Integrate logit commitment generation into the inference step
- Publish commitment alongside scores in the IPFS audit trail

**Infrastructure:**
- GPU hardware procurement or cloud GPU account setup
- Model storage and versioning (weight files are large — 7B model ~14GB, 70B model ~140GB)
- Monitoring and alerting for inference failures
