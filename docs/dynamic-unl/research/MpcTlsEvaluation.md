# MPC-TLS Technical Evaluation for Dynamic UNL Proof-of-Compute

**Date:** 2026-03-04
**Purpose:** Go/no-go evaluation of MPC-TLS as the verifiable LLM compute proof layer for the PostFiat Dynamic UNL scoring pipeline.

---

## 1. How the MPC-TLS 2PC Protocol Works

### 1.1 Overview

TLSNotary is a three-party protocol involving a **Prover** (the scoring service), a **Verifier** (or Notary — the MPC-TLS witness node), and a **Server** (the LLM API endpoint). The Prover and Verifier jointly participate in a TLS session such that neither party alone holds the complete session key. The Server sees a standard TLS connection and is entirely unaware that a Verifier is participating.

Developed by PSE (Privacy and Scaling Explorations), an Ethereum Foundation research lab. Implemented in Rust.

### 1.2 Step-by-Step 2PC Handshake (ECDH Key Exchange)

The handshake splits the TLS Pre-Master Secret (PMS) between Prover and Verifier using a three-party ECDH key exchange:

```
Step 1: Server sends its public key (Spk) to the Prover
Step 2: Prover forwards Spk to the Verifier
Step 3: Prover generates random key share Csk, computes Cpk = Csk * G
Step 4: Verifier generates random key share Nsk, computes Npk = Nsk * G
Step 5: Verifier sends Npk to the Prover
Step 6: Prover computes combined public key Ppk = Cpk + Npk, sends to Server
        (Server sees this as a normal client public key)
Step 7: Prover computes EC point C = (xC, yC) = Csk * Spk
Step 8: Verifier computes EC point N = (xN, yN) = Nsk * Spk
Step 9: Pre-Master Secret derived from R = C + N via secure computation
```

The PMS x-coordinate computation requires three MPC sub-steps:

1. **A2M (Additive-to-Multiplicative):** Converts additive shares to multiplicative shares for `(yN - yC)` and `(xN - xC)`.
2. **Intermediate computation:** Computes `CN * CC - xC - xN` where CN and CC are multiplicative shares.
3. **M2A (Multiplicative-to-Additive):** Converts back to additive shares to obtain the final PMS as `PMS = (DN - xN) - (DC - xC)`.

**Critical invariant:** Neither the Prover nor the Verifier ever learns the complete TLS session key. Each holds a secret share: `k = k_U XOR k_N`.

### 1.3 Data Encryption/Decryption (DEAP Protocol)

After the handshake, the protocol uses **DEAP (Dual Execution with Asymmetric Privacy)** for application data:

**Encryption:** Prover and Verifier jointly compute ciphertext using a block cipher in counter mode:
```
f(k_U, k_N, ctr, p) = Enc(k_U XOR k_N, ctr) XOR p = c
```
The Prover commits to garbled plaintext labels before the Verifier reveals their DEAP Delta value, ensuring commitment validity.

**Decryption:** The protocol masks the encrypted counter block with a random value `z` chosen by the Prover:
```
f(k_U, k_N, ctr, z) = Enc(k_U XOR k_N, ctr) XOR z = ectr_z
```
The Prover removes the mask to recover plaintext. The Verifier never sees the decrypted data.

**Validity proof:** The Prover proves that decrypted labels correspond to received ciphertext using a zero-knowledge protocol leveraging garbled circuit techniques.

### 1.4 Evolution: Garbled Circuits to VOLE-IZK (QuickSilver)

Starting with **v0.1.0-alpha.8** (March 2024), TLSNotary replaced the garbled-circuit proof backend with **QuickSilver**, a VOLE-based Interactive Zero-Knowledge protocol:

| Metric | Garbled Circuits | VOLE-IZK (QuickSilver) |
|--------|-----------------|----------------------|
| Communication cost | Baseline | **>100x reduction** |
| Online time | Baseline | **96% reduction** |
| Latency sensitivity | High | Significantly reduced |

