# TLSNotary MPC-TLS for Post Fiat Dynamic UNL: Technical Evaluation

## Context

Post Fiat's Dynamic UNL system replaces manual validator selection with an automated, LLM-driven scoring pipeline. The pipeline calls an LLM (via Cloudflare AI Gateway) to score validators, then publishes scores, reasoning, and the resulting UNL on-chain and to IPFS. Cryptographic proof that the LLM was genuinely called — not fabricated — is essential for trust.

The current design specifies Opacity Network for MPC-TLS proof generation. This document evaluates what can be built on top of TLSNotary's MPC-TLS proofs and whether TLSNotary-based patterns are viable alternatives or complements for the scoring pipeline.

---

## 1. Selective Disclosure: ZK Proofs Over Redacted TLS Data

### How TLSNotary Selective Disclosure Works

TLSNotary's protocol separates **attestation** from **presentation**. During the MPC-TLS session, the Prover and Notary collaboratively generate commitments to the TLS transcript. The Notary signs an attestation over these commitments without seeing the plaintext. Later, the Prover creates a presentation that selectively reveals chosen portions of the transcript to any Verifier, while keeping the rest redacted but cryptographically committed.

The attestation includes:
- Server domain name and ephemeral public key (verifiable via certificate chain)
- Commitment bindings to the TLS transcript data
- Notary's cryptographic signature (secp256k1) over the commitments
- Supported commitment hash algorithms: keccak256 and sha256

### ZK Proof System: QuickSilver (VOLE-based IZK)

TLSNotary replaced garbled circuits with the **QuickSilver** proving system (starting v0.1.0-alpha.8). QuickSilver uses VOLE (Vector Oblivious Linear Evaluation) as its underlying primitive, achieving communication complexity of 1 field element per non-linear gate. This significantly reduced bandwidth and latency sensitivity compared to the garbled circuit approach.

QuickSilver is an **interactive** ZK protocol, not a non-interactive zkSNARK. Proofs require the Verifier to have participated in the session. They are not independently verifiable by an arbitrary third party without the Notary's signed attestation as a trust anchor. This is the most important architectural constraint for any downstream application.

### Plugin-Level Selective Disclosure Handlers

TLSNotary's browser extension plugin system provides fine-grained selective disclosure control via handlers:

- **Actions**: `REVEAL` (disclose data in the clear) or `PEDERSEN` (cryptographic commitment, keeping data private but provably committed)
- **Targeting**: by transcript direction (`SENT`/`RECV`), by HTTP part (`STATUS_CODE`, `HEADERS`, `BODY`, `ALL`), with params for specific header keys, JSON paths, or regex patterns

**Example configuration for proving an LLM API call:**
```json
[
  { "type": "SENT", "part": "HEADERS", "action": "REVEAL", "params": { "key": "Host" } },
  { "type": "SENT", "part": "BODY", "action": "REVEAL" },
  { "type": "RECV", "part": "BODY", "action": "REVEAL" },
  { "type": "SENT", "part": "HEADERS", "action": "PEDERSEN", "params": { "key": "Authorization" } }
]
```

This would reveal the API endpoint, the full prompt, and the full response, while keeping the API key committed but hidden. Anyone verifying the proof can confirm the API key was present (via the Pedersen commitment) without learning its value.

### Applicability to Post Fiat

Selective disclosure is well-suited for the scoring pipeline:

| Data | Action | Rationale |
|------|--------|-----------|
| API endpoint URL | REVEAL | Proves which LLM provider was called |
| Prompt (validator profiles + system prompt) | REVEAL | Full transparency of scoring inputs |
| Response (scores + reasoning) | REVEAL | Full transparency of scoring outputs |
| API key (Authorization header) | PEDERSEN | Proves authentication was present without exposing credentials |
| Cookies / session tokens | PEDERSEN | Committed but private |

The handler system maps directly to Post Fiat's transparency requirements. The full prompt and response are published anyway (to IPFS), so selective disclosure adds the cryptographic binding between "this prompt produced this response from this API endpoint" — which is exactly what the scoring pipeline needs.

---

## 2. On-Chain Verification of TLS Attestations

### Current State: Not Production-Ready

TLSNotary's FAQ states explicitly: "the most practical way to verify data on-chain is to prove the data directly to an off-chain application-specific verifier." Direct on-chain verification is listed as a planned upgrade with no timeline.

