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

**Why it matters:** The foundation must operate a TLSNotary notary server for Oracle Node LLM call attestation. The reference `notary-server` is a starting point, but PostFiat needs rate limiting per Oracle Node, health monitoring, metrics, and potentially custom authentication.

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

### 5. Local LLM Execution with Proof of Logits
**Question:** Can Oracle Nodes run the scoring LLM locally instead of calling a remote API, and prove their computation via cryptographic proof of the model's logits?

**Why it matters:** The current design requires Oracle Nodes to call a remote LLM API (Anthropic/OpenAI) and prove the call via TLSNotary. This has several limitations:
- Each Oracle Node pays for API calls (cost scales with number of Oracle Nodes)
- Dependency on LLM provider availability and pricing
- TLSNotary proves the API was called, not that the computation was correct
- LLM provider could theoretically return different results to different callers

Local execution with proof of logits would:
- Eliminate per-call API costs (one-time model download)
- Remove LLM provider as a dependency and trust assumption
- Prove the actual computation, not just that an API was called
- Enable deterministic verification (same model + same input = same logits)

**Subquestions to research:**

#### 5a. What is Proof of Logits?
The LLM produces logits (raw output probabilities) at each token generation step. If two parties run the same model with the same weights on the same input with the same sampling parameters, they should produce identical logits. Publishing logits alongside scores allows independent verification that the model genuinely produced the output.

#### 5b. Deterministic Inference
- Can modern LLMs produce bit-identical outputs given the same input + weights + parameters?
- What about GPU non-determinism (floating point ordering)?
- Does `llama.cpp` (CPU inference) provide deterministic output?
- Does `vLLM` support deterministic mode?
- What about quantized models (GGUF, GPTQ, AWQ) — are they more deterministic than full-precision?

#### 5c. Which Models Can Run Locally?
- The current design pins `claude-opus-4-6` (Anthropic) — this is a proprietary, closed-weight model. Cannot run locally.
- Switching to an open-weight model (Llama, Mistral, Qwen, DeepSeek) would be required.
- **Key question:** Is there an open-weight model capable enough for the nuanced scoring task (consensus quality, reputation assessment, geographic diversity optimization)?
- Benchmark: run the scoring prompts through candidate open-weight models and compare output quality to Claude.

#### 5d. zkML Approaches
- **EZKL**: Converts ML models to ZK circuits. Can prove inference was computed correctly. Current limitations: model size constraints, proof generation time (minutes to hours for large models).
- **Modulus Labs**: zkML research. ZKML proofs for neural network inference.
- **Giza / ONNX-to-Cairo**: Prove ML inference on StarkNet.
- **Practical question:** Can any zkML framework handle a multi-billion-parameter LLM inference in reasonable time? Current state of the art suggests no — zkML works for small models (image classifiers, simple NNs) but not LLM-scale inference.

#### 5e. Optimistic Verification
- Instead of proving every inference, assume honesty and challenge only on dispute.
- Oracle Node publishes: model hash, input hash, output, logits sample.
- Any challenger can re-run the inference and dispute if results differ.
- Slashing mechanism for proven dishonesty.
- This avoids the prohibitive cost of zkML for LLMs.

#### 5f. TEE-Based Local Inference
- Run inference inside a Trusted Execution Environment (Intel TDX, AMD SEV, AWS Nitro).
- TEE attests that the specific model binary ran on the specific input.
- Weaker than ZK proofs (trust the hardware), but practical for LLM-scale models.
- Similar to what Opacity does on the notary side, but applied to the inference itself.

**Action:** This is a significant architectural alternative. Research each subquestion. If viable, this could replace the entire TLSNotary-based LLM proving mechanism with something fundamentally stronger (proving computation, not just API calls). However, it requires switching from proprietary to open-weight models, which affects scoring quality. Evaluate the tradeoff.

### 6. LLM API Costs Per Scoring Round
**Question:** What is the per-round cost if each Oracle Node pays for their own LLM API calls?

**Why it matters:** With N Oracle Nodes each making 2 API calls (Step 1 + Step 2) per round, the total network cost is `N * 2 * cost_per_call`. If the scoring prompt is ~50 KB input and ~20 KB output, this is non-trivial for frontier models.

**Action:** Estimate costs for:
- Claude Opus (current pinned model) — input + output token pricing
- GPT-4o — as comparison
- Open-weight alternatives (if local execution is viable) — hardware cost only
- Multiply by expected Oracle Node count (5-20) and weekly cadence

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

**Action:** Validate these durations against: LLM API response times, TLSNotary proof generation time, IPFS publication time, on-chain transaction confirmation time. Consider timezone distribution of Oracle Node operators. Test on devnet with real timings.

### 9. IPFS Pinning Service Selection
**Question:** Which IPFS pinning service to use? What are the costs?

**Candidates:** Pinata, web3.storage, Filebase, self-hosted IPFS node with cluster.

**Action:** Compare pricing (per GB stored, per GB bandwidth), reliability, API quality, and evaluate whether self-hosting is worth the operational overhead. Estimate monthly storage requirements based on proof sizes and scoring cadence.

### 10. Proof Size Empirical Measurement
**Question:** How large are TLSNotary attestation artifacts and Opacity proofs in practice?

**Why it matters:** Affects IPFS storage costs, download times for community verification, and whether proofs can reasonably be included in on-chain memo fields (if ever desired).

**Action:** Generate actual proofs in items 2 and 3 above and measure artifact sizes. Document typical sizes for: small API calls (KYC check), medium API calls (VHS data fetch), large API calls (LLM scoring).

### 11. Oracle Node Minimum Hardware Requirements
**Question:** What hardware does an Oracle Node operator need?

**Components:** Run TLSNotary client, call LLM API (or run model locally if item 5 is viable), publish to IPFS, submit on-chain transactions.

**Action:** Profile resource usage during the PoC tests (items 2, 3). Document minimum CPU, RAM, bandwidth, and storage requirements. Consider whether Oracle Nodes can run on commodity VPS instances.

### 12. LLM Provider Model Stability
**Question:** Can LLM providers silently change model behavior while keeping the same model ID?

**Why it matters:** If Anthropic updates `claude-opus-4-6` weights without changing the model identifier, different Oracle Nodes calling at different times could get meaningfully different results — not from non-determinism, but from actual model changes.

**Action:** Research LLM provider versioning policies. Anthropic pins model versions with date suffixes (e.g., `claude-opus-4-6-20250620`). Confirm that pinning to a dated version guarantees identical model weights over time. Document the policy and include it in the scoring configuration.

---

## Research Priority Order

| Priority | Item | Blocking? |
|----------|------|-----------|
| 1 | Opacity pricing (#1) | Yes — affects architecture if too expensive |
| 2 | Opacity SDK integration test (#2) | Yes — validates core assumption |
| 3 | TLSNotary notary-server setup (#3) | Yes — needed for Oracle Node path |
| 4 | LLM API costs (#6) | Yes — affects Oracle Node economics |
| 5 | Local LLM with proof of logits (#5) | No — architectural alternative, research in parallel |
| 6 | TLSNotary fork decision (#4) | No — depends on #3 results |
| 7 | Proof size measurement (#10) | No — measured during #2 and #3 |
| 8 | PFT bond justification (#7) | No — can be adjusted before mainnet |
| 9 | Commit-reveal timing (#8) | No — validated during devnet testing |
| 10 | IPFS pinning service (#9) | No — commodity decision |
| 11 | Oracle Node hardware (#11) | No — measured during testing |
| 12 | LLM model stability (#12) | No — policy question, quick to resolve |