The DEAP protocol still uses garbled circuits for the encryption/decryption 2PC itself, but the proof/verification backend is now VOLE-IZK.

### 1.5 Security Model

From the TLSNotary FAQ:

> "The protocol does not have trust assumptions. In particular, it does not rely on secure hardware or on the untamperability of the communication channel."

Key security guarantees:
- A malicious Prover cannot convince the Verifier of false data authenticity.
- A malicious Verifier cannot learn the Prover's private data.
- The Verifier's key share is ephemeral — only private during the TLS session.
- The Server cannot distinguish a TLSNotary session from a normal TLS session (though statistical timing analysis might reveal it).

**Critical trust caveat:** If the Verifier/Notary and the Prover collude, they can reconstruct the full session key and fabricate any transcript. This is the fundamental limitation that Opacity Network addresses (see Section 5).

---

## 2. Hard Limitations

### 2.1 Bandwidth Overhead

The MPC between Prover and Verifier requires bandwidth orders of magnitude larger than the actual data transferred.

| Component | Overhead |
|-----------|----------|
| Fixed cost per session | ~25 MB upload |
| Per 1 KB of outgoing data (request) | ~10 MB upload |
| Per 1 KB of incoming data (response) | ~40 KB upload |

**Worked example for a 1 KB request + 100 KB response:**
```
25 MB (fixed) + 10 MB (1 KB request) + 4 MB (100 KB response) = ~39 MB upload
```

**PostFiat scoring pipeline estimate:**
- Prompt with all validator profiles: ~50-100 KB request
- LLM response with scores + reasoning: ~10-50 KB response
- Estimated overhead per LLM call: **25 + 500-1000 + 0.4-2 = ~525-1027 MB upload**
- Two LLM calls (Step 1 + Step 2): **~1-2 GB total upload overhead**

Once bandwidth exceeds ~100 Mbps between Prover and Verifier, bandwidth ceases to be the limiting factor.

### 2.2 Latency Benchmarks

The protocol involves ~40 communication rounds between Prover and Verifier. Network latency has a direct linear impact on runtime.

**alpha.14 benchmarks (1 KB request + 2 KB response):**

| Network Type | Bandwidth | Latency | Native | Browser (WASM) |
|---|---|---|---|---|
| Cable | 20 Mbps | 20 ms | 15.0s | 16.8s |
| Fiber | 100 Mbps | 15 ms | 4.1s | 6.5s |
| Mobile 5G | 30 Mbps | 30 ms | 10.9s | 12.9s |

**For typical ~10 KB API responses (10 ms latency, 200 Mbps):**

| Build | Runtime |
|-------|---------|
| Native | ~5-6 seconds |
| Browser (WASM) | ~10-11 seconds |

**PostFiat scoring pipeline latency estimate:**
- Prompt size: 50-100 KB (all validator profiles)
- Response size: 10-50 KB
- With 100 Mbps, 15 ms latency: estimated **30-120 seconds per LLM call** (extrapolating from benchmarks, since overhead scales with request size at ~10 MB/KB)
- Two calls total: **1-4 minutes of MPC overhead** on top of LLM inference time

**Important:** These estimates are for the MPC overhead only. The actual LLM inference time (30-120 seconds for a large prompt with Claude or GPT-4) runs concurrently with the TLS session. The total wall-clock time is the LLM inference time plus MPC overhead, not their sum.

Benchmarks conducted on AWS c5.4xlarge instances (16 vCPU, 3.0 GHz, 32 GB RAM).

### 2.3 TLS Version Support

**Currently supported: TLS 1.2 only.**

TLS 1.3 is not supported, and the TLSNotary team states:

> "There are no immediate plans to support TLS 1.3. Once the web starts to transition away from TLS 1.2, we will consider adding support."