The fundamental challenge is that QuickSilver is interactive. The resulting proofs are not independently verifiable by a smart contract without the Notary's signed attestation as a trust bridge. Converting interactive proofs to non-interactive, succinct proofs (e.g., wrapping in a SNARK) is computationally expensive and not yet implemented in TLSNotary.

### vlayer: The Closest Bridge to On-Chain

vlayer (mainnet since 2025, 480K+ ZK proofs generated) is the most mature project bringing TLSNotary proofs on-chain for EVM chains. It provides Solidity interfaces (`WebProofProver`, `WebProofVerifier`) that verify HTTPS transcripts, confirm Notary signatures, check Notary identity against a trusted list, and validate SSL certificates.

However, vlayer operates its own Notary server, making it a trusted party. Proof size and gas cost figures are not publicly documented. And critically, Post Fiat runs on PFT Ledger (an XRPL fork), not an EVM chain — vlayer's Solidity contracts are not directly usable.

### VOLEitH (VOLE-in-the-Head)

An emerging technique called VOLE-in-the-Head could transform VOLE-ZK proofs into non-interactive format, potentially enabling on-chain verification. TLSNotary has not adopted this yet, and no implementation exists for XRPL-family chains.

### Applicability to Post Fiat

On-chain verification of TLS proofs is **not viable** for Post Fiat in the near term:

1. **PFT Ledger has no smart contract VM.** XRPL (and its forks) do not support arbitrary smart contract execution. There is no EVM, no WASM runtime, no way to run a proof verifier on-chain. Even if TLSNotary proofs were succinct and non-interactive, there is no execution environment on PFT Ledger to verify them.

2. **The existing design is correct.** Post Fiat's architecture publishes only a hash + IPFS CID on-chain, with the full proof artifacts on IPFS. This is the right pattern for a non-EVM chain. Nodes verify the UNL hash against the on-chain hash; community members verify proofs off-chain by fetching from IPFS.

3. **If on-chain verification were ever desired**, it would require either: (a) an amendment adding a native proof verification opcode to PFT Ledger (significant protocol work), or (b) a sidechain/bridge to an EVM chain where vlayer-style verification could run. Neither is justified given that IPFS publication with on-chain hash anchoring achieves the same auditability goal.

**Recommendation:** Do not pursue on-chain verification of TLS proofs. The hash-on-chain + proof-on-IPFS pattern is architecturally sound and does not require changes to PFT Ledger's consensus or transaction processing.

---

## 3. Browser Extension and Plugin Development

### Extension Architecture

The TLSNotary browser extension (`tlsn-extension`) uses a three-layer isolation architecture:

1. **Background Service Worker**: coordinates execution and message routing
2. **Offscreen Document**: contains `SessionManager` and `ProveManager` managing the MPC-TLS session
3. **QuickJS WebAssembly Sandbox**: executes untrusted plugin code in isolation with capability-based security

Plugins are JavaScript files running in an isolated QuickJS WASM sandbox. They cannot access network functions (`fetch`, `XMLHttpRequest`), file system APIs, browser APIs (except registered capabilities), or `eval()`.

### Key Plugin APIs

| API | Purpose |
|-----|---------|
| `openWindow(url, options?)` | Open managed browser windows (max 10 concurrent) |
| `useState(key, default)` / `setState(key, value)` | Persistent state across re-renders |
| `useRequests(filterFn)` | Access intercepted HTTP requests (URL, method, body, timestamp) |
| `useHeaders(filterFn)` | Access intercepted request headers |
| `prove(requestOptions, proverOptions)` | Generate MPC-TLS proof with selective disclosure handlers |

The `prove()` function is the core capability: it establishes a prover connection, sends an HTTP request through a WebSocket proxy, captures the TLS transcript at byte level, applies selective reveal handlers, and returns a structured proof.

### Resource Constraints

- Max 10 concurrent windows per plugin
- 1000 request/header limit per window (FIFO eviction)
- Default `maxSentData`: 4096 bytes
- Default `maxRecvData`: 16384 bytes (configurable)
- Supported browsers: Chrome, Edge, Brave (Chromium-based only)

### Applicability to Post Fiat

The browser extension plugin system is **not the right integration point** for Post Fiat's scoring pipeline:

1. **The scoring service is server-side.** Post Fiat's `dynamic-unl-scoring` service is an automated pipeline (data collection → LLM calls → publication) running on a server via cron or manual trigger. It is not a browser-based workflow.

2. **Server-side integration uses the Rust crate or JS module.** TLSNotary provides `tlsn` (Rust crate) and `tlsn-js` (NPM module) for programmatic, non-browser MPC-TLS sessions. The scoring service would use these directly — not the browser extension.

