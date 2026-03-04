# Dynamic UNL: Approaches

High-level architectural approaches for the Dynamic UNL scoring system. Each approach describes a fundamentally different way to organize validator scoring. Specific designs (Design_FoundationCloudAPI, Design_DistributedCloudAPI, and future plans) are detailed implementations of one of these approaches.

---

## Common Infrastructure (All Approaches)

Regardless of which approach is chosen, the following components are shared:

**Data collection:** The foundation collects validator performance data from VHS, Network Monitoring, MaxMind GeoIP2, and SumSub. Every API call is attested by Opacity Network (decentralized MPC-TLS) to prove the data genuinely came from the claimed source. The resulting data snapshot is published to IPFS with all proofs.

**On-chain publication:** The final UNL hash and IPFS CID are published on-chain by the foundation's master account. Nodes verify the fetched UNL JSON against this hash.

**Node-side components:** UNLHashWatcher monitors on-chain hash publications. DynamicUNLManager verifies and applies the score-based UNL. Both hook into the existing ValidatorSite fetch path. Gated behind the `featureDynamicUNL` amendment.

**IPFS audit trail:** Every scoring round publishes the full audit trail to IPFS — data snapshot, proofs, scoring outputs, and final UNL. Content-addressed and independently verifiable.

**Identity gate:** Validators must pass KYC/KYB verification (via SumSub, Opacity-attested) before they can be scored. Institutional domain verification is optional but positively influences scores.

**Model and prompt configuration:** The LLM model version is pinned and published with every scoring round. Prompts are version-controlled in the open-source scoring repository. The choice between proprietary models (Claude, GPT) and open-weight models (Llama, Qwen, DeepSeek) is a configuration decision within any approach, not an approach-level distinction. Open-weight models add independent auditability — anyone can re-run the same prompt to check results — but do not change the architecture.

---

## Governance (All Approaches)

Every approach starts with the foundation holding centralized authority exercised transparently. This authority is designed to be progressively transferred to community governance (DAO, on-chain voting, multisig, or similar mechanism — to be designed separately).

What the foundation controls at launch:
- LLM model selection and prompt authorship
- Scoring cadence and emergency triggers
- Data collection and publication
- KYC/KYB eligibility policy
- Master account and publisher key

What transfers to governance over time:
- Scoring criteria and prompt authorship (medium priority)
- Model selection and cadence (medium priority)
- KYC/KYB policy (medium-high priority)
- Master account and publisher key → multisig (highest priority, transfers last)
- Emergency UNL fallback (may remain with foundation permanently as safety net)

The governance transition is independent of which approach is used. The specific transfer timeline and mechanism will be documented separately.

---

## Approach 1: Foundation + Cloud API

The foundation runs the scoring service. It collects data, calls a cloud LLM API, and publishes the results. Opacity Network proves every API call (both data collection and LLM scoring) genuinely happened.

**Trust model:** High trust in the foundation. Opacity proves the LLM was called with a specific prompt and returned a specific response — the foundation cannot fabricate scores. However, the foundation could call the LLM multiple times and publish the preferred result. The community can detect unusual scoring patterns over time but cannot prevent cherry-picking in real time.

**Complexity:** Lowest. Single scoring service, no distributed coordination, no commit-reveal, no bond mechanism, no quorum rules. Straightforward pipeline: collect data → call LLM → publish.

**Time to ship:** Fastest. The design is fully specified (see Design_FoundationCloudAPI).

**Centralization:** Most centralized. A single entity collects data, scores validators, and publishes the UNL. Every step is provable and auditable, but the foundation controls the process.

**Cost to foundation:** LLM API fees per round (2 calls × model pricing), Opacity Network fees for data collection and LLM proofs, IPFS pinning, infrastructure hosting.

**Cost to nodes:** None. There are no Oracle Nodes — only the foundation scores.

**Node rewards:** Not applicable. No external Oracle Nodes participate.

**Key risk:** Single point of trust and failure. If the foundation is compromised, coerced, or acts maliciously, scoring is compromised — even though the community can detect it after the fact via the published audit trail.

**Community impact:** Easiest to understand. Community members can verify every scoring round by checking Opacity proofs and reading the LLM reasoning on IPFS. However, participation is passive — community members cannot independently score, only audit. Trust depends on the foundation's reputation and the strength of the Opacity proofs.

**Institutional impact:** Simple and auditable, which institutions appreciate. However, institutions may hesitate to rely on a network where a single entity controls validator selection — even with transparency. Regulatory clarity is straightforward (one accountable entity) but the centralization may be seen as a risk.

**Existing design:** Design_FoundationCloudAPI.

---

## Approach 2: Distributed Nodes + Cloud API

Multiple independent Oracle Nodes each call a cloud LLM API with the same data and prompts. Each call is proven via TLSNotary (operated by the foundation). Oracle Nodes submit scores via commit-reveal on-chain. Final scores are computed by taking the median across all valid submissions.