**Why TLS 1.3 is hard for MPC-TLS:**
- TLS 1.3's key schedule requires 5 HMAC-SHA256 operations for handshake keys and 6 more for application keys, making 2PC significantly more expensive.
- Most servers enforce 10-15 second TLS handshake timeouts, and completing the 2PC for TLS 1.3 within this window is challenging.
- TLS 1.3 encrypts more of the handshake itself, adding further MPC complexity.

**Practical impact for PostFiat:** Both Anthropic (`api.anthropic.com`) and OpenAI (`api.openai.com`) currently accept TLS 1.2 connections (verified 2026-03-04). Cloudflare AI Gateway also supports TLS 1.2. This is not a blocker today, but represents a long-term risk if LLM providers deprecate TLS 1.2.

### 2.4 Payload Size Constraints

The data size limits are configured on `MpcTlsConfigBuilder` (not `ProverConfigBuilder`). Both `max_sent_data` and `max_recv_data` are **required fields** with no defaults — they must be set explicitly. These values control how much data is preprocessed for the MPC protocol prior to the TLS connection.

**Configuration code for PostFiat's 50-100 KB prompts and 10-50 KB responses:**

```rust
use tlsn::config::tls_commit::mpc::{MpcTlsConfig, NetworkSetting};

// 128 KB for outgoing LLM prompt (validator profiles + system prompt)
const MAX_SENT_DATA: usize = 1 << 17;  // 131,072 bytes
// 64 KB for incoming LLM response (scores + reasoning)
const MAX_RECV_DATA: usize = 1 << 16;  // 65,536 bytes

let mpc_config = MpcTlsConfig::builder()
    .max_sent_data(MAX_SENT_DATA)
    .max_recv_data(MAX_RECV_DATA)
    // Optimize for bandwidth over latency on server infrastructure.
    .network(NetworkSetting::Bandwidth)
    .build()?;
```

The full prover setup in context:

```rust
use tlsn::{
    config::{
        prover::ProverConfig,
        tls_commit::{mpc::MpcTlsConfig, TlsCommitConfig},
    },
    Session,
};

let prover = handle
    .new_prover(ProverConfig::builder().build()?)?
    .commit(
        TlsCommitConfig::builder()
            .protocol(
                MpcTlsConfig::builder()
                    .max_sent_data(MAX_SENT_DATA)
                    .max_recv_data(MAX_RECV_DATA)
                    .network(NetworkSetting::Bandwidth)
                    .build()?,
            )
            .build()?,
    )
    .await?;
```

Additional `MpcTlsConfigBuilder` options relevant to PostFiat:

| Method | Type | Default | Purpose |
|---|---|---|---|
| `max_sent_data(n)` | `usize` | **required** | Max bytes the Prover can send to the server |
| `max_recv_data(n)` | `usize` | **required** | Max bytes the Prover can receive from the server |
| `max_recv_data_online(n)` | `usize` | 32 | Max bytes decrypted while MPC connection is active (rest deferred) |
| `defer_decryption_from_start(b)` | `bool` | `true` | Defer decryption to after the TLS session closes (reduces online time) |
| `network(setting)` | `NetworkSetting` | `Latency` | `Bandwidth` reduces round-trips at cost of more bandwidth; `Latency` reduces bandwidth at cost of more round-trips |
| `max_sent_records(n)` | `usize` | optional | Limit on outgoing TLS application data records |
| `max_recv_records_online(n)` | `usize` | optional | Limit on records decrypted online |

**Important:** The Verifier's `MpcTlsConfig` must use matching `max_sent_data` and `max_recv_data` values (swapped: the Verifier's `max_sent_data` = Prover's `max_recv_data` and vice versa). When using Opacity Network, this coordination is handled by the SDK.

**Sizing guidance:** Set limits generously (oversizing wastes preprocessing time but does not fail). Undersizing causes a runtime error if actual data exceeds the limit. For the scoring pipeline, 128 KB sent / 64 KB received provides comfortable headroom. If prompts grow beyond this (e.g., 100+ validators with verbose profiles), increase to `1 << 18` (256 KB).