3. **The browser extension is designed for user-facing attestations** (e.g., proving a bank balance, proving social media ownership). Post Fiat's use case is machine-to-machine: a scoring service calling an LLM API and proving the call was genuine.

4. **Plugin resource constraints are too restrictive.** The default `maxSentData` of 4096 bytes and `maxRecvData` of 16384 bytes are far too small for LLM scoring prompts (which include all validator profiles, potentially 50+ KB) and responses (scores + reasoning for up to 35 validators).

**Recommendation:** If Post Fiat were to use TLSNotary directly (instead of Opacity), integrate the `tlsn` Rust crate into the scoring service. The browser extension and its plugin system are irrelevant to this use case.

---

## 4. TLSNotary vs Opacity Network

### Relationship

Opacity Network is **built on top of TLSNotary**. It is not an alternative protocol — it is an infrastructure layer that wraps TLSNotary's MPC-TLS with additional trust mechanisms. The Stanford Blockchain Review describes Opacity as "one of the most robust and comprehensive deployments of a zkTLS architecture, building upon the TLSNotary framework."

### Comparison

| Aspect | TLSNotary (Raw Protocol) | Opacity Network |
|--------|-------------------------|----------------|
| Trust model | Single Notary must be trusted | Multiple notaries + TEEs + EigenLayer staking |
| Collusion mitigation | None at protocol level | Commit-and-reveal assignment, proof by committee, on-chain logging |
| TEE usage | None (pure cryptographic) | Intel SGX enclaves alongside MPC |
| Economic security | None | EigenLayer AVS with staking and slashing |
| On-chain component | None native | Smart contract verification, attestation logging |
| SDK availability | Rust crate, JS module, browser extension | TypeScript SDK, mobile SDKs |
| Maturity | Pre-production ("should not be used in production") | Production (raised $12M seed, active development) |
| Verifiable inference | Not built-in (must be implemented) | Pre-built pattern via Cloudflare AI Gateway |

### Opacity's Verifiable Inference Flow

Opacity already implements the exact pattern Post Fiat needs:

1. Application sends prompt to Cloudflare AI Gateway
2. Cloudflare routes to LLM provider and logs the response
3. Application requests proof from Opacity prover service
4. Opacity fetches the logged response via MPC-TLS
5. Opacity returns cryptographic proof that the response is genuine

Configuration requires: `OPACITY_TEAM_ID`, `OPACITY_CLOUDFLARE_NAME`, `OPACITY_PROVER_URL`.

### Concerns with Opacity

- The ElizaOS `plugin-opacity` repository was **archived on February 7, 2025** and marked unmaintained. The reference implementation for the verifiable inference pattern is no longer maintained.
- Opacity relies on Intel SGX TEEs alongside MPC. SGX has known side-channel vulnerabilities (Foreshadow, Plundervolt, SGAxe). The economic security layer (EigenLayer slashing) mitigates this but does not eliminate it.
- Dependency on Opacity Network availability. If Opacity's notary network goes down, proof generation fails.

### Concerns with TLSNotary (Direct Integration)

- The GitHub README states: "This project is currently under active development and should not be used in production. Expect bugs and regular major breaking changes."
- Must run your own Notary server, which becomes a single point of trust. Prover + Notary collusion can produce fabricated proofs.
- No built-in economic security or slashing mechanism.
- The bandwidth overhead is substantial (detailed below).

---

## 5. Critical Technical Constraints

### TLS Version Support

TLSNotary supports **TLS 1.2 only**. The FAQ states they will consider TLS 1.3 "once the web starts to transition away from TLS 1.2." As of 2026, major LLM API providers (Anthropic, OpenAI) still support TLS 1.2, but the industry trend is toward TLS 1.3. This is a medium-term risk for any system built on TLSNotary.

### Bandwidth Overhead

Raw TLSNotary MPC-TLS has significant bandwidth costs:

- **Fixed cost**: ~25 MB per MPC-TLS session
- **Variable cost (outgoing data)**: ~10 MB per 1 KB of data sent
- **Variable cost (incoming data)**: ~40 KB per 1 KB of data received

#### Raw TLSNotary: Direct LLM API Notarization (Hypothetical)

If a scoring service notarized the LLM API call directly via raw TLSNotary, the full prompt (~50 KB of validator profiles) would be the outgoing payload in the MPC-TLS session:

