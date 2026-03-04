# Dynamic UNL: Research Status and Open Questions

Tracks what has been investigated, what conclusions were reached, and what still needs research before implementation begins.

---

## Completed Research

### TLSNotary Protocol
- **Selective disclosure**: QuickSilver VOLE-based interactive ZK proofs. Handlers support `REVEAL` and `PEDERSEN` actions on HTTP request/response parts (headers, body, status code). Can reveal prompts and responses while hiding API keys via Pedersen commitments.
- **On-chain verification**: Not viable for PFT Ledger (no smart contract VM). Hash-on-chain + proof-on-IPFS is the correct pattern.
- **Browser extension plugins**: Irrelevant for server-side scoring pipeline. Resource constraints (4 KB send / 16 KB receive defaults) incompatible with LLM payloads.
- **Bandwidth overhead**: ~25 MB fixed + ~10 MB per 1 KB outgoing + ~40 KB per 1 KB incoming. For a 50 KB prompt: ~526 MB per MPC-TLS session. Manageable for weekly batch jobs.
- **TLS version**: TLS 1.2 only. No TLS 1.3 support planned near-term.
- **Maturity**: Pre-production. README warns against production use. Breaking API changes between alphas.
- **Detailed findings**: `TLSNotaryEvaluation.md`, `MpcTlsEvaluation.md`

### Opacity Network
- **Architecture**: Built on TLSNotary. Adds decentralized notary network, EigenLayer staking/slashing, Intel SGX TEEs, Cloudflare AI Gateway integration.
- **Bandwidth advantage**: Cloudflare log-fetch indirection reduces MPC overhead from ~526 MB to ~46 MB per proof. The MPC-TLS session fetches a small log entry from Cloudflare, not the full LLM prompt.
- **Consumer requirements**: API key only. No tokens needed to use the service. Sign up at `app.opacity.network`.
- **Pricing**: Not publicly documented (see open question below).
- **SDK**: TypeScript, iOS, Android, Flutter, React Native.
- **Detailed findings**: `TLSNotaryEvaluation.md`

### zkTLS Landscape
- **Evaluated**: Opacity, Reclaim Protocol, zkPass, Pluto, Primus Labs, DECO (Chainlink), Nillion.
- **Conclusion**: Opacity is the strongest fit for data collection attestation (decentralized, production-ready, pre-built verifiable inference). Primus Labs is the second-best alternative. All providers are startups; TLSNotary (Ethereum Foundation-backed, Apache-2.0) is the only durable fallback.

### Two-Layer Proof Architecture
- **Decision**: Opacity for data collection (prevents foundation self-attestation), foundation TLSNotary server for Oracle Node LLM calls (trust separation already built into the protocol).
- **Rationale**: Documented in `DesignPlan_v2.md` Section 5.

### Ambient (ambient.xyz) — Proof of Logits Reference Implementation
- **What it is**: Solana fork (SVM-compatible) PoW L1 blockchain. Replaces PoS with Continuous Proof of Logits (CPoL). Raised $7.2M seed (a16z CSX + Delphi Digital), reportedly $74M total.
- **Model**: Standardizes on GLM-4.6 (357B MoE, 32B active params) from Z.ai/THUDM. Single pinned open-weight model for the entire network.
- **How CPoL works**: Miner runs full inference, commits a hash of the logit vector at each token position. Verifier spot-checks a random position — reruns inference at that single token (cheap) and compares hashes. Hash match = miner ran the model. Hash mismatch = slash.
- **Verification overhead**: ~0.1% (verify 1 token out of ~4000).
- **Hardware requirements**: GLM-4.6 requires 8× H100 SXM or 4× H200 (1TB+ system memory). The `ambient-miner-benchmark` enforces TTFT P95 ≤ 4.0s, ≥ 20 tok/s at 64 concurrent requests. Apple M4 Mac Mini ($1,600) is not viable for this model size.
- **RunPod alternative**: 8× H100 SXM pod costs ~$20-25/hour on RunPod. Could work as a cron job (spin up monthly, run inference, spin down) to reduce cost.
- **GitHub**: `github.com/ambient-xyz` — 8 repos (Rust). Auction system for inference requests, tokenizer for GLM-4.6, miner benchmark tool, oracle data structures.
- **Relevance to PostFiat**: Ambient's CPoL pattern could be adapted for PostFiat's validator scoring. Instead of mining, validators produce scoring inferences and commit logit hashes. Other validators spot-check. However, PostFiat's scoring task runs weekly (not continuous), uses a different model, and targets a much smaller network.