- Megabyte-scale proving was added in alpha.13 (October 2024).
- If selective disclosure is not needed, large responses can be handled via ciphertext commitment (fast, size-independent).

### 2.5 Concurrency

- Multiple Prover/Verifier pairs over the same TLS connection are **not supported** (planned future feature).
- Each notarization session requires a separate connection.
- The protocol is inherently sequential and interactive during the TLS session.

### 2.6 Current Maturity

**Version:** v0.1.0-alpha.14 (released January 14, 2026).

The GitHub repository explicitly states the project **"should not be used in production"** with expectations of bugs and regular major breaking changes.

| Version | Date | Key Changes |
|---------|------|-------------|
| alpha.14 | Jan 2026 | 8-16% perf improvements, KECCAK256 support |
| alpha.13 | Oct 2024 | Unified API, megabyte-scale proving |
| alpha.12 | Jun 2024 | mTLS support, JWT auth, MPC optimizations |
| alpha.8 | Mar 2024 | QuickSilver VOLE-IZK (100x bandwidth reduction) |

---

## 3. Tested Servers and Known Incompatibilities

### 3.1 Confirmed Working

| Server/Service | Notes |
|---|---|
| Twitter/X (`api.x.com`) | Primary documented example; browser extension ships with Twitter plugin |
| Generic HTTPS servers | Protocol is transparent to servers; any TLS 1.2-capable server should work |
| Anthropic API (`api.anthropic.com`) | TLS 1.2 handshake succeeds (verified 2026-03-04, no TLSNotary-specific test) |
| OpenAI API (`api.openai.com`) | TLS 1.2 handshake succeeds (verified 2026-03-04, no TLSNotary-specific test) |

### 3.2 Known Incompatibilities

| Issue | Details |
|---|---|
| TLS 1.3-only servers | Any server that has disabled TLS 1.2 is incompatible |
| Server timeout sensitivity | The ~40 MPC rounds add seconds to the TLS session; servers with aggressive timeouts may drop the connection |
| Streaming responses (SSE) | LLM APIs commonly use Server-Sent Events for streaming; this creates many small TLS records over a long-lived connection, potentially increasing MPC overhead and complexity |
| Debug builds | Significantly slower; can trigger server-side timeouts |

### 3.3 CDN/Load Balancer Compatibility

No specific documentation exists for Cloudflare, AWS ALB, or other CDN/load balancer compatibility. Since the protocol is transparent to the server, CDNs should work as long as they support TLS 1.2. The additional latency from MPC rounds could trigger aggressive timeout policies on some load balancers.

### 3.4 PostFiat-Specific Compatibility Assessment

The scoring pipeline routes LLM calls through **Cloudflare AI Gateway** → **LLM Provider (Anthropic/OpenAI)**:

| Component | TLS 1.2 Support | Concern Level |
|---|---|---|
| Cloudflare AI Gateway | Yes | Low — Cloudflare has strong TLS 1.2 support |
| Anthropic Claude API | Yes (verified) | Low — TLS 1.2 accepted today |
| OpenAI API | Yes (verified) | Low — TLS 1.2 accepted today |

**Main risk:** LLM API calls with large prompts (50-100 KB of validator profiles) are atypical payloads for TLSNotary. No public evidence of TLSNotary being tested with LLM API payloads of this size exists.

---

## 4. Feasibility Verdict for PostFiat LLM Proof-of-Compute

### 4.1 What PostFiat Needs

The Dynamic UNL scoring pipeline makes two LLM API calls per scoring run:
1. **Step 1:** All validator profiles (~50-100 KB prompt) → individual scores + reasoning (~10-50 KB response)
2. **Step 2:** Step 1 results + diversity data (~20-50 KB prompt) → adjusted scores (~10-30 KB response)