| Component | Size | MPC Overhead |
|-----------|------|-------------|
| Prompt (all validator profiles) | ~50 KB | ~500 MB upload |
| Response (scores + reasoning) | ~20 KB | ~0.8 MB |
| Fixed session cost | — | ~25 MB |
| **Total per LLM call** | **~70 KB** | **~526 MB** |

With 2 LLM calls per scoring cycle, that would be ~1 GB of MPC data transfer per cycle.

#### Opacity Network: Cloudflare Log-Fetch Architecture (Actual)

**Opacity does not incur this same penalty.** Its architecture decouples the LLM call from the MPC-TLS session through an indirection layer:

1. The scoring service sends the prompt to the LLM **through Cloudflare AI Gateway** (a proxy). The LLM call happens normally — no MPC overhead on this path.
2. Cloudflare logs the complete request/response pair.
3. Opacity's prover then fetches the **logged response** from Cloudflare's log API endpoint via MPC-TLS.

The MPC-TLS session is between Opacity's prover and Cloudflare's REST log API (`GET /accounts/{id}/ai-gateway/gateways/{id}/logs/{id}/request`), not between the scoring service and the LLM. The outgoing payload in the MPC session is a small GET request (~1-2 KB: log ID + auth headers), not the full 50 KB prompt:

| Component | Size | MPC Overhead |
|-----------|------|-------------|
| Log fetch request (GET + auth) | ~2 KB | ~20 MB upload |
| Log response (LLM output from Cloudflare) | ~20 KB | ~0.8 MB |
| Fixed session cost | — | ~25 MB |
| **Total per proof** | **~22 KB** | **~46 MB** |

This is roughly **10x less overhead** than notarizing the LLM call directly. For 2 proofs per scoring cycle: ~92 MB total MPC data, not ~1 GB.

**Why this works:** The Cloudflare log contains the same LLM response, but the MPC-TLS session only needs to authenticate the log fetch — a small, simple REST call. The expensive outgoing payload (the full prompt) is never part of the MPC session. The proof still establishes that the specific LLM response was genuinely produced, because Cloudflare's log is the authoritative record of what the LLM API returned.

**Trade-off:** This architecture introduces Cloudflare as an additional trust dependency. The proof chain becomes: "Cloudflare logged this response from the LLM, and MPC-TLS proves the log entry is authentic." If Cloudflare's logging were compromised, the proof would not catch it. In practice, Cloudflare is a major CDN provider with strong security posture, and this trade-off is widely accepted in the zkTLS ecosystem.

**Note on TEEs:** Opacity's Intel SGX enclaves do not reduce MPC bandwidth. They serve as an anti-collusion mechanism — ensuring the notary executes the correct MPC protocol honestly — rather than a performance optimization.

### Performance (alpha.14 Benchmarks, January 2026)

For a 1 KB request / 2 KB response on fiber (100 Mbps, 15ms latency):
- Native build: 4.1 seconds
- Browser build: 6.5 seconds

For ~10 KB responses: native ~5-6 seconds, browser ~10-11 seconds.

LLM API calls with 50 KB prompts and 20 KB responses would take longer. No published benchmarks exist for payloads this large. Empirical measurement would be required.

### Proof Sizes

Specific proof sizes in bytes are **not documented** in any publicly available source. The documentation focuses on MPC session bandwidth, not the size of the resulting attestation artifacts. This matters for IPFS storage costs and download times during audit verification. Experimental measurement is needed.

### Software Maturity

TLSNotary's README carries an explicit warning: "This project is currently under active development and should not be used in production." Breaking API changes between alpha releases are common. The codebase is 98.3% Rust and requires `--release` builds (debug builds timeout). WASM compilation requires clang 16.0.0+.

### Not an Oracle

MPC-TLS proves that data was received from a specific server over TLS. It does not prove the server's response was correct. For Post Fiat: MPC-TLS proves the LLM was genuinely called and the response is authentic, but it cannot prove the LLM's scoring decisions are correct or fair. This limitation applies equally to TLSNotary and Opacity.

---

## 6. Prioritized Recommendations for Post Fiat Dynamic UNL

### Recommendation 1: Stay with Opacity Network (High Priority — Do This)

**Rationale:** Opacity already implements the exact verifiable inference pattern Post Fiat needs. It wraps TLSNotary's MPC-TLS with multi-notary trust, economic security, and a pre-built Cloudflare AI Gateway integration. The `DYNAMIC_UNL_PLAN.md` design is architecturally sound as written.