### Deterministic LLM Inference — State of the Art
- **Greedy decoding (temp=0)**: ~98%+ token match rate across runs even with floating-point variation. Logit variance rarely flips which token is selected. Nearly deterministic but not bitwise identical at the logit level.
- **SGLang batch-invariant kernels**: Achieve bitwise identical results regardless of batch size. 34-55% performance overhead. Limited to same GPU architecture (not cross-vendor).
- **LLM-42 decode-verify-rollback**: Enables determinism with lower overhead than batch-invariant mode. Same hardware family constraint.
- **FP32 inference (LayerCast)**: Near-perfect cross-hardware determinism (FP32 is standardized). 2× memory, slower.
- **Cross-hardware bitwise identity**: Not achievable under standard BF16/FP8 inference. A100 vs H100 produce different floating-point results. Same-architecture constraint is real.
- **Key papers**: SGLang deterministic inference (LMSYS), LLM-42 (arxiv 2601.17768), Understanding Numerical Nondeterminism (arxiv 2506.09501), DiFR auditing (Adam Karvonen).

### Competing Approaches to Decentralized LLM Inference Verification
- **VeriLLM**: Prefill-based re-verification, ~1% overhead, Merkle commitments to hidden states. Strongest cryptographic guarantees below zkML.
- **Proof of Quality (PoQ)**: Lightweight evaluator models score output quality. Vulnerable to score manipulation. Not computation verification.
- **Optimistic Proof of Computation (OPoC)**: Fraud proofs + economic staking. High dispute resolution latency.
- **zkML (EZKL, Modulus Labs, Giza)**: Cryptographically sound but computationally infeasible for multi-billion-parameter models (~1000s per 1M params).

---

## Open Research — Must Resolve Before Implementation

### 1. Opacity Network Pricing
**Question:** What does Opacity charge consumers? Is there a free tier? Per-proof pricing? Monthly subscription?

**Why it matters:** The scoring pipeline generates proofs for every data collection call (VHS, Network Monitoring, MaxMind, SumSub) every scoring round. If pricing is per-proof, weekly rounds with 4-5 data sources could accumulate costs.

**Action:** Sign up at `app.opacity.network`, explore the developer portal, and document the pricing model. If pricing is prohibitive, evaluate Primus Labs as alternative or increase the weight of the TLSNotary fallback path.

### 2. Opacity SDK Integration Test
**Question:** Does the Opacity SDK actually work end-to-end for our use case? Can we generate a proof of an API call to a PostFiat service (e.g., VHS API) and verify it?

**Why it matters:** Documentation and reality can diverge. Need to validate: SDK stability, proof generation time, proof size, verification workflow, error handling, and rate limits.

**Action:** Build a minimal proof-of-concept:
1. Call the VHS API through Opacity
2. Generate a proof
3. Verify the proof independently
4. Measure: proof generation time, proof artifact size, any failures or quirks

### 3. TLSNotary Notary Server Customization
**Question:** What does it take to fork/customize the TLSNotary `notary-server` for PostFiat's use case? What customizations are needed?

**Why it matters:** The foundation must operate a TLSNotary notary server for Oracle Node LLM call attestation (in the API-based model) or for general attestation purposes. The reference `notary-server` is a starting point, but PostFiat needs rate limiting per Oracle Node, health monitoring, metrics, and potentially custom authentication.

**Action:**
- Clone and build `github.com/tlsnotary/tlsn` (Rust, requires `--release` builds)
- Run the reference `notary-server` locally
- Test: have a client (simulating an Oracle Node) notarize an API call through it
- Document: configuration options, extension points, what needs custom code vs configuration
- Evaluate: fork the repo or wrap it? Maintenance burden of staying on upstream vs diverging?

### 4. TLSNotary Notary Server as Our Own Fork
**Question:** Should PostFiat maintain its own fork of the TLSNotary notary-server, or wrap the upstream binary with a sidecar?

**Tradeoffs:**
- **Fork**: Full control, can add PostFiat-specific features (Oracle Node auth, rate limiting, metrics). Risk: upstream breaks compatibility on alpha updates, maintenance burden.
- **Sidecar/wrapper**: Run upstream notary-server as-is, add a reverse proxy or wrapper service that handles auth, rate limiting, and metrics. Easier to track upstream. Less control over internals.

**Action:** After completing item 3, evaluate which approach is more practical. Document the decision and rationale.

### 5. Local LLM Scoring with Proof of Logits (Validator-Side Execution)

This is the most significant architectural decision remaining. The strategic direction is clear: move from centralized foundation scoring to validators independently running LLM inference and proving their computation. The question is how to implement it.