The proof must demonstrate:
- A real HTTPS request was made to the specific LLM API endpoint
- The specific prompt was sent (verifiable)
- The specific response was received (not tampered with)

Scoring runs weekly (default), with potential emergency runs.

### 4.2 TLSNotary Assessment

| Requirement | Assessment | Status |
|---|---|---|
| Prove LLM API call happened | Yes — MPC-TLS proves the TLS session occurred with the specific server | PASS |
| Prove specific prompt was sent | Yes — Prover can selectively disclose the request content | PASS |
| Prove specific response was received | Yes — the response authenticity is cryptographically verified | PASS |
| TLS 1.2 compatibility with LLM APIs | Both Anthropic and OpenAI accept TLS 1.2 today | PASS (with long-term risk) |
| Handle 50-100 KB request payloads | Configurable beyond 4 KB default; megabyte-scale proving supported since alpha.13 | PASS (untested at this payload size with LLM APIs) |
| Handle 10-50 KB response payloads | Supported; ~5-6 seconds native for 10 KB | PASS |
| Performance acceptable for weekly runs | 1-4 minutes MPC overhead is acceptable for a weekly batch job | PASS |
| Bandwidth overhead acceptable | ~1-2 GB upload per scoring run is acceptable for server-to-server | PASS |
| Production readiness | Alpha software; "should not be used in production" | **FAIL** |
| Streaming response support | SSE/streaming adds complexity; non-streaming mode recommended | CONDITIONAL |

### 4.3 Opacity Network Assessment

Opacity Network builds on TLSNotary and addresses its main weakness (Verifier collusion) with:
- **Proof by committee:** 10 proofs from 10 randomly-selected notary nodes (not just 1)
- **Economic security via EigenLayer:** Slashing for misbehavior
- **Random node sampling:** Prover cannot choose a colluding notary
- **On-chain verification and audit trail**

| Aspect | TLSNotary Direct | Opacity Network |
|---|---|---|
| Trust model | Single Verifier must be honest | Committee of 10 + economic slashing |
| Anti-collusion | None built-in | Slashing, commit-reveal, whistleblowing |
| Maturity | Alpha (open source) | Funded startup ($12M seed, a16z CSX, Breyer) |
| Integration effort | Build notary infrastructure yourself | SDK integration, managed network |
| Cost per proof | Self-hosted (infrastructure cost only) | 10x the MPC overhead (10 parallel sessions) |

For PostFiat's use case, Opacity's committee model is more appropriate than raw TLSNotary because:
- The scoring service and Verifier would both be operated by the PostFiat Foundation if using TLSNotary directly — this is exactly the collusion scenario TLSNotary cannot prevent.
- Opacity's random multi-node attestation removes the single-point-of-trust problem.
- The Dynamic UNL plan already specifies Opacity as the target integration.

### 4.4 Risks and Mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| TLS 1.2 deprecation by LLM providers | Medium (years away) | TLSNotary team will add TLS 1.3 when needed; LLM providers unlikely to drop TLS 1.2 soon |
| Large prompt payloads untested | Medium | Test early with actual validator profile payloads through Opacity/TLSNotary; use non-streaming mode |
| Alpha software stability | High for TLSNotary; lower for Opacity (abstraction layer) | Opacity's SDK abstracts away TLSNotary internals; pin SDK versions; comprehensive error handling |
| Streaming responses | Low | Use non-streaming API mode for scoring calls (no user-facing latency requirement) |
| Server timeout from MPC overhead | Low | LLM APIs have generous timeouts (minutes, not seconds) for inference |
| Bandwidth for 10-node Opacity proofs | Low | ~10-20 GB total upload is acceptable for server infrastructure on a weekly cadence |

### 4.5 Verdict

**GO — with conditions.**