**Trust model:** Distributed across Oracle Nodes. No single entity controls the scoring outcome. The foundation still collects data (Opacity-attested) and operates the TLSNotary notary server, but it cannot control individual Oracle Node scores. Median aggregation resists outlier manipulation. An Oracle Node and the foundation cannot collude to forge a TLSNotary proof — the MPC protocol prevents either party from seeing the full TLS session key.

**Complexity:** High. Requires Oracle Node registration with PFT bond, commit-reveal protocol, quorum rules, TLSNotary notary server operation, aggregation service, liveness tracking, and dispute handling. Significantly more infrastructure than Approach 1.

**Time to ship:** Medium. Builds on Approach 1 infrastructure but adds substantial distributed coordination.

**Centralization:** Moderately decentralized. Multiple independent scorers eliminate single-entity control over scores. However, all nodes depend on cloud LLM API providers (if the provider goes down, no one can score) and the foundation operates the TLSNotary notary (censorship vector — it could refuse to notarize a specific node, though this is publicly detectable).

**Cost to foundation:** TLSNotary notary server operation and maintenance, data collection with Opacity, aggregation service, IPFS pinning. No LLM API fees (Oracle Nodes pay their own).

**Cost to nodes:** LLM API key and per-round API fees, PFT bond (locked while active), basic VPS to run the Oracle Node client. Low hardware requirements — any server that can make HTTPS calls.

**Node rewards:** Oracle Node operators are compensated via the Task Node product (separate system, details outside the scope of this document).

**Key risk:** Requires enough Oracle Nodes to participate for quorum. The foundation's TLSNotary notary is a censorship vector (can refuse to notarize). All nodes depend on cloud API availability. If the LLM provider has an outage during a scoring round, the round fails.

**Community impact:** Stronger trust than Approach 1. Community members can verify all proofs, see all Oracle Node submissions, and independently recompute the median. Active participation is possible — anyone willing to bond PFT and run an Oracle Node can become a scorer. Community has visibility into disagreements between Oracle Nodes, which surfaces potential issues.

**Institutional impact:** Attractive trust model. Multiple independent scorers with economic bonds demonstrate seriousness. Institutions can participate as Oracle Node operators, giving them direct influence over scoring quality. The distributed model is more defensible from a regulatory perspective (no single entity makes the decision). Foundation's notary censorship risk is a concern but is publicly detectable.

**Existing design:** Design_DistributedCloudAPI.

---

## Approach 3: Foundation + Local Inference

The foundation runs an open-weight LLM on its own hardware (or rented GPU instances). Instead of proving an API call happened, the foundation proves the computation itself was correct using proof-of-logits: it publishes cryptographic commitments of the model's internal outputs (logits) at every token position. Anyone can spot-check a random position by re-running a single forward pass (~0.1% of the cost of full inference) and comparing logit hashes.

**Trust model:** The foundation runs scoring centrally, but the computation is independently verifiable. Unlike Approach 1, where the proof says "an API was called," the proof here says "this specific computation was performed." Cherry-picking is harder because re-running the model from scratch and committing different logits would require full re-computation, and any discrepancy in a spot-check reveals dishonesty. However, if nobody challenges, the foundation is trusted by default (optimistic model).

**Complexity:** Medium-high. Requires implementing logit commitment generation during inference, spot-check verification tooling, a challenge protocol, and managing GPU infrastructure. No distributed coordination (only the foundation scores), but proof-of-logits engineering is significant.

**Time to ship:** Slow. Depends on solving or working around the cross-hardware determinism problem. Current experiments show ~93% correlation; needs >99% for reliable spot-check verification. Possible workarounds: require the same GPU type for foundation and challengers, accept approximate matching with tolerance thresholds, or use optimistic verification with slashing.

**Centralization:** Centralized scoring with verifiable computation. The foundation still makes all scoring decisions, but unlike Approach 1, those decisions are mathematically auditable — not just provably made via an API call. A meaningful step toward trustlessness, but the foundation remains the single scorer.

**Cost to foundation:** GPU hardware or cloud GPU rental (e.g., RunPod, Lambda). No per-round API fees. For a 7B-32B parameter model: ~$0.50-2/hour on cloud GPUs. For 70B+: ~$5-15/hour. Hardware depreciates but amortizes over many rounds.

**Cost to nodes:** None for passive observation. To challenge a score (spot-check), a community member or institution needs access to the same model on compatible GPU hardware — a meaningful barrier for casual participants.

**Node rewards:** Not applicable. No external Oracle Nodes participate. Challengers are not formally rewarded (challenge is a community service, similar to how anyone can audit financial statements).

**Key risk:** Cross-hardware determinism is the blocking prerequisite. If two machines running the same model on different GPU types produce different logits, spot-checks produce false positives. This could be mitigated by standardizing on a specific GPU type, but that limits who can verify. The determinism problem is the subject of active research (see ResearchStatus.md).

**Community impact:** Strong auditability in theory — anyone with GPU access can mathematically verify scoring. In practice, the hardware barrier means most community members cannot challenge. Trust is higher than Approach 1 (computation is provable, not just API call) but lower than Approach 2 (still a single scorer). The optimistic verification model ("assume honest unless challenged") is well-understood in blockchain contexts.