**Context from strategic review:** Six frontier LLMs were consulted on the Dynamic UNL architecture. All agreed:
- Keep LLM-based scoring as the UNL selection mechanism
- Drop centralized foundation scoring in favor of each validator running the same LLM independently
- Scores converge through reproducible inference, not through a single entity dictating results
- This preserves AI-driven validator selection while eliminating single-entity control

**The model:** Each validator (or scoring node) runs a pinned open-weight model with a canonical prompt and greedy decoding. They publish scores alongside logit hashes. Other validators can spot-check by re-running inference at random token positions. If logit hashes don't match, the submitter is challenged/slashed.

#### 5a. Open-Weight Model Selection
- The current design pins `claude-opus-4-6` (Anthropic) — proprietary, closed-weight, cannot run locally.
- Switching to an open-weight model is required for local execution.
- **Candidates**: Qwen 3.5 (suggested in strategic review), Llama 4, DeepSeek-V3, Mistral Large.
- **Key question**: Is there an open-weight model capable enough for the nuanced scoring task (consensus quality, reputation assessment, geographic diversity optimization)?
- **Action**: Run the scoring prompts through candidate open-weight models and compare output quality to Claude. This is a critical benchmark — if no open model produces acceptable scoring quality, the local execution path is blocked.

#### 5b. Hardware Requirements for Validators
- **Strategic review suggestion**: Apple Silicon Mac Mini (~$1,600) or RunPod cloud GPU.
- **Reality check from Ambient research**: Ambient's GLM-4.6 (357B MoE) requires 8× H100 (1TB+ RAM). PostFiat does not need to use a model that large.
- **Realistic targets**: A 7B-70B parameter model (Qwen 3.5 32B, Llama 4 Scout 109B) can run on consumer hardware or affordable cloud instances.
  - 7B-13B models: Mac Mini M4 (32GB) or $0.20-0.50/hr RunPod instance
  - 32B-70B models: Mac Studio M4 Ultra (192GB) or $1-3/hr RunPod instance
  - 100B+ models: RunPod 2-4× A100/H100 at $5-15/hr
- **Scoring runs weekly**: Even expensive hardware only needs to run for minutes per month. A RunPod cron job is viable.
- **Action**: Determine minimum model size that produces acceptable scoring quality, then derive hardware requirements from that.

#### 5c. Cross-Hardware Determinism Problem
This is the hardest unsolved problem in the proof-of-logits approach.

**The issue**: For logit hash comparison to work, two validators running the same model on the same input must produce identical (or near-identical) logits. Current state:
- **Same GPU model + same software**: Achievable with SGLang deterministic mode (34-55% overhead) or LLM-42.
- **Different GPU models (A100 vs H100)**: Not bitwise identical. Floating-point operation ordering differs.
- **CPU inference (llama.cpp)**: More deterministic than GPU, but slower.
- **Greedy decoding**: 98%+ token match rate even without bitwise logit identity — the selected token is nearly always the same.

**Possible approaches for PostFiat:**
1. **Token-level verification** (not logit-level): Hash the selected token sequence, not the raw logits. 98%+ match rate. Simple but allows small divergences that could be exploited.
2. **Constrained hardware**: Require all scoring nodes to use the same GPU architecture and inference stack. Reduces decentralization but enables bitwise determinism.
3. **Correlation threshold**: Accept logit correlation >0.99 rather than bitwise identity. Cross-validate with spot-checks. This is what Ambient's real-world testing suggests (>0.93 correlation in 150+ runs, with controlled conditions aiming for >0.99).
4. **Optimistic with challenge**: Assume honest execution, allow any validator to re-run and challenge. Slash on divergence beyond threshold. Most practical for PostFiat's weekly cadence.
5. **OpenRouter as intermediary**: Use OpenRouter to route all scoring nodes to the same model endpoint. All nodes call the same API with the same parameters. TLSNotary proves the call happened. This is a hybrid between local execution and API-based — the model is "shared" but each node independently calls it and proves their call.

**Action**: The correlation threshold + optimistic challenge approach (option 4) is the most practical path. Research empirically: run the same scoring prompt through the same quantized model on different hardware, measure logit correlation, determine what threshold is achievable and sufficient.

**Key concern from strategic review**: "Bitwise-identical inference across heterogeneous hardware is not yet proven at scale. The >0.93 foundation correlation from 150+ runs is promising but was achieved in controlled conditions." This needs empirical validation with PostFiat's actual scoring prompts before committing to this architecture.