MPC-TLS is technically feasible as the proof layer for PostFiat's Dynamic UNL scoring pipeline. The core cryptographic protocol achieves exactly what PostFiat needs: unforgeable proof that a specific prompt was sent to a specific LLM API and a specific response was received.

**Conditions:**

1. **Use Opacity Network, not raw TLSNotary.** Direct TLSNotary use would require PostFiat to operate its own Notary, creating a single-Verifier trust problem. Opacity's committee model eliminates this.

2. **Use non-streaming API mode.** Send LLM requests with `stream: false` to avoid SSE complexity. The scoring pipeline is a batch job with no latency sensitivity.

3. **Validate with actual payloads early.** No public evidence exists of MPC-TLS being tested with 50-100 KB LLM API prompts. Run an integration test with real validator profile data through Opacity before committing to this architecture.

4. **Accept the alpha risk.** The underlying TLSNotary protocol is alpha software. Opacity abstracts this, but breaking changes in TLSNotary could propagate. Pin versions and budget for maintenance.

5. **Monitor TLS 1.2 deprecation timelines.** If Anthropic or OpenAI announce TLS 1.2 deprecation, this becomes a blocker until TLSNotary adds TLS 1.3 support.

---

## Appendix A: Summary of Critical Numbers

| Metric | Value |
|--------|-------|
| TLS version supported | 1.2 only |
| MPC communication rounds | ~40 |
| Fixed bandwidth overhead per session | ~25 MB upload |
| Bandwidth per 1 KB request data | ~10 MB upload |
| Bandwidth per 1 KB response data | ~40 KB upload |
| 1 KB req + 100 KB resp total overhead | ~39 MB upload |
| Runtime: fiber (100 Mbps, 15 ms), native | 4.1s (1 KB req + 2 KB resp) |
| Runtime: cable (20 Mbps, 20 ms), native | 15.0s (1 KB req + 2 KB resp) |
| Runtime: 10 KB response, native | ~5-6s |
| Runtime: 10 KB response, browser (WASM) | ~10-11s |
| Browser vs native performance gap | ~2-3x slower (no AES-NI in WASM) |
| Default max request size | 4 KB (configurable) |
| Concurrent sessions per connection | 1 |
| Current version | v0.1.0-alpha.14 (Jan 2026) |
| Production ready (TLSNotary) | No (alpha) |
| QuickSilver vs garbled circuits bandwidth | >100x reduction |
| QuickSilver vs garbled circuits online time | 96% reduction |
| Opacity proof committee size | 10 nodes |
| Opacity funding | $12M seed (a16z CSX, Breyer, Archetype) |

## Appendix B: Sources

| Source | URL |
|--------|-----|
| TLSNotary Introduction | https://tlsnotary.org/docs/intro/ |
| TLSNotary FAQ | https://tlsnotary.org/docs/faq/ |
| TLSNotary Performance Benchmarks (Aug 2025) | https://tlsnotary.org/blog/2025/08/31/benchmarks/ |
| TLSNotary alpha.14 Performance (Jan 2026) | https://tlsnotary.org/blog/2026/01/19/alpha14-performance/ |
| TLSNotary GitHub Releases | https://github.com/tlsnotary/tlsn/releases |
| Comprehensive Review of TLSNotary Protocol (arxiv) | https://arxiv.org/html/2409.17670v1 |
| QuickSilver Paper (ACM CCS 2021) | https://dl.acm.org/doi/10.1145/3460120.3484556 |
| Opacity Network Investment (Archetype) | https://www.archetype.fund/media/announcing-our-lead-investment-in-opacity |
| Opacity Network Seed Round (The Block) | https://www.theblock.co/post/321160/opacity-network-funding-zk-data-verification |
| FOSDEM 2026 TLSNotary Talk | https://fosdem.org/2026/schedule/event/QZ8NAZ-tlsnotary/ |
| TLSNotary Test Harness (Jan 2026) | https://tlsnotary.org/blog/2026/01/31/tlsnotary-test-harness/ |
