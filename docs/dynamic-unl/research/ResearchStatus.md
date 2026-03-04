# Dynamic UNL: Research Status

What we know, what we don't, and what to do next.

---

## What We Know

### How to prove API calls are real (TLSNotary + Opacity)

TLSNotary lets two parties jointly participate in an HTTPS connection so that one can prove to the other that a real API call happened. The proof can show the full request and response while hiding secrets like API keys. It only supports TLS 1.2 today, but all major LLM providers still support 1.2.

Opacity Network is a production service built on top of TLSNotary. Instead of running your own proof server, you use Opacity's decentralized network of proof servers. They're backed by economic guarantees (operators lose money if they cheat). No tokens needed to use it — just an API key from their developer portal.

We looked at 7 alternatives (Reclaim, zkPass, Pluto, Primus Labs, DECO, Nillion). Opacity is the best fit. Primus Labs is a decent backup. If all third parties fail, we can always run our own TLSNotary server.

**Decision made:** Use Opacity to prove the foundation's data collection calls (so we can't fake the data we feed to the LLM). Use our own TLSNotary server to prove Oracle Nodes' LLM API calls (the trust separation between Oracle Node and foundation is already built into the protocol).

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

### 1. Opacity pricing
We know it's API-key-only (no tokens), but the actual pricing isn't public. We generate proofs for 4-5 data sources every scoring round. Need to sign up and find out the real cost. If it's too expensive, we fall back to running our own TLSNotary server.

### 2. Does Opacity actually work for us?
Need to build a quick test: call the VHS API through Opacity, generate a proof, verify it. Measure how long it takes, how big the proof is, and whether it breaks.

### 3. Can we run a TLSNotary server?
Need to set up the reference TLSNotary server, test it with a simulated Oracle Node, and figure out what customizations we need (authentication, rate limiting, monitoring). Then decide: fork the project or wrap it with a proxy.

### 4. Can an open-weight model score validators well enough?
The current design uses Claude (proprietary, can't run locally). To move toward local inference, we need an open-weight model (Qwen 3.5, Llama 4, DeepSeek) that produces scoring quality comparable to Claude. This needs a benchmark: run our actual scoring prompts through candidates and compare results.

If no open model is good enough, the local inference path is dead and we stay with API-based scoring.

### 5. Can we achieve reliable determinism for proof of logits?
The >0.93 correlation from 150+ controlled runs is promising but not enough. We need >0.99 for spot-check verification to work. Need to test with our actual scoring prompts on real hardware.

Possible approaches if perfect determinism isn't achievable:
- Accept approximate match with a tolerance threshold
- Require all scoring nodes to use the same GPU type
- Use optimistic verification — assume honesty, slash on challenge
- Use OpenRouter so everyone hits the same API endpoint (hybrid approach)

### 6. How much does each scoring round cost?
With N nodes × 2 API calls × model pricing per round:
- Claude Opus: expensive
- Open-weight via OpenRouter: cheaper
- Local hardware/RunPod: cheapest per run, but upfront cost

Need real numbers to evaluate.

### 7. Is 100,000 PFT the right bond amount?
Too low = easy to create fake nodes. Too high = nobody participates. Need to analyze PFT economics.

### 8. Are the scoring round timing windows right?
Current plan: ~13 hours total (snapshot → scoring → commit → reveal → aggregation). Need real-world testing.

### 9. Which IPFS pinning service?
Pinata, web3.storage, Filebase, or self-hosted. Need to compare cost and reliability.

### 10. How big are the proofs?
Nobody has published actual TLSNotary/Opacity proof sizes. Need to generate real proofs and measure.

### 11. What hardware do scoring nodes need?
Depends on which approach we pick: a basic VPS for API-based, or GPU hardware for local inference.

### 12. Can LLM providers silently change a model?
Anthropic uses date-pinned versions (e.g. `claude-opus-4-6-20250620`). Need to confirm this means the weights are frozen. This question goes away if we switch to open-weight models (we pin the exact weight file by hash).

### 13. Who controls the scoring prompt?
Foundation publishes it in the open-source repo. Later, governance can take over. This works for all approaches.

### 14. Can we use OpenRouter as a stepping stone?
All scoring nodes call the same open-weight model through OpenRouter. TLSNotary proves each call. Gives us consistent results + provability without requiring local GPUs. Good intermediate step.

### 15. Do we still need KYC if validators post an economic bond?
The bond prevents Sybil attacks. KYC is still useful for reputation scoring and entity concentration limits, but might not be mandatory. Need to test if the LLM can score well without identity data.

---

## Approaches

The four architectural approaches for Dynamic UNL scoring are defined in [Approaches.md](../Approaches.md). They differ along two primary axes: **who scores** (foundation vs. distributed nodes) and **where the LLM runs** (cloud API vs. local inference). The proof method follows from the LLM execution: cloud API → MPC-TLS proof (Opacity/TLSNotary), local inference → proof-of-logits.

| Approach | Who Scores | LLM Execution | Proof Method | Existing Design |
|----------|-----------|--------------|-------------|-----------------|
| 1 | Foundation | Cloud API | Opacity | Design_FoundationCloudAPI |
| 2 | Distributed Nodes | Cloud API | TLSNotary | Design_DistributedCloudAPI |
| 3 | Foundation | Local | Proof-of-logits | Design_FoundationLocalHardware |
| 4 | Distributed Nodes | Local | Proof-of-logits | Design_DistributedLocalHardware |

---

## Research Priority

Ordered by what blocks approach selection and design decisions:

| # | What | Why |
|---|------|-----|
| **1** | Benchmark open-weight models on our scoring task | Determines if local inference approaches (3, 4) are viable |
| **2** | Test cross-hardware determinism with our prompts | Determines if proof-of-logits verification works reliably |
| **3** | Find out Opacity pricing | Needed for all approaches (data collection) |
| **4** | Test Opacity SDK end-to-end | Confirm it actually works |
| **5** | Set up TLSNotary server | Needed for Approach 2 |
| **6** | Calculate LLM costs (API vs local) | Economics of cloud vs local approaches |
| 7 | PFT bond amount analysis | Needed for Approach 2 and 4 |
| 8 | Test scoring round timing on devnet | Can adjust later |
| 9 | Measure actual proof sizes | Measured during #4 and #5 |
| 10 | Compare IPFS pinning services | Commodity decision |
| 11 | Define scoring node hardware requirements | Falls out of model and approach choice |
| 12 | Test scoring quality without identity data | Can ship with current plan first |
| 13 | Confirm LLM provider version pinning policy | Quick check, or irrelevant if going open-weight |
| 14 | Decide prompt governance model | Governance question, not blocking |