#### 5d. Prompt Governance
- If validators run their own LLM, who controls the prompt?
- Prompts need periodic updates as scoring criteria evolve.
- **Options**: Foundation publishes canonical prompt in open-source repo (current plan), on-chain governance votes on prompt changes, validator supermajority must adopt new prompts.
- This is a governance question, not a technical one. The current plan (foundation controls prompts, transfers to governance later) works for all architectures.

#### 5e. Ambient's Code Reusability
- Ambient's repos are Rust. PostFiat's scoring service would likely be TypeScript/Python.
- The auction system is Solana-specific (not applicable to PFT Ledger).
- The logit hashing scheme and spot-check verification logic are conceptually reusable but would need to be reimplemented.
- The `ambient-miner-benchmark` tool could be adapted for PostFiat scoring node qualification.
- **Action**: Study Ambient's `oracle-datastructures` for the logit commitment format. Reimplement the verification logic rather than porting Rust code.

### 6. LLM API Costs Per Scoring Round
**Question:** What is the per-round cost if each Oracle Node pays for their own LLM API calls (in the API-based model)?

**Why it matters:** With N Oracle Nodes each making 2 API calls (Step 1 + Step 2) per round, the total network cost is `N * 2 * cost_per_call`. If the scoring prompt is ~50 KB input and ~20 KB output, this is non-trivial for frontier models.

**Action:** Estimate costs for:
- Claude Opus (current pinned model) — input + output token pricing
- GPT-4o — as comparison
- Open-weight alternatives via OpenRouter — compare pricing
- Open-weight local execution — hardware cost per run
- Multiply by expected node count (5-20) and weekly cadence

### 7. PFT Bond Amount Justification
**Question:** Why 100,000 PFT? Is this amount sufficient to deter Sybil attacks while remaining accessible to legitimate operators?

**Why it matters:** Too low = cheap to spin up malicious Oracle Nodes. Too high = excludes legitimate participants, reduces decentralization.

**Action:** Analyze current PFT price/market cap, estimate the cost of acquiring enough PFT to control a majority of Oracle Nodes, and set the bond at a level where majority control is economically irrational. Document the analysis.

### 8. Commit-Reveal Timing Parameters
**Question:** How long should each phase of the scoring round last?

**Current proposal in DesignPlan_v2.md:**
- Phase 1 (data snapshot): ~30 minutes
- Phase 3 (Oracle Node scoring): ~6 hours
- Phase 4 (commit): ~3 hours
- Phase 5 (reveal): ~3 hours
- Phase 6 (aggregation): ~1 hour
- Total: ~13 hours

**Action:** Validate these durations against: LLM API response times (or local inference times), TLSNotary proof generation time, IPFS publication time, on-chain transaction confirmation time. Consider timezone distribution of Oracle Node operators. Test on devnet with real timings.

### 9. IPFS Pinning Service Selection
**Question:** Which IPFS pinning service to use? What are the costs?

**Candidates:** Pinata, web3.storage, Filebase, self-hosted IPFS node with cluster.

**Action:** Compare pricing (per GB stored, per GB bandwidth), reliability, API quality, and evaluate whether self-hosting is worth the operational overhead. Estimate monthly storage requirements based on proof sizes and scoring cadence.

### 10. Proof Size Empirical Measurement
**Question:** How large are TLSNotary attestation artifacts and Opacity proofs in practice?

**Why it matters:** Affects IPFS storage costs, download times for community verification, and whether proofs can reasonably be included in on-chain memo fields (if ever desired).

**Action:** Generate actual proofs in items 2 and 3 above and measure artifact sizes. Document typical sizes for: small API calls (KYC check), medium API calls (VHS data fetch), large API calls (LLM scoring).

### 11. Oracle Node / Scoring Node Hardware Requirements
**Question:** What hardware does a scoring node operator need?

**Depends on architecture:**
- **API-based model** (DesignPlan_v2.md): Commodity VPS. Run TLSNotary client, call LLM API, publish to IPFS, submit on-chain transactions. Minimal hardware.
- **Local inference model**: Depends on model size. 7B-13B: Mac Mini or cheap cloud GPU. 32B-70B: Mac Studio or mid-tier cloud. 100B+: datacenter GPU required.
- **RunPod cron approach**: Scoring runs weekly. Spin up a GPU instance, run inference (~minutes), submit results, spin down. Cost: $1-25 per run depending on model size.

**Action:** Determine target model first (item 5a), then derive hardware requirements.

### 12. LLM Provider Model Stability
**Question:** Can LLM providers silently change model behavior while keeping the same model ID?

**Why it matters:** If Anthropic updates `claude-opus-4-6` weights without changing the model identifier, different Oracle Nodes calling at different times could get meaningfully different results — not from non-determinism, but from actual model changes. This concern goes away entirely with local open-weight models (weights are hash-pinned).