**Institutional impact:** Appealing to technically sophisticated institutions that value mathematical proofs over economic/social trust. Institutions with GPU resources can independently audit. However, the single-scorer centralization remains a concern. The determinism problem adds uncertainty — institutions may prefer the proven guarantees of MPC-TLS proofs (Approach 1 or 2) over the unproven guarantees of proof-of-logits.

**Existing design:** No design document yet.

---

## Approach 4: Distributed Nodes + Local Inference

Multiple independent Oracle Nodes each run the same open-weight LLM on their own hardware. Each node publishes logit commitments proving their computation. Other nodes spot-check each other. Scores are aggregated via median. This combines the distributed trust of Approach 2 with the computational proofs of Approach 3.

**Trust model:** Strongest. No dependency on cloud APIs, no single scorer, and computation is mathematically verifiable. Each node runs the model independently, commits logit hashes, and is subject to random spot-checks by other nodes. Dishonest nodes risk detection and potential slashing. The combination of distributed scoring + proof-of-logits means no single entity can manipulate the outcome without being caught.

**Complexity:** Highest. Combines all the distributed coordination complexity of Approach 2 (registration, bonds, commit-reveal, quorum, aggregation) with all the proof-of-logits engineering of Approach 3 (logit commitments, spot-check verification, challenge protocol). Additionally requires cross-node verification scheduling and slashing for proven dishonesty.

**Time to ship:** Slowest. Requires everything from Approach 2 and 3, plus the determinism problem must be solved reliably across multiple independent operators with potentially different hardware.

**Centralization:** Most decentralized. Multiple independent scorers, no cloud API dependency, computation is verifiable. The foundation's role is reduced to data collection (Opacity-attested), aggregation (deterministic, anyone can recompute), and infrastructure operation. The closest to a fully trustless scoring system.

**Cost to foundation:** Data collection with Opacity, aggregation service, IPFS pinning. Minimal compared to other approaches — no API fees, no GPU infrastructure for scoring.

**Cost to nodes:** GPU hardware or cloud GPU rental, PFT bond, model storage and loading. Significantly higher hardware requirements than Approach 2, where nodes just need a VPS. For a 7B-32B model: consumer GPU or ~$0.50-2/hour cloud. For 70B+: mid-tier GPU or ~$5-15/hour cloud. The hardware barrier limits who can participate.

**Node rewards:** Scoring node operators are compensated via the Task Node product. The higher hardware costs make adequate compensation important for network participation.

**Key risk:** Cross-hardware determinism (same as Approach 3, but worse — multiple independent operators with different hardware make determinism harder to guarantee). High hardware barrier may limit the number of Oracle Nodes, threatening quorum. If too few nodes can afford to participate, the system degrades toward centralization in practice even if decentralized in design.

**Community impact:** Highest trust — scoring is distributed and mathematically provable. Community members with GPU access can participate as Oracle Nodes or independently verify any score. However, the hardware barrier creates a participation gap: well-resourced community members can fully engage, while others must trust the system passively. This is analogous to Bitcoin mining — trustless in design, but participation requires resources.

**Institutional impact:** Most attractive from a trust and compliance perspective. No single entity controls scoring, and every score is mathematically verifiable. Institutions with technical resources can run their own Oracle Nodes, giving them direct participation. The decentralized, provable model is the strongest argument in regulatory discussions. However, the hardware cost and operational complexity may deter institutions that prefer simpler setups. The unproven determinism guarantees add risk compared to the battle-tested MPC-TLS approach.

**Existing design:** No design document yet.

---

## Summary

| Dimension | Approach 1 | Approach 2 | Approach 3 | Approach 4 |
|-----------|-----------|-----------|-----------|-----------|
| **Who scores** | Foundation | Oracle Nodes | Foundation | Oracle Nodes |
| **Where LLM runs** | Cloud API | Cloud API | Local hardware | Local hardware |
| **How proven** | Opacity (MPC-TLS) | TLSNotary (MPC-TLS) | Proof-of-logits | Proof-of-logits |
| **Complexity** | Low | High | Medium-high | Highest |
| **Time to ship** | Fastest | Medium | Slow | Slowest |
| **Centralization** | Most centralized | Moderately decentralized | Centralized but verifiable | Most decentralized |
| **Cost to foundation** | API fees + Opacity | TLSNotary server + Opacity | GPU hardware | Data collection only |
| **Cost to nodes** | None | API fees + bond + VPS | None (challenge needs GPU) | GPU + bond |
| **Key risk** | Single point of trust | Quorum + API dependency | Determinism unsolved | Determinism + high barrier |
| **Community** | Audit only | Audit + participate | Audit with GPU | Audit + participate with GPU |
| **Institutions** | Simple but centralized | Distributed trust, can participate | Provable but centralized | Strongest trust, highest cost |
| **Existing design** | Design_FoundationCloudAPI | Design_DistributedCloudAPI | None | None |