**Action items:**
- Verify Opacity SDK stability independent of the archived ElizaOS plugin (contact Opacity team directly if needed)
- Prototype the integration: Cloudflare AI Gateway → Anthropic API → Opacity proof generation
- Measure actual proof sizes for LLM scoring payloads to validate IPFS storage estimates
- Establish a fallback plan (see Recommendation 3)

### Recommendation 2: Do Not Build On-Chain TLS Proof Verification (High Priority — Do Not Do This)

**Rationale:** PFT Ledger has no smart contract execution environment. On-chain verification would require a protocol amendment adding a native proof verification opcode — unjustified effort given that the hash-on-chain + proof-on-IPFS pattern achieves the same auditability. vlayer's Solidity verifiers are irrelevant to a non-EVM chain. The current design is correct.

### Recommendation 3: Maintain TLSNotary Direct Integration as Fallback (Medium Priority — Prepare but Don't Build Yet)

**Rationale:** If Opacity Network becomes unavailable, unreliable, or introduces unacceptable terms, the scoring service could integrate TLSNotary's Rust crate (`tlsn`) directly. This would require running a Notary server (initially foundation-operated, with the Notary's public key published for transparency).

**What to prepare:**
- Document the `tlsn` Rust crate API surface relevant to the scoring service
- Identify who would operate the Notary server and how its identity would be published
- Understand that this downgrades from Opacity's multi-notary trust to single-notary trust (acceptable for a foundation-operated system in early phases)

**What not to build:** Do not build this integration unless Opacity proves unusable. The bandwidth overhead (~500 MB per LLM call) and pre-production status of TLSNotary make it a fallback, not a primary choice.

### Recommendation 4: Do Not Build Browser Extension Plugins (Low Priority — Irrelevant)

**Rationale:** The scoring pipeline is a server-side automated process. The TLSNotary browser extension is designed for user-facing attestation workflows. Its resource constraints (4 KB send / 16 KB receive defaults) are incompatible with LLM scoring payloads. The plugin development surface is irrelevant to this use case.

**Exception:** If Post Fiat later builds a community verification tool (a web app where anyone can independently re-verify scoring proofs), the browser extension's plugin system could be used to let community members independently attest API calls. This is a future consideration, not a near-term priority.

### Recommendation 5: Monitor TLS 1.3 Support (Low Priority — Watch)

**Rationale:** TLSNotary (and by extension Opacity) only supports TLS 1.2. This is not a problem today but becomes a risk if LLM API providers deprecate TLS 1.2. Monitor Anthropic's and OpenAI's TLS version policies. If they drop 1.2 before TLSNotary/Opacity supports 1.3, the proof-of-computation mechanism would break.

### Recommendation 6: Benchmark Real Payload Sizes Before Committing (Medium Priority — Do This Early)

**Rationale:** The bandwidth overhead estimates above (~500 MB per LLM call) are based on published TLSNotary benchmarks scaled to expected prompt sizes. These have not been validated empirically with actual Post Fiat validator profiles. Before finalizing the scoring service architecture:

- Build a representative set of 35 validator profiles in the JSON schema defined in the plan
- Measure actual prompt size and response size for both Step 1 and Step 2 LLM calls
- If using Opacity: measure actual proof generation time and proof artifact size
- If prompt sizes can be reduced (e.g., more compact JSON representation), the bandwidth overhead drops proportionally

---

## Summary Table

| Application Pattern | Viability for Post Fiat | Recommendation |
|---------------------|------------------------|----------------|
| Selective disclosure (reveal prompt/response, hide API key) | High — directly applicable | Use via Opacity; handlers map cleanly to scoring transparency requirements |
| On-chain TLS proof verification | Not viable — PFT Ledger has no smart contract VM | Do not pursue; hash-on-chain + proof-on-IPFS is the correct pattern |
| Browser extension plugin development | Not applicable — scoring is server-side | Do not pursue for scoring pipeline; potential future use for community verification tools |
| Opacity Network (current plan) | High — pre-built verifiable inference pattern, ~10x less MPC bandwidth than raw TLSNotary via Cloudflare log-fetch indirection | Proceed as planned; verify SDK stability |
| TLSNotary direct integration (Rust crate) | Medium — viable fallback but ~500 MB MPC overhead per LLM call without Cloudflare indirection | Prepare documentation; do not build unless Opacity proves unusable |
| TLS 1.3 support | Not available — TLS 1.2 only | Monitor LLM provider TLS policies |