**Action:** Research LLM provider versioning policies. Anthropic pins model versions with date suffixes (e.g., `claude-opus-4-6-20250620`). Confirm that pinning to a dated version guarantees identical model weights over time. Document the policy. Note: this item becomes irrelevant if the architecture moves to local inference with open-weight models.

### 13. Phased Implementation Strategy
**Question:** What is the implementation path from current state to fully decentralized local-inference scoring?

**Proposed phases (from strategic review):**
1. **Phase 1 — Bootstrap**: Foundation publishes a fixed UNL based on objective metrics (uptime, agreement scores). AI scoring is not active in the consensus path. Validators run but don't depend on LLM scores yet.
2. **Phase 2 — Centralized AI scoring (DesignPlan_v2.md)**: Foundation collects data (Opacity-attested), Oracle Nodes call remote LLM API (TLSNotary-attested), commit-reveal, median aggregation. This is what DesignPlan_v2.md describes.
3. **Phase 3 — Local inference**: Validators run open-weight model locally, publish scores + logit hashes, optimistic verification with spot-checks. Foundation's role reduced to prompt governance and data snapshot publication.

**Action**: Validate this phasing. Phase 1 can ship fastest (no LLM infrastructure needed). Phase 2 is what DesignPlan_v2.md describes. Phase 3 requires resolving items 5a-5e above. Each phase is independently valuable and the system can operate at any phase indefinitely.

### 14. OpenRouter as Intermediate Step
**Question:** Can OpenRouter serve as a stepping stone between API-based and local inference?

**Context:** OpenRouter provides a unified API for multiple model providers, including open-weight models. All scoring nodes could call the same model through OpenRouter with the same parameters, and each call could be TLSNotary-attested. This gives:
- Consistent model behavior (same endpoint, same weights)
- TLSNotary provability (HTTPS API call)
- Access to open-weight models without running them locally
- Lower cost than frontier proprietary models

**Action:** Test OpenRouter with candidate open-weight models. Verify TLSNotary can notarize OpenRouter API calls. Compare scoring quality and cost to direct Anthropic/OpenAI API calls.

### 15. Identity Requirements Without KYC
**Question:** Can the identity layer be simplified or dropped if the architecture moves to local inference with economic staking?

**Context from strategic review:** If validators post a PFT bond and prove their computation via proof of logits, KYC/KYB may become unnecessary for Sybil resistance — the economic bond serves that purpose. However, identity still matters for:
- LLM-based reputation and sanctions scoring (requires knowing who the entity is)
- Entity concentration limits (prevent one entity running many validators under different names)
- Regulatory clarity

**Options:**
1. Keep full KYC/KYB (current plan) — strongest identity, heaviest onboarding burden
2. Domain verification only (no KYC) — lighter, still ties validators to real institutions
3. Bond-only (no identity) — simplest, relies entirely on economic security. LLM scores based on network metrics and geolocation only, not reputation.

**Action:** Determine if the LLM scoring prompt can produce meaningful scores without identity data. If yes, option 3 is viable for launch with option 1 added later. If identity data significantly improves scoring quality, keep it.

---

## Research Priority Order

| Priority | Item | Blocking? |
|----------|------|-----------|
| 1 | Open-weight model quality benchmark (#5a) | Yes — determines entire architecture direction |
| 2 | Cross-hardware determinism testing (#5c) | Yes — validates proof-of-logits feasibility |
| 3 | Opacity pricing (#1) | Yes — affects data collection architecture |
| 4 | Opacity SDK integration test (#2) | Yes — validates core assumption |
| 5 | TLSNotary notary-server setup (#3) | Yes — needed for Phase 2 |
| 6 | LLM costs: API vs local vs OpenRouter (#6) | Yes — affects Oracle Node economics |
| 7 | Phased implementation strategy (#13) | No — planning, not blocking |
| 8 | OpenRouter as intermediate step (#14) | No — optimization |
| 9 | TLSNotary fork decision (#4) | No — depends on #5 results |
| 10 | Proof size measurement (#10) | No — measured during #2 and #4 |
| 11 | Identity requirements (#15) | No — can ship with current plan, revisit later |
| 12 | PFT bond justification (#7) | No — can be adjusted before mainnet |
| 13 | Commit-reveal timing (#8) | No — validated during devnet testing |
| 14 | IPFS pinning service (#9) | No — commodity decision |
| 15 | Scoring node hardware (#11) | No — derived from model choice |
| 16 | LLM model stability (#12) | No — irrelevant if moving to local inference |
