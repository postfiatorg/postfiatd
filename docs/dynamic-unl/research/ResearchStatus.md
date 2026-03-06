# Dynamic UNL: Research Status

What we know, what we don't, and what to do next.

---

## What We Know

### How Ambient does proof of logits

Ambient is a blockchain ($74M+ raised, a16z-backed) where the "mining" is running LLM inference. Their approach:

1. A node runs an LLM and generates a response (expensive — full inference)
2. At each token, the node saves a hash of the model's internal output (the "logits")
3. A verifier picks a random token position and re-runs just that one position (cheap — one forward pass)
4. If the hashes match, the node was honest. If not, they get slashed.

This is clever because verification costs ~0.1% of generation. The catch: it requires that two machines running the same model produce the same internal outputs. That's the hard part.

Ambient uses GLM-4.6 (a 357B parameter model needing 8× H100 GPUs, ~$20-25/hr on RunPod). We don't need a model that big — a 7B-70B model would work for our scoring task and runs on much cheaper hardware.

We don't have Ambient's code and can't reuse it (it's Solana-specific Rust). But we understand the concept well enough to build our own version.

### Can two machines produce the same LLM output?

This is the key question for proof of logits. Current state:

- **Same GPU type + same software**: Yes, with special settings. SGLang has a deterministic mode (34-55% slower). Works within the same GPU family.
- **Different GPU types (e.g. A100 vs H100)**: No. Floating-point math produces slightly different results on different hardware.
- **Greedy decoding (temperature 0)**: 98%+ of generated tokens are identical even with small floating-point differences. The tiny math errors almost never change which word the model picks.
- **Bottom line**: Exact match is hard across different hardware. But "close enough" (>0.99 correlation) is achievable with constraints, and 98%+ token-level match is already there with simple greedy decoding.

Testing with 150+ runs showed >0.93 correlation in controlled conditions. Needs to reach >0.99 to be reliable. This is the biggest open question.

### Other ways to verify LLM inference

- **VeriLLM**: Re-run the full input as a single batch (1% overhead). Strong guarantees but still needs same-hardware constraint.
- **Optimistic verification**: Assume everyone is honest, let anyone challenge by re-running. Slash cheaters. Most practical for our weekly schedule.
- **Zero-knowledge ML proofs**: Mathematically perfect but way too slow for large models. Not viable for years.

---

## What We Don't Know Yet

### 1. Can an open-weight model score validators well enough?
The current design uses Claude (proprietary, can't run locally). To move toward local inference, we need an open-weight model (Qwen 3.5, Llama 4, DeepSeek) that produces scoring quality comparable to Claude. This needs a benchmark: run our actual scoring prompts through candidates and compare results.

If no open model is good enough, the local inference path is dead and we stay with API-based scoring.

### 2. Can we achieve reliable determinism for proof of logits?
The >0.93 correlation from 150+ controlled runs is promising but not enough. We need >0.99 for spot-check verification to work. Need to test with our actual scoring prompts on real hardware.

Possible approaches if perfect determinism isn't achievable:
- Accept approximate match with a tolerance threshold
- Require all scoring nodes to use the same GPU type
- Use optimistic verification — assume honesty, slash on challenge
- Use OpenRouter so everyone hits the same API endpoint (hybrid approach)

### 3. How much does each scoring round cost?
With N nodes × 2 API calls × model pricing per round:
- Claude Opus: expensive
- Open-weight via OpenRouter: cheaper
- Local hardware/RunPod: cheapest per run, but upfront cost

Need real numbers to evaluate.

### 4. Is 100,000 PFT the right bond amount?
Too low = easy to create fake nodes. Too high = nobody participates. Need to analyze PFT economics.

### 5. Are the scoring round timing windows right?
Current plan: ~13 hours total (snapshot → scoring → commit → reveal → aggregation). Need real-world testing.

### 6. Which IPFS pinning service?
Pinata, web3.storage, Filebase, or self-hosted. Need to compare cost and reliability.

### 7. What hardware do scoring nodes need?
Depends on which approach we pick: a basic VPS for API-based, or GPU hardware for local inference.

### 8. Who controls the scoring prompt?
Foundation publishes it in the open-source repo. Later, governance can take over. This works for all approaches.

### 9. Do we still need KYC if validators post an economic bond?
The bond prevents Sybil attacks. KYC is still useful for reputation scoring and entity concentration limits, but might not be mandatory. Need to test if the LLM can score well without identity data.

---

## Approaches

The four architectural approaches for Dynamic UNL scoring are defined in [Approaches.md](../archive/Approaches.md). They differ along two primary axes: **who scores** (foundation vs. distributed nodes) and **where the LLM runs** (cloud API vs. local inference). The chosen architecture ([Design](../Design.md)) uses a phased rollout from foundation scoring to validator-driven scoring with local inference and proof-of-logits.

| Approach | Who Scores | LLM Execution | Proof Method | Design Doc |
|----------|-----------|--------------|-------------|-----------------|
| 1 | Foundation | Cloud API | Published audit trail | archive/Design_FoundationCloudAPI |
| 2 | Distributed Nodes | Cloud API | Commit-reveal + median | archive/Design_DistributedCloudAPI |
| 3 | Foundation | Local | Proof-of-logits | archive/Design_FoundationLocalHardware |
| 4 | Distributed Nodes | Local | Proof-of-logits | archive/Design_DistributedLocalHardware |
| **Chosen** | **Foundation → Validators** | **Local** | **Output convergence + proof-of-logits** | **Design.md** |

---

## Research Priority

Ordered by what blocks approach selection and design decisions:

| # | What | Why |
|---|------|-----|
| **1** | Benchmark open-weight models on our scoring task | Determines if local inference produces quality scores |
| **2** | Test output convergence across same GPU type | Determines if Layer 1 (output convergence) works |
| **3** | Test logit-level determinism with SGLang/Ingonyama/LayerCast | Determines if Layer 2 (proof-of-logits) works |
| **4** | Calculate local inference costs (RunPod, owned hardware) | Economics of validator GPU sidecar |
| 5 | Test scoring round timing on testnet | Can adjust later |
| 6 | Compare IPFS pinning services | Commodity decision |
| 7 | Define mandatory GPU type | Falls out of determinism testing |
| 8 | Test scoring quality without identity data | Can ship with current plan first |
| 9 | Decide prompt governance model | Governance question, not blocking |
