# Feedback Analysis: Phased Validator Scoring Review

Analysis of [external review](https://gist.github.com/goodalexander/ce24d011d90c00f5180e20db0f121cd7) against our current Design and ImplementationPlan. Each item gets a verdict: **Accept**, **Accept with modification**, **Defer**, or **Reject** — with reasoning.

---

## Architectural Changes (The Big Ones)

### 1. Shrink the LLM's role — constrained rubric instead of free-form 0-100 scores

**Reviewer says:** Don't let the LLM emit a final 0-100 score with long reasoning. Instead, have it emit a constrained schema (e.g., `operational_reliability: 0-4`, `software_diligence: 0-4`, etc.) and compute the final score mechanically in code. Split reasoning into a non-authoritative artifact generated after the score.

**Current plan:** LLM ingests all validator data in one prompt, outputs score 0-100 + written reasoning per validator. Reasoning is part of the published artifact.

**Resolution: Rejected**

LLM scoring is a central part of the project's vision. The LLM producing holistic scores with reasoning is a deliberate product decision, not a technical oversight. Shrinking the LLM's role to a constrained rubric with mechanical score computation conflicts with this direction. The technical advantages (better determinism, smaller transcripts) are real but are outweighed by the product/vision argument.

**No changes to plan.**

---

### 2. Separate candidate evaluation from set construction (diversity as set-level optimization)

**Reviewer says:** Diversity is a set-level property, not a per-candidate property. Don't ask the LLM to consider diversity. Instead: (1) compute independent base candidate scores, (2) run a deterministic set-construction algorithm that optimizes base score + diversity (ASN, jurisdiction, datacenter, operator caps, churn limits).

**Current plan:** LLM considers "geographic/ISP/datacenter diversity" as part of per-validator scoring, then top-35 mechanical cutoff.

**Resolution: Rejected**

Linked to item #1 — keeping the LLM as the central scorer means it retains responsibility for considering diversity as part of its holistic assessment. The reviewer's argument that diversity is a set-level property is technically correct, but the project vision prioritizes the LLM's comprehensive judgment over mechanical separation. The scoring prompt should clearly instruct the LLM on how to weigh diversity factors (see item #28 for specifying those dimensions).

**No changes to plan.**

---

### 3. Treat determinism as a hard gate, not an assumption

**Reviewer says:** vLLM reproducibility is not guaranteed by default; its batch-invariance is beta and requires H100/H200-class hardware. SGLang's deterministic mode is documented but with ~34% overhead. Don't lock the network into a hardware assumption before a reproducibility harness passes. Add a real reproducibility harness milestone before Phase 2.

**Current plan:** Milestone 0.3 is "Determinism Research Documentation" — research and docs, not implementation. Hardware assumption is speculative (A40, L4, RTX 4090, A100 as candidates).

**Resolution: Accepted**

Phase 1 doesn't need determinism (foundation-only), so this doesn't block shipping. But before Phase 2, we need actual measurements, not just research docs. Milestone 0.3 upgraded to include a reproducibility harness that runs during Phase 1 and gates Phase 2 entry. The harness measures output equality across same worker, different workers, same GPU type, different datacenters, warm vs cold starts. Phase 1 Decision Gate now requires the harness to pass with >99% output equality.

**Impact on plan:** Milestone 0.3 expanded. Phase 1 Decision Gate updated.

---

### 4. Phase 3 is research, not implementation backlog

**Reviewer says:** Don't commit Phase 3 proof-of-logits to a calendar. Treat it as a research program with a kill switch and a simpler fallback. Split Phase 3 into 3A (authoritative converged content — foundation still publishes but content comes from validator convergence) and 3B (publication decentralization — remove the foundation as publication choke point).

**Current plan:** Phase 3 is presented as ~5-7 weeks of implementation with concrete milestones.

**Verdict: Accept with modification**

The reviewer correctly identifies that Phase 3 has genuine research risk (logit-level determinism may not work reliably), and our plan currently papers over that by presenting it as a linear implementation sequence.

The 3A/3B split is valuable:
- **3A** (content authority transfer): Validators converge, foundation publishes the converged result. This is achievable as soon as Phase 2 convergence is proven. No logit proofs needed.
- **3B** (publication authority transfer): Remove foundation as the single publisher. This requires protocol-level changes (threshold signing, multi-publisher, or on-chain derivation). This is a harder, separate problem.

The modification: I'd keep the logit commitment work (Milestone 3.1-3.2) as defined but explicitly mark it as "research milestone — proceed only if Phase 2 convergence rates justify it." If Phase 2 shows >99% output convergence reliably, logit proofs become less critical (the system is already working). If convergence is spotty, logit proofs become the diagnostic tool for why.

**Resolution: Accepted with modification — 3A/3B split with explicit fallback**

Restructure Phase 3 into two sub-phases:
- **Phase 3A** (content authority transfer): Validators converge on scores, foundation publishes the converged result. Achievable as soon as Phase 2 convergence is proven. No logit proofs needed. This can proceed on a calendar.
- **Phase 3B** (publication decentralization): Remove foundation as single publisher. Requires protocol-level changes (threshold signing, multi-publisher, or on-chain derivation). Marked as future work.

Logit commitment milestones (3.1-3.2) are kept but marked as research — proceed only if Phase 2 convergence rates justify the investment. If Phase 2 shows >99% output convergence reliably, logit proofs are less critical (the system already works). The explicit fallback: "If logit determinism proves infeasible, the system works at Phase 2 + 3A level with output-level convergence."

**Impact on plan:** Restructure Phase 3 in both Design doc and ImplementationPlan. Mark logit milestones as conditional/research. Add fallback path. Update Phase 3 goal and summary.

---

### 5. Foundation authority isn't truly removed in Phase 3

**Reviewer says:** Even with perfect validator convergence, the foundation still controls: snapshot assembly, round announcements, VL signing/distribution path, IPFS gateway. Phase 3 isn't "foundation replaced" unless those choke points are also addressed.

**Current plan:** Phase 3 says "the foundation becomes one validator among many" but doesn't address snapshot production, list signing, or publication monopolies.

**Verdict: Accept**

This is an honest assessment. Our current Phase 3 narrative overstates the decentralization achieved. With all the Phase 3 milestones complete as designed, the foundation would still be:
- The sole data collector/snapshot assembler
- The sole round announcer
- The sole VL signer
- The primary IPFS gateway operator

The validators prove computation honestly, but the foundation controls the inputs and the delivery channel.

This doesn't invalidate the plan — it means we should be precise in our language and honest about the trust model at each phase. Phase 3 as currently scoped achieves "converged scoring authority" not "full decentralization."

**Resolution: Accepted — update trust model language to be precise**

The current Phase 3 narrative overstates decentralization. Even with all Phase 3 milestones complete, the foundation still controls: snapshot assembly, round announcements, VL signing/distribution, and IPFS gateway. Phase 3 as scoped achieves "converged scoring authority" — validators collectively determine the UNL content — but the foundation retains control of the inputs and delivery channel.

Update the Trust Model Progression table to be precise about what Phase 3 actually achieves. Acknowledge remaining centralization vectors explicitly. The 3A/3B split from item #4 captures this: 3A transfers content authority, 3B (future work) transfers publication authority.

**Impact on plan:** Update Trust Model Progression table in Design doc. Update Phase 3 goal language. Acknowledge remaining foundation roles explicitly.

---

## Implementation-Level Changes

### 6. Per-validator independent scoring instead of single giant prompt

**Reviewer says:** Scoring all validators in one prompt creates rank interactions, order sensitivity, context-window pressure, and hard-to-debug drift. Score each validator independently with the rubric, then build the set mechanically.

**Current plan:** "LLM ingests all candidate validator data packets in a single prompt."

**Resolution: Rejected**

Evaluated pros and cons in detail:

Pros: order invariance, isolation, debuggability, context-window freedom, parallelizable.

Cons: loses cross-validator comparison (the LLM can't calibrate relative to peers), calibration drift risk (same metrics might score differently across independent calls without seeing the distribution), loses the holistic narrative that the project values.

The cons outweigh the pros for this project. The single-prompt approach lets the LLM see the full validator landscape and produce a comparative assessment, which aligns with the vision of LLM-as-central-scorer. The order sensitivity risk is real but can be mitigated through prompt design (e.g., sorting validators deterministically by public key before including them in the prompt).

**No changes to plan.**

---

### 7. Pin the full execution manifest, not just model weights

**Reviewer says:** Hash and publish: model snapshot revision, every weight shard, tokenizer files, config files, prompt template, structured-output schema, inference engine version, attention backend, dtype, quantization mode, container image digest, CUDA/driver/torch version.

**Current plan:** "Model version and weight file hash (SHA-256 of the GGUF or safetensors file) are published with every scoring round."

**Resolution: Accepted**

A weight file hash alone doesn't catch different tokenizer versions, chat templates, inference engine configs, or structured-output backends — any of which could cause divergence. A full `ScoringManifest` captures every convergence-critical parameter and is published alongside each round. Also accepting the point to construct raw prompt strings instead of relying on chat-template defaults.

**Impact on plan:** Manifest definition added to Phase 0 model selection. Published with every scoring round. Phase 2 convergence checks compare manifests as first diagnostic step.

---

### 8. Separate raw evidence from normalized snapshot

**Reviewer says:** Publish both `raw/` (raw VHS responses, raw chain reads, raw IP-classification responses, each timestamped and hashed) and `normalized_snapshot.json` (the exact scorer input after deterministic normalization). This lets validators verify both the source evidence and the deterministic transformation.

**Current plan:** Single `snapshot.json` that combines everything.

**Resolution: Accepted**

If a validator disputes their score, they need to verify what the source APIs actually returned vs what ended up in the snapshot. Publishing raw responses alongside the normalized snapshot creates a verifiable audit chain: raw data → normalization → snapshot → scoring.

**Impact on plan:** Milestone 1.2 archives raw API responses. Milestone 1.5 IPFS audit trail includes `raw/` directory alongside the normalized snapshot.

---

### 9. Observer-dependent metrics need multi-vantage or low-weight treatment

**Reviewer says:** Latency, peer count, and topology are observations from a particular vantage point, not universal truths. Either collect from multiple observers and aggregate, or keep them low-weight.

**Current plan:** VHS data (single observer) used directly for scoring.

**Resolution: Accepted — low weight for observer-dependent metrics**

VHS sees the network from one vantage point. Keep it simple: the scoring prompt gives low weight to observer-dependent metrics (latency, peer count, topology) relative to objective metrics (agreement scores, uptime, version). No need for multiple observers in Phase 1.

**Impact on plan:** Scoring prompt in Milestone 1.3 instructs the LLM to weight observer-dependent VHS metrics lower.

---

### 10. MaxMind licensing concern

**Reviewer says:** MaxMind's EULA for self-serve databases says you can't use them to support a B2B product/service/platform or display extracted data points outside your organization. If you publish MaxMind-derived fields to IPFS, you need legal review.

**Current plan:** MaxMind GeoIP2 data published as part of the validator snapshot to IPFS.

**Verdict: Accept**

This is a legitimate legal risk we hadn't considered. Publishing raw MaxMind data (ISP name, datacenter, city) to IPFS could violate their license terms.

Solutions:
1. **Publish normalized categories instead of raw data.** Instead of `"isp": "DigitalOcean"`, publish `"datacenter_provider": true`, `"cloud_concentration_class": "major"`. Our own taxonomy, derived from but not reproducing MaxMind data.
2. **Get commercial license.** MaxMind offers redistribution licenses — cost TBD.
3. **Use alternative data sources.** ASN data is public (WHOIS/RIR databases). Country from IP is available from multiple free sources. We may not even need MaxMind for the fields that matter.

Option 1 is the safest and aligns with the reviewer's broader point about publishing normalized categories rather than vendor data.

**Resolution: Accepted — split data sources by publishability**

Use ASN (Autonomous System Number) data from public WHOIS/RIR databases for ISP and cloud provider identification. ASN data is public, freely redistributable, and provides the same information needed for diversity scoring (ISP name like "DigitalOcean", AS number, cloud provider classification). This data can be published to IPFS without licensing concerns.

Use MaxMind GeoIP2 only for city/country geolocation data, which is kept internal to the foundation's scoring pipeline and not published to IPFS. The scoring snapshot published to IPFS includes ASN-derived ISP/provider fields but not MaxMind-derived geolocation fields.

Why not all via MaxMind: MaxMind's EULA for self-serve databases restricts republishing extracted data points. Using public ASN data for the publishable fields avoids this licensing risk entirely, while MaxMind is only used internally where the EULA permits it.

**Impact on plan:** Update Milestone 1.2 to use ASN data for ISP/provider fields (publishable) and MaxMind only for geolocation (internal). Update the snapshot schema to reflect this split. Note the licensing rationale.

---

### 11. Add churn controls to UNL selection

**Reviewer says:** Without explicit churn controls, top-35 mechanics will oscillate. Need: max entries changed per round, enter threshold > exit threshold, cooldown before removal, probation before inclusion, fallback to previous list if too much turnover.

**Current plan:** Simple cutoff + top-35. No churn management.

**Resolution: Accepted with simplification**

The oscillation problem is real: small LLM score fluctuations between rounds could cause validators near the cutoff boundary to swap in and out every round, hurting consensus stability.

After evaluating several approaches (percentage caps, hysteresis thresholds, incumbent advantage), settled on a single mechanism:

**Minimum score gap for replacement:** A challenger only displaces an incumbent UNL validator if the challenger's score exceeds the incumbent's score by at least X points. If the gap is smaller, the incumbent stays. This directly prevents oscillation from minor score fluctuations while still allowing genuinely better validators to enter. One config value, tuned on testnet by observing natural score variance between rounds.

**Impact on plan:** Add minimum score gap rule to the UNL inclusion logic in Milestone 1.3. The exact gap value will be determined during devnet testing (Milestone 1.9) by measuring how much scores naturally fluctuate.

---

### 12. Orchestrator should be an explicit state machine with idempotent steps

**Reviewer says:** Use explicit states (`COLLECTING`, `NORMALIZED`, `SCORED`, `SELECTED`, `VL_SIGNED`, `IPFS_PUBLISHED`, `ONCHAIN_PUBLISHED`, `COMPLETE`, `FAILED`). Every step must be idempotent. Build `dry_run`, `replay_round(round_id)`, `rebuild_from_raw(round_id)`.

**Current plan:** Milestone 1.7 describes a linear orchestrator with step logging.

**Verdict: Accept**

A proper state machine with idempotent steps is strictly better than "linear pipeline with logging." It enables:
- Resuming from failure (don't re-run scoring if IPFS upload failed)
- Debugging (replay a round from any intermediate state)
- Dry runs (test the pipeline without publishing)
- Audit (every state transition is recorded)

The `replay_round` and `rebuild_from_raw` capabilities are genuinely useful for debugging and will save significant time during development and operations.

Also accept the reviewer's point about not using APScheduler in-process — use a round table in Postgres with advisory locks for singleton orchestration.

**Resolution: Accepted**

Redesign Milestone 1.7 as an explicit state machine with idempotent steps. States: `COLLECTING`, `NORMALIZED`, `SCORED`, `SELECTED`, `VL_SIGNED`, `IPFS_PUBLISHED`, `ONCHAIN_PUBLISHED`, `COMPLETE`, `FAILED`. Each step is idempotent — rerunning from any state produces the same result. Add `dry_run`, `replay_round(round_id)`, and `rebuild_from_raw(round_id)` capabilities. Replace APScheduler with a round table in Postgres with advisory locks for singleton orchestration.

**Impact on plan:** Redesign Milestone 1.7 around a state machine pattern. Add replay/rebuild capabilities. Replace APScheduler with Postgres-based scheduling.

---

### 13. VL signing key needs stronger security treatment

**Reviewer says:** Treat VL signing key as a separate security domain. Use a minimal signing service or HSM-backed function. Don't let the general FastAPI process hold the raw long-lived secret. Have a manual offline emergency publication tool.

**Current plan:** Publisher token stored as environment variable, used by the FastAPI service directly.

**Verdict: Accept with modification**

The reviewer is right that this key is governance-critical — compromise means an attacker can publish a malicious UNL. However, HSM/Vault in Phase 1 for a testnet with ~30 validators may be over-engineering.

Modification: For Phase 1/devnet/testnet, environment variable is acceptable but with these mitigations:
- Separate devnet and testnet keys (already implied but make explicit)
- Key rotation runbook documented
- Access logging for the signing operation
- Manual offline signing tool for emergencies

For mainnet (if ever), upgrade to HSM/Vault transit.

**Resolution: Accepted with modification**

For Phase 1/testnet, environment variable storage is acceptable with these mitigations: separate devnet and testnet keys, key rotation runbook, access logging for signing operations, manual offline emergency signing tool. For mainnet, upgrade to HSM/Vault transit.

**Impact on plan:** Expand Milestone 1.4 security note. Add key rotation runbook, access logging, and offline signing tool.

---

### 14. IPFS publication should mirror over plain HTTPS and support multiple gateways

**Reviewer says:** Pin to more than one pinning backend. Mirror over plain HTTPS. Let validators fetch by CID through any gateway, not just yours. If your only accessible gateway is foundation-run, you kept an unnecessary dependency.

**Current plan:** Single self-hosted IPFS node at `ipfs-testnet.postfiat.org`.

**Verdict: Accept with modification**

The principle is correct — a single foundation-run IPFS gateway is a centralization point. However, for Phase 1 on testnet, a single gateway is pragmatically fine.

Modification: In Phase 1, document that validators can use any IPFS gateway to fetch by CID (the data is content-addressed — any gateway works). Add Pinata or web3.storage as a secondary pin for redundancy. The scoring service should also serve the audit trail over plain HTTPS as a fallback.

**Resolution: Accepted — document IPFS redundancy and HTTPS fallback**

For Phase 1 on testnet, a single gateway is pragmatically fine. But document that: (a) validators can use any IPFS gateway to fetch by CID (content-addressed — any gateway works), (b) add a secondary pinning service (Pinata or web3.storage) for redundancy, (c) the scoring service serves audit trail artifacts over plain HTTPS as a fallback.

**Impact on plan:** Add to Milestone 1.5: secondary pin service, HTTPS fallback, documentation that any IPFS gateway works.

---

### 15. On-chain hash construction must be domain-separated, not loose string concatenation

**Reviewer says:** Never hash concatenated strings loosely. Use typed encoding: `sha256(domain || version || round_uint64 || artifact_hash || cid_bytes || ...)`

**Current plan:** Commit hash is `sha256(scores_json + salt + round_number)` — string concatenation.

**Verdict: Accept**

This is a subtle but real correctness issue. Loose string concatenation can create ambiguities (is `"score12"+"3"` the same as `"score1"+"23"`?). Domain-separated hashing with fixed-width fields is the standard approach for cryptographic commitments.

Small change, high value. Define a canonical binary encoding for all hash preimages.

**Resolution: Accepted**

Loose string concatenation (`sha256(scores_json + salt + round_number)`) creates ambiguity in hash preimages. Domain-separated hashing with a binary encoding using fixed-width fields is the standard approach for cryptographic commitments. Define a canonical binary encoding for all hash preimages: `sha256(domain_tag || version_uint8 || round_uint64 || artifact_hash_32bytes || salt_32bytes)`.

**Impact on plan:** Update Milestone 2.1 (Commit-Reveal Protocol Design) to define exact binary encoding of all hash preimages. Update the commit hash format in memo definitions.

---

### 16. Convergence monitoring needs more diagnostic layers

**Reviewer says:** Publish four layers: (1) artifact match — exact hash, (2) selection match — same UNL set, (3) first divergence analysis — first field/token where divergence appears, (4) environment diff — which manifest fields differed.

**Current plan:** Milestone 2.5 has three levels: exact match, score-level match, UNL-level match.

**Verdict: Accept**

The reviewer's breakdown is more useful for debugging. Especially the "first divergence analysis" and "environment diff" — these are what you actually need when troubleshooting why validators diverged. Without them you just know "they disagree" but not why.

**Resolution: Accepted with modification — environment diff only, no token-level analysis**

Add environment diff as a diagnostic layer: when validators diverge, compare their execution manifests to identify which configuration field differs (SGLang version, CUDA driver, model hash, etc.). Drop the first-divergence token-level analysis for now — it's overkill for Phase 2. The three existing levels (exact match, score-level, UNL-level) plus environment diff is sufficient.

**Impact on plan:** Add environment diff comparison to Milestone 2.5 convergence analysis.

---

### 17. Phase 2 devnet testing must include truly independent execution environments

**Reviewer says:** At least one devnet test series must use truly independent execution environments, not a shared RunPod endpoint. Otherwise you're proving transport symmetry, not independent execution.

**Current plan:** Milestone 2.8 says "Use RunPod cloud mode for simplicity (same endpoint as the scoring service)."

**Verdict: Accept**

If all validators hit the same RunPod endpoint, you're testing that the commit-reveal plumbing works, not that independent inference converges. Each validator (or at least a subset) needs their own endpoint/pod for the convergence test to be meaningful.

**Resolution: Accepted**

If all validators hit the same RunPod endpoint during devnet testing, we're proving transport symmetry, not independent execution. At least a subset of devnet validators must use their own GPU or separate RunPod endpoint for the convergence test to be meaningful.

**Impact on plan:** Update Milestone 2.8 to require at least 2 out of 4 devnet validators to use independent execution environments (separate RunPod endpoints or local GPU).

---

### 18. Spot-check challenge positions must be unpredictable

**Reviewer says:** Challenges must be derived from something validators can't know at commit time. A future validated ledger hash is the natural source.

**Current plan:** "Pick a random token position K" — no specification of randomness source.

**Verdict: Accept**

If challenge positions are known in advance, a cheating validator could precompute correct logits at likely challenge positions while fabricating the rest. Using a future ledger hash (from after the reveal window closes) as the challenge seed makes this impossible.

**Resolution: Accepted — use future ledger hash as challenge seed**

Derive spot-check challenge positions from a validated ledger hash that closes after the reveal window. Validators cannot know this hash at commit time, making it impossible to precompute logits at only the challenged positions.

**Impact on plan:** Update Milestone 3.2 to specify challenge seed derivation from a post-reveal ledger hash.

---

## Tooling & Stack Recommendations

### 19. Use SGLang over vLLM for determinism R&D

**Reviewer says:** SGLang's deterministic path is explicitly documented and tested. vLLM's batch invariance is beta and tied to H100+ class hardware.

**Resolution: Accepted — SGLang as default everywhere**

SGLang as the primary inference engine across all phases. It has an explicitly documented deterministic inference path that works on cheaper hardware than vLLM's equivalent. vLLM kept only as a documented fallback if SGLang proves unsuitable during the reproducibility harness testing (Milestone 0.3).

**Impact on plan:** All references to "vLLM or SGLang" updated to "SGLang" as default. vLLM mentioned as fallback only.

---

### 20. Use safetensors, not GGUF; pin full HuggingFace snapshot revision

**Reviewer says:** GGUF is not appropriate for governance-critical artifacts. Use safetensors with HuggingFace snapshot revision pinning and full file manifest hashing.

**Resolution: Accepted**

GGUF is for consumer/hobbyist inference (llama.cpp). For a production governance system using SGLang, safetensors is the standard format. HuggingFace snapshot revision pinning gives a deterministic, immutable reference to the exact model files.

**Impact on plan:** All GGUF references replaced with safetensors. Model pinning uses HuggingFace snapshot revision + full file manifest hashing.

---

### 21. Use canonical JSON (RFC 8785 / JCS) for anything that gets hashed

**Reviewer says:** Use canonical JSON serialization for any artifact that gets hashed. Also use `orjson` for speed.

**Resolution: Accepted**

Standard JSON serialization is non-deterministic (key ordering, whitespace, number formatting). If two validators serialize the same data differently, their hashes diverge even though the content is identical. Canonical JSON eliminates this. Essential for Phase 2 convergence where multiple parties hash the same logical content.

**Impact on plan:** Added as a requirement in Milestone 1.1 repo setup. All hashed artifacts use canonical JSON serialization.

---

### 22. Python stack recommendations (uv, ruff, mypy --strict, httpx, structlog, etc.)

**Reviewer says:** Specific Python tooling recommendations for both new repos.

**Verdict: Accept mostly**

Most of these are modern best practices: `uv` over pip, `ruff` over black/flake8, `httpx` over requests, `structlog` over stdlib logging, `pydantic-settings` (already planned). `hypothesis` for property testing is a good call for the normalization/canonicalization code.

The one thing I'd skip for now: `sqlalchemy + alembic` may be over-engineered for Phase 1. Raw SQL with a migration script might be simpler for the small schema. But if the team prefers an ORM, fine.

**Resolution: Accepted — keep it simple**

Use modern Python tooling without over-engineering: `uv` for dependency management, `ruff` for linting, `httpx` for HTTP, `structlog` for structured logging, `pydantic-settings` (already planned). Skip `sqlalchemy + alembic` for Phase 1 — raw SQL with migration scripts is simpler for the small schema. Add `hypothesis` for property testing of canonicalization/normalization code.

**Impact on plan:** Update Milestone 1.1 repo setup with agreed tooling.

---

### 23. Ops/security stack (Prometheus, Sentry, Trivy, Cosign, SOPS)

**Reviewer says:** Add container scanning (Trivy), SBOM (Syft), image signing (Cosign), secret management (SOPS/Vault).

**Verdict: Defer to Phase 2**

For Phase 1 on testnet, this is over-engineering. We already have Grafana/Loki for observability. Container scanning and image signing are mainnet-grade practices. Add them when the system is stable and heading toward production.

Prometheus metrics endpoint and structured logs: accept for Phase 1 — low cost, high value.

**Resolution: Accepted — use existing Loki stack, defer container security**

The scoring service should output structured JSON logs (compatible with existing Promtail → Loki → Grafana stack already deployed). Optionally expose a `/metrics` endpoint (Prometheus format) for Grafana to scrape operational metrics (rounds completed, scoring latency, IPFS upload time). No need to add a separate Prometheus stack — use what's already deployed.

Defer container scanning (Trivy), SBOM (Syft), image signing (Cosign), and secret management (SOPS/Vault) to Phase 2 or mainnet.

**Impact on plan:** Note structured JSON logging and optional metrics endpoint in Milestone 1.1. Reference existing Loki stack.

---

## Process & Documentation Changes

### 24. Add a legal/licensing workstream in Phase 0

**Reviewer says:** Specifically for IP-intelligence vendor licensing, public redistribution of derived fields, identity attestation wording.

**Verdict: Accept**

The MaxMind licensing issue (item #10) is real. This doesn't need to be a big workstream — one legal consultation focused on: (a) can we publish MaxMind-derived categories to IPFS, (b) what identity data can we publish on-chain. A day of work but needs to happen early.

**Resolution: Accepted**

Add a brief legal/licensing assessment to Phase 0 covering: (a) MaxMind data publication constraints (already resolved via ASN split — see item #10), (b) what identity attestation data can be published on-chain. This is a one-day effort, not a major workstream.

**Impact on plan:** Add a licensing/legal assessment step to Phase 0, alongside Milestone 0.4.

---

### 25. Add specific stability tests for scoring

**Reviewer says:** Add these tests: candidate-order permutation, field-order permutation, one-candidate-added/removed stability, replay-same-snapshot 10 times, current vs previous prompt regression.

**Resolution: Already handled in Task 1**

These tests were already added as Milestone 1.9.4 (Scoring stability testing) during Task 1. Includes: replay consistency, one-candidate-added/removed stability, score variance measurement for churn control tuning. No additional changes needed.

---

### 26. Move identity portal off the critical path

**Reviewer says:** Identity portal (Milestone 3.5) is useful but not critical path. Make it explicitly parallel.

**Verdict: Accept**

The plan already says "Dependencies: None (can be built in parallel)" but it's listed in Phase 3, implying it gates the full system test. Make the independence explicit. It can be built anytime during Phase 1-3.

**Resolution: Accepted — make independence explicit, but clarify prerequisite**

The identity portal (Milestone 3.5) is independent infrastructure work — it can be built anytime during Phase 1-3 and does not gate the system test. However, validators must have on-chain identity data (submitted via any method, including the existing scoring-onboarding flow) before scoring begins. The portal is a convenience layer, not the only path to identity data.

**Impact on plan:** Note in Milestone 3.5 that it is parallel work, not gating Phase 3. Add a note that identity data (via any submission method) is a prerequisite for meaningful scoring, not the portal itself.

---

### 27. Identity data should be minimal (status only, no PII)

**Reviewer says:** Publish only: verified/not-verified, institutional/individual/unknown, domain-attested/not-attested. No PII.

**Verdict: Accept**

This is already roughly our intent but worth making explicit. The on-chain memo should contain attestation status, not identity details.

**Resolution: Accepted**

On-chain identity data should contain only attestation status, not PII. The snapshot should include: `verified: true/false`, `entity_type: institutional/individual/unknown`, `domain_attested: true/false`. No names, addresses, or personal details. This aligns with the existing intent but needs to be made explicit in the snapshot schema.

**Impact on plan:** Update Milestone 1.2 snapshot schema to use attestation-status-only identity fields. Clarify in the data collection step that no PII is collected or published.

---

### 28. Diversity scoring dimensions need explicit specification

**Reviewer says:** Decide exactly which concentration surfaces matter (country, ASN, operator, cloud provider, datacenter, jurisdiction, institution class) and whether they are hard caps, soft penalties, or tie-breakers.

**Resolution: Accepted**

The current plan tells the LLM to "consider geographic/ISP/datacenter diversity" but doesn't define what that means concretely. Since we're keeping the LLM as the holistic scorer (items #1, #2 rejected), the diversity dimensions need to be explicitly specified in the scoring prompt so the LLM knows exactly what to evaluate and how heavily to weigh each factor.

The prompt should define the relevant concentration surfaces (country, ASN, operator, cloud provider, datacenter) and give the LLM clear guidance on how to factor them in. Exact dimensions and weights will be refined during devnet testing.

**Impact on plan:** Update the scoring prompt design in Milestone 1.3 to explicitly enumerate diversity dimensions and weighting guidance for the LLM.

---

### 29. "No rewards" model needs explicit participation fallback rules

**Reviewer says:** If no rewards, you need low-cost round cadence, minimal operator burden, reputational incentives, public participation metrics, and hard fallback rules if participation drops.

**Verdict: Accept as documentation, not code change**

The XRPL model (no validator rewards) works for XRPL. We're following the same model. But the reviewer is right that we should document: what happens if participation drops below threshold? Answer: fall back to foundation UNL (which is already the Phase 2 design — foundation remains authoritative).

**Resolution: Accepted — document participation fallback rules**

The XRPL model (no validator rewards) works, but we should document: if participation drops below threshold, fall back to foundation UNL (which is already the Phase 2 design — foundation remains authoritative). Document minimum participation requirements and how round cadence affects operator burden.

**Impact on plan:** Add participation fallback rules to Phase 2 protocol design (Milestone 2.1). Document minimum participation thresholds and fallback behavior.

---

### 30. Validator onboarding needs an Ansible/Terraform path for institutions

**Reviewer says:** Three installation paths: interactive shell, non-interactive .env/Compose, Ansible/Terraform module. For institutions, the third is the real one.

**Verdict: Defer**

Shell script + Docker Compose is sufficient for testnet. Ansible/Terraform is mainnet-grade. If an institutional validator asks for it during testnet, build it then.

**Impact on plan:** None for now. Add to future work.

---

### 31. Add failure drills to Phase 3 system test

**Reviewer says:** Test: foundation doesn't announce a round, IPFS gateway down, one-third of validators fail to reveal, list publication misses before expiry, model upgrade half-applied, signing service unavailable.

**Verdict: Accept**

These are the scenarios that matter for operational resilience. The current Milestone 3.6 has some adversarial tests but not these operational failure scenarios.

**Resolution: Accepted**

Add operational failure scenarios to Milestone 3.6: foundation doesn't announce a round, IPFS gateway goes down, one-third of validators fail to reveal, VL expires before the next one is published, model upgrade half-applied across validators, signing service unavailable. These are the scenarios that actually happen in production.

**Impact on plan:** Expand Milestone 3.6 with operational failure drill scenarios.

---

### 32. Layer 2 (proof-of-logits) proves computation consistency, not hardware provenance or non-collusion

**Reviewer says:** Proof-of-logits proves transcript consistency with the model/input/runtime. It does not prove the validator used its own GPU, didn't outsource, or is socially independent.

**Verdict: Accept as documentation clarification**

This is correct and we should be precise about what Layer 2 actually proves. It's not a flaw in the design — it's a scope clarification. Update the design doc to be explicit about what proof-of-logits does and doesn't prove.

**Resolution: Accepted — clarify scope in design doc**

Update the Design doc to be explicit about what proof-of-logits does and doesn't prove:
- **Does prove:** transcript consistency — the validator ran the declared model with the declared input and produced the declared output
- **Does not prove:** that the validator used its own GPU (could outsource), that the validator is socially independent from others, or that the validator didn't collude pre-computation

This is a scope clarification, not a design flaw.

**Impact on plan:** Documentation update to Design_PhasedValidatorScoring.md Phase 3 section.

---

## Items I'd Push Back On or Deprioritize

### 33. Merkle tree over logit positions instead of array hash

**Reviewer says:** Use a Merkle tree so you can commit to one root and reveal partial proofs efficiently.

**Verdict: Defer**

Merkle trees are elegant but add complexity. For Phase 3 R&D, an ordered list of hashes with a root hash of the concatenation is simpler and sufficient. If logit commitment proves viable and we need more efficient proofs, upgrade to Merkle trees later.

---

### 34. Rename "sidecar" to "executor" internally

**Reviewer says:** Half the complexity comes from the fact it may not be co-located.

**Verdict: Reject**

"Sidecar" is a well-understood infrastructure pattern. "Executor" is generic. The current name communicates intent clearly.

---

### 35. Use CAR or deterministic tar manifest for audit bundles

**Reviewer says:** For IPFS audit bundles.

**Verdict: Defer**

IPFS directory pinning is sufficient for Phase 1-2. CAR files are an optimization for when you need portable, self-contained bundles. Not needed yet.

---

## Summary: Decisions on Task 1 (Scoring Architecture)

| Item | Decision | Reason |
|------|----------|--------|
| 1. Constrained rubric output | **Rejected** | LLM scoring is central to project vision |
| 2. Separate set construction | **Rejected** | Linked to #1 — LLM retains holistic scoring role |
| 6. Per-validator independent scoring | **Rejected** | Cons (no cross-comparison, calibration drift, loses holistic view) outweigh pros |
| 11. Churn controls | **Accepted** | Minimum score gap for replacement — one config value, prevents oscillation |
| 28. Diversity dimensions | **Accepted** | Specify dimensions explicitly in the scoring prompt |

---

## Summary: Priority Changes to ImplementationPlan (Remaining Tasks)

### Must-do before writing code (changes the architecture):
1. **Full execution manifest pinning** — not just weight file hash
2. **Canonical JSON for all hashed artifacts**
3. **Raw evidence + normalized snapshot separation**
4. **MaxMind licensing assessment**

### Must-do before Phase 2 (changes the gate criteria):
5. **Reproducibility harness** — real measurements, not just docs
6. **Domain-separated hash construction** for commit-reveal
7. **Independent execution environments** required in devnet testing

### Should-do (improves quality significantly):
8. **State machine orchestrator** with idempotent steps, replay, dry-run
9. **Richer convergence diagnostics** (4 layers)
10. **Unpredictable spot-check positions** from future ledger hash
11. **Phase 3 split into 3A (content authority) and 3B (publication authority)**
12. **Explicit stability tests** (replay, add/remove)
13. **Secondary IPFS pin + HTTPS fallback**
14. **Observer-dependent metric handling**

### Accepted from Task 1 (applied to plan):
15. **Minimum score gap for UNL replacement** — churn control
16. **Diversity dimensions specified in scoring prompt**

### Documentation updates:
17. Update Trust Model Progression to be honest about remaining centralization
18. Clarify what proof-of-logits does and doesn't prove
19. Add participation fallback rules
20. Identity data = attestation status only, no PII
