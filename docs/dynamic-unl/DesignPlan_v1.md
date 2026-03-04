# Dynamic UNL: Complete Design Plan

## 1. What is Dynamic UNL?

The Unique Node List (UNL) determines which validators participate in consensus. On XRPL, this list is effectively hardcoded — controlled by a single entity (Ripple) with no on-chain accountability or transparency into how validators are selected.

Dynamic UNL replaces this with an automated, transparent, LLM-driven system where validators are scored based on objective performance metrics and subjective reputation assessments, with cryptographic proof that scores were genuinely produced by the declared AI model and not fabricated.

### Core Differentiators from XRPL

| Aspect | XRPL | PostFiat Dynamic UNL |
|--------|------|---------------------|
| UNL selection | Manual, opaque | Automated, LLM-scored |
| Accountability | None on-chain | Full audit trail on-chain + IPFS |
| Validator criteria | Undisclosed | Published scoring criteria and reasoning |
| Update mechanism | Publisher signs list | On-chain hash + score-based selection |
| Proof of computation | None | MPC-TLS cryptographic proof of LLM computation |
| Identity | Optional domain | Mandatory KYC/KYB + institutional domain verification |
| Validator incentive | No fees | Transaction fees paid to validators |

---

## 2. How Nodes Handle Dynamic UNL

Dynamic UNL is implemented as a protocol-level feature inside postfiatd, gated behind the `featureDynamicUNL` amendment. Nodes must vote to enable it.

### Two New Components in postfiatd

**UNLHashWatcher** monitors validated transactions for UNL hash publications from the trusted publisher (the PostFiat Foundation's master account). When it sees a payment from `masterAccount` to `memoAccount` with a JSON memo containing the UNL hash, it stores the update as "pending" until the designated flag ledger.

**DynamicUNLManager** parses the score-based UNL JSON fetched from the HTTPS endpoint, verifies its hash against the on-chain hash (via UNLHashWatcher), and selects the top N validators by score.

### On-Chain Hash Publication

The foundation publishes a transaction with a memo containing:

```json
{
  "hash": "<sha512Half of UNL JSON>",
  "ipfs_cid": "<IPFS content identifier for full audit trail>",
  "effectiveLedger": 12800,
  "sequence": 42,
  "scoring_config_version": "v1.2.3"
}
```

- `hash`: sha512Half of the UNL JSON, used by nodes to verify the fetched list
- `ipfs_cid`: points to the complete scoring audit trail on IPFS
- `effectiveLedger`: the flag ledger when the update takes effect
- `sequence`: monotonically increasing counter preventing replay attacks
- `scoring_config_version`: which scoring configuration produced this UNL

### Node-Side Update Flow

```
1. Foundation sends on-chain tx with UNL hash
        ↓
2. BuildLedger processes the tx during ledger building
   → UNLHashWatcher.processTransaction() stores pending update
   → Sequence must be > highest seen (replay protection)
        ↓
3. At flag ledger (every 256 ledgers, ~15 min):
   → UNLHashWatcher.applyPendingUpdate() → pending becomes current
        ↓
4. ValidatorSite fetch cycle (every 5 minutes):
   → Fetches UNL JSON from HTTPS endpoint
   → Computes sha512Half of fetched JSON
   → DynamicUNLManager verifies hash matches on-chain hash
   → If match: parses validators, selects top N by score
   → Feeds into ValidatorList.applyListsAndBroadcast()
        ↓
5. Next consensus round:
   → updateTrusted() picks up new validator set
   → Quorum recalculated (80% supermajority)
   → Consensus uses new trusted keys
```

### Why This Works Without Node Restarts

The existing XRPL codebase already supports runtime UNL changes. `ValidatorSite` fetches lists via HTTP every 5 minutes. `ValidatorList` uses a `shared_mutex` for thread-safe updates — writes take an exclusive lock, reads during consensus use shared locks. `updateTrusted()` is called at each consensus round start to pick up changes. Dynamic UNL hooks into this existing infrastructure rather than replacing it.

### UNL Transition Safety

All nodes apply the UNL change at the same flag ledger simultaneously. The switch is deterministic — every node sees the same on-chain hash, fetches the same JSON, switches at the same ledger. There is no risk of consensus fork from a UNL transition.

The only theoretical stall risk is if new validators in the UNL are offline. But Dynamic UNL promotes validators based on their scores, which reflect uptime and reliability. Validators being promoted are already running, synced, and producing validations. The score-based system is self-protecting. Even a full UNL replacement is safe as long as the new validators have high uptime scores.

### UNL Size

The maximum UNL size is set to **35 validators** (`MAX_UNL_VALIDATORS = 35`), matching XRPL's proven production configuration. With 35 validators and 80% quorum, up to 7 can be offline or misbehaving before the network stalls.

Formal research on optimal UNL size for PostFiat's specific requirements (security, decentralization, consensus performance) should be conducted before mainnet launch. The result may differ from 35.

### Bootstrap and Restart

Nodes start with a hardcoded `NetworkValidators` list for their network (devnet/testnet/mainnet). Once Dynamic UNL is active and a hash has been published on-chain, nodes pick up the dynamic list on the next fetch cycle. On restart, no special persistence is needed — the node syncs ledger history (which contains the hash transaction) and fetches the current UNL from the HTTPS endpoint.

### Failure Modes

| Failure | Node Behavior |
|---------|--------------|
| HTTPS endpoint unreachable | Keeps current UNL, retries every 30 seconds, falls back to local cache |
| Hash published but JSON not yet available | Keeps current UNL until fetch succeeds and hash verifies |
| Hash mismatch (fetched JSON ≠ on-chain hash) | Rejects fetched list, keeps current UNL, logs warning |

### Node Configuration

```ini
[validator_list_sites]
https://postfiat.org

[validator_list_keys]
ED2677ABFFD1B33AC6FBC3062B71F1E8397C1505E1C42C64D11AD1B28FF73F4734

[unl_hash_publisher]
memo_account=rPT1Sjq2YGrBMTttX4GZHjKu9dyfzbpAYe
```

---

## 3. Validator Identity and Onboarding

Before a validator can be scored and included in the UNL, it must pass identity verification. This is a mandatory prerequisite, not an optional enhancement.

### Why Identity is Required

- Prevents Sybil attacks (can't create fake validators to game scores)
- Makes reputation and sanctions assessment meaningful
- Required for regulatory clarity
- Enables entity concentration limits (can't have one entity run too many validators under different names)

### Identity Verification Types

#### KYC/KYB Verification (Mandatory)

KYC (Know Your Customer) / KYB (Know Your Business) verification is handled by **SumSub**, a third-party identity verification provider.

**Flow:**
1. Validator candidate initiates KYC via the onboarding portal (embedded SumSub WebSDK)
2. Candidate uploads government-issued ID and completes liveness verification
3. SumSub automatically reviews documents and sends webhook with result
4. On approval, the result is published on-chain by the foundation's master account as a memo transaction:

```json
{
  "type": "pf_identity_v1",
  "validator_address": "rXXX...",
  "identity_type": "kyc",
  "proof_hash": "<sumsub_applicant_id>",
  "proof_type": "sumsub_kyc",
  "decision": "approved",
  "verified_at": "2025-06-15T12:00:00Z"
}
```

KYC status is binary: approved or not. Without approval, a validator cannot be scored.

#### Institutional Domain Verification (Recommended)

This is a custom PostFiat verification mechanism where an institution proves ownership of a domain by adding a DNS TXT record. This is **distinct from the standard validator domain configuration** where a validator sets a domain in their config and publishes a manifest.

**Naming distinction:**
- **Validator domain** (standard): A validator sets `[domain]` in their postfiatd config, creates a `pft-ledger.toml` at `/.well-known/`, and publishes a manifest. This is a node-level configuration.
- **Institutional domain verification** (PostFiat-specific): An institution proves it controls a domain by adding a `_postfiat.<domain>` TXT record signed with their validator key. This is an identity-level verification that ties the validator to a real-world institution.

**Flow:**
1. Validator candidate provides their domain and entity type (university/corporation)
2. System generates a verification token and instructs them to add a DNS TXT record:
   ```
   _postfiat.stanford.edu TXT "pf-verify=<token> sig=<signature_hex>"
   ```
3. The signature is over the message `pf-verify:<token>:<domain>` using the validator's Ed25519 or secp256k1 key
4. System verifies the DNS record exists, extracts the signature, verifies it against the validator's public key
5. On success, published on-chain:

```json
{
  "type": "pf_identity_v1",
  "validator_address": "rXXX...",
  "identity_type": "university",
  "identifier": "stanford.edu",
  "proof_hash": "<sha256_of_verification_data>",
  "proof_type": "dns_txt",
  "verified_at": "2025-06-15T12:00:00Z"
}
```

Institutional domain verification is not mandatory for UNL inclusion but positively influences the validator's score — it demonstrates the validator is operated by a legitimate, identifiable institution.

#### On-Chain Publication of Identity

All identity verification results (KYC approvals, institutional domain verifications) are published on-chain by the foundation's master account. This creates an immutable, publicly queryable record of which validators have verified identities. The scoring data pipeline reads these on-chain records when building validator profiles.

### Existing Infrastructure

The onboarding system already exists in the `scoring-onboarding` repository:
- SumSub KYC integration (WebSDK, webhook processing, on-chain publishing)
- DNS-based institutional domain verification
- On-chain identity publishing via memo transactions
- Automatic retry for failed on-chain publishes
- Wallet authorization for portal access

---

## 4. Validator Scoring

The scoring system is the core differentiator of PostFiat's Dynamic UNL. The LLM is the brain — it receives precomputed objective metrics and makes the final scoring decisions with contextual interpretation that a formula cannot provide.

### Architecture

```
KYC/KYB gate (binary: eligible or not)
        │
        ↓ only verified entities proceed
        │
Data pipeline (deterministic, automated)
        │   Computes objective metrics from on-chain data and monitoring services
        │   Outputs structured validator profile (JSON) for each eligible validator
        │
        ↓
        │
LLM scoring — Step 1: Individual scores
        │   Receives: ALL validator profiles in one prompt (comparative context)
        │   Produces: score (0-100) + reasoning for each validator
        │   Witnessed by Opacity MPC-TLS for proof of computation
        │
        ↓
        │
LLM scoring — Step 2: Network diversity adjustment
        │   Receives: all Step 1 results + geographic/entity/ISP distribution
        │   Produces: adjusted scores + adjustment reasoning
        │   Witnessed by Opacity MPC-TLS for proof of computation
        │
        ↓
        │
Top 35 validators by adjusted score → published as UNL
```

### Why Two Steps

Step 1 answers: "How good is this validator on its own merits?"
Step 2 answers: "Given all the candidates, what combination produces the most resilient network?"

Geographic diversity and entity concentration are relative to the current set, not properties of an individual validator. A validator in Tokyo scores differently depending on whether there are 0 or 12 other validators in Asia.

### Why All Validators in One Prompt

The LLM sees all candidates together in a single call per step:
- Comparative context matters: a 97% agreement score means different things if the average is 99% vs 95%
- The LLM can spot anomalies better when it sees the full distribution
- One call is cheaper and faster than N separate calls
- Even 100 validator profiles (~1-2KB JSON each) fit within modern context windows

### Data Pipeline: What Feeds the LLM

The data pipeline queries existing PostFiat infrastructure and builds a structured JSON profile for each validator. The LLM does not compute raw metrics — it receives them precomputed.

**Data sources (all already collected):**

| Source | Data |
|--------|------|
| VHS PostgreSQL | Agreement scores (1h/24h/30d), uptime, latency, peer connections, server version, manifests, amendment voting, fee votes, geographic location (MaxMind), domain verification, WebSocket health |
| Network Monitoring PostgreSQL | Alert history (agreement drops, offline events, version mismatches), severity, resolution times, consecutive missed checks |
| On-chain identity records | KYC/KYB status, institutional domain verification status |

**MaxMind upgrade required:** The VHS currently uses GeoLite2 (free tier) for geographic data. This must be upgraded to **MaxMind GeoIP2** (paid tier) for accurate datacenter/hosting provider identification, reliable ISP data, and precise city-level geolocation. Scoring decisions that affect UNL composition depend on this accuracy.

### Validator Profile (LLM Input)

Each validator is represented as a structured JSON object:

```json
{
  "validator": {
    "master_key": "ED2677AB...",
    "domain": "example-validator.com",
    "domain_verified": true,
    "identity": {
      "entity_name": "Example Corp",
      "jurisdiction": "Switzerland",
      "entity_type": "company",
      "kyc_verified": true,
      "institutional_domain_verified": true,
      "institutional_domain": "example-corp.com"
    }
  },
  "consensus_performance": {
    "agreement_1h": { "validated": 598, "missed": 2, "score": 99.67 },
    "agreement_24h": { "validated": 14350, "missed": 50, "score": 99.65 },
    "agreement_30d": { "validated": 430000, "missed": 1200, "score": 99.72 },
    "validation_type": "full",
    "ledger_lag": 1
  },
  "reliability": {
    "uptime_seconds": 8640000,
    "io_latency_ms": 12,
    "consecutive_missed_checks": 0,
    "websocket_connected": true,
    "alert_history": [...]
  },
  "software": {
    "server_version": "2.4.0",
    "network_majority_version": "2.4.0",
    "version_current": true
  },
  "history": {
    "first_seen": "2025-01-15T00:00:00Z",
    "operating_days": 400,
    "agreement_trend": "stable",
    "alerts_last_30d": 0
  },
  "participation": {
    "amendment_votes": { "featureDynamicUNL": "yes", "featureVaults": "yes" },
    "amendment_alignment_with_network": 0.95,
    "fee_votes": { "base_fee": 10, "reserve_base": 10000000, "reserve_inc": 2000000 }
  },
  "geography": {
    "continent": "Europe",
    "country": "Switzerland",
    "city": "Zurich",
    "isp": "Hetzner Online GmbH",
    "datacenter": "Hetzner FSN1"
  }
}
```

### LLM Step 1: Individual Scoring

**Input:** All validator profiles + system prompt with scoring philosophy.

**Output per validator:**
- Score: integer 0-100
- Reasoning: a few sentences covering everything needed for a human to understand why the score is what it is
- Flags: any concerns or notable observations

**What the LLM evaluates:**

| Dimension | What the LLM Considers |
|-----------|----------------------|
| Consensus quality | Agreement scores across all time windows. Distinguishes between consistently high scores vs recovery from problems. A brief dip from upgrading software is a sign of diligence, not a problem. |
| Operational reliability | Uptime, latency, connection health, alert history. A single quickly-resolved alert is very different from recurring issues. |
| Software diligence | Running the latest version shows active maintenance. Outdated software is a risk factor. How quickly did the operator upgrade after a new release? |
| Historical track record | Longevity matters. 400 days of stable operation is more trustworthy than 5 days of perfect scores. |
| Network participation | Amendment voting reveals governance engagement. Consistent opposition to upgrades that pass suggests misalignment with network evolution. |
| Identity and reputation | Given verified identity, the LLM assesses entity reputation, jurisdiction, sanctions risk, and alignment with PostFiat's goals. Institutional domain verification provides additional trust signal. |

**Prompt philosophy:** The system prompt establishes priorities and values, not exact formulas. "Consensus performance is the most important factor" rather than "agreement = 40 points." This preserves the LLM's ability to interpret context.

### LLM Step 2: Network Diversity Adjustment

**Input:** All Step 1 results + geographic/entity/ISP distribution summary.

**Output per validator:**
- Adjusted score: integer 0-100
- Adjustment reasoning: a few sentences explaining why the score was adjusted (or left unchanged)

**What the LLM optimizes for:**

| Dimension | How Adjustment Works |
|-----------|---------------------|
| Geographic diversity | Validators in underrepresented regions get boosted. Overrepresented regions get penalized. The LLM understands nuance: 3 validators in Germany in different datacenters is less risky than 3 in the same datacenter. |
| Entity concentration | No single entity should dominate the UNL. Even high-quality validators get penalized if their entity already has validators in the set. |
| ISP/datacenter diversity | Same ISP or datacenter = correlated failure risk. The LLM penalizes clustering. |
| Jurisdictional diversity | Spread across legal jurisdictions for resilience against regulatory action. |

**Example:**

```
Step 1 results (top 5 by raw score):
  Validator A: 98, Zurich, Hetzner
  Validator B: 97, Frankfurt, Hetzner
  Validator C: 96, Berlin, AWS
  Validator D: 95, Tokyo, NTT
  Validator E: 94, São Paulo, Equinix

Step 2 adjustment:
  Validator D: 95 → 98 (Asia underrepresented, unique ISP — boosted)
  Validator E: 94 → 97 (South America underrepresented — boosted)
  Validator A: 98 → 96 (Hetzner already represented)
  Validator C: 96 → 94 (Europe overrepresented, but different ISP)
  Validator B: 97 → 93 (Europe overrepresented, same ISP as A)

Final ranking: D (98), E (97), A (96), C (94), B (93)
```

### Anti-Gaming Properties

- KYC/KYB gate prevents Sybil attacks
- The LLM can detect patterns that look artificially optimized
- Scoring criteria are public (transparency), but contextual LLM interpretation makes gaming harder than optimizing a fixed formula
- The LLM sees all validators together, so coordinated manipulation across multiple validators is detectable

### Non-Determinism

LLMs are non-deterministic. The same inputs may produce slightly different scores across runs. This is accepted as a property of the system:
- Temperature 0 (or near-zero) minimizes variation
- Structured JSON output mode prevents format drift
- Every scoring run is published regardless of whether the UNL changes, so the community can track score stability
- The important thing is that the process is transparent and provably not fraudulent — not that it produces identical numbers each run

---

## 5. Proof of Computation

### The Trust Problem

Publishing inputs, prompts, and outputs proves transparency but doesn't prove the LLM was actually called. Without cryptographic proof, the foundation could fabricate plausible-looking scores. The community needs hard evidence.

### Solution: MPC-TLS via Opacity Network

Opacity Network provides cryptographic proof of LLM API interactions using zkTLS with Multi-Party Computation over TLS (MPC-TLS).

**How it works:**

```
Scoring service sends prompt to LLM
        ↓
Request goes through Cloudflare AI Gateway (proxy that logs the request)
        ↓
Opacity's MPC-TLS notary network participates in the TLS session
Neither the scoring service nor the notary alone can see the full TLS session key
        ↓
LLM provider (Anthropic/OpenAI) returns scores + reasoning
        ↓
Opacity produces a cryptographic proof that:
  • A real HTTPS request was made to the specific LLM API endpoint
  • The specific prompt was sent (verifiable)
  • The specific response was received (not tampered with)
  • The response genuinely came from that API, not fabricated locally
```

**Verification:** Anyone can verify the Opacity proof. Forgery is cryptographically infeasible.

### Infrastructure Requirements

**Cloudflare AI Gateway:** A free proxy that sits between the scoring service and the LLM provider. The scoring service changes its base URL to route through Cloudflare rather than calling the LLM API directly. Supports Anthropic, OpenAI, and other major providers. Free tier covers 100K requests/day (more than sufficient). A Cloudflare account is required to create a gateway instance.

**Opacity Network:** Provides the MPC-TLS notary network and proof generation. TypeScript SDK available. The scoring service integrates the Opacity adapter to generate proofs alongside each LLM call.

**LLM Provider:** The actual model provider (e.g., Anthropic for Claude). The scoring service uses its own API key. Cloudflare proxies the call transparently — same models, same API, just a different base URL.

---

## 6. Publication and Transparency

### What Gets Published (Every Scoring Run)

Every scoring run publishes the complete audit trail, even if the UNL doesn't change:

| Artifact | Where Published |
|----------|----------------|
| Validator profiles (LLM inputs) | IPFS |
| Scoring configuration (model version, prompt versions, pipeline version) | IPFS |
| Step 1 LLM output (individual scores + reasoning) | IPFS |
| Step 2 LLM output (adjusted scores + reasoning) | IPFS |
| Opacity MPC-TLS proofs (Step 1 and Step 2) | IPFS |
| Final UNL JSON | IPFS + HTTPS endpoint |
| UNL hash + IPFS CID + sequence + config version | On-chain transaction |

### Why IPFS

IPFS provides permanent, content-addressed, decentralized storage:
- Content-addressed: the CID (Content Identifier) is a hash of the content, making tampering impossible
- Decentralized: available from any IPFS gateway, not dependent on foundation servers
- Permanent: content persists as long as it's pinned
- Verifiable: anyone can confirm the content at a CID matches what was claimed

The IPFS CID is published on-chain alongside the UNL hash, creating a link from the immutable ledger to the complete audit trail.

### How Anyone Can Verify Scoring

```
1. Find the on-chain tx from master_account → memo_account
   └── Extract: UNL hash, IPFS CID, sequence, scoring config version

2. Fetch from IPFS using the CID
   └── Get: validator profiles, prompts, LLM outputs, proofs, UNL JSON

3. Verify UNL hash
   └── Compute sha512Half(UNL JSON from IPFS) == hash from on-chain tx

4. Verify Opacity proofs
   └── Confirm LLM API calls actually happened (not fabricated)

5. Verify inputs
   └── Cross-check validator profiles against VHS public API data

6. Review reasoning
   └── Read the LLM's explanation for each validator's score
```

---

## 7. Validator Fees

Validators on PostFiat earn transaction fees as compensation for running infrastructure. This is a departure from XRPL, where validators receive no direct compensation.

**Current state:** The fee structure is controlled by the PostFiat Foundation. Specific fee levels and distribution mechanisms are to be designed separately from the Dynamic UNL system.

**Future state:** Fee governance will be transferred to the community governance mechanism as it matures. This is part of the broader governance transition described below.

**Relevance to Dynamic UNL:** Fees provide an economic incentive for validators to maintain high performance (uptime, agreement, software currency) because their position in the UNL — and therefore their fee income — depends on their scores.

---

## 8. PostFiat Foundation Role and Governance

### Guiding Principle

The PostFiat Foundation is the steward of the Dynamic UNL system. At launch, it holds centralized authority exercised transparently. This authority is designed to be progressively transferred to community governance. Even while centralized, every action is publicly visible and auditable via on-chain transactions, IPFS publications, and the open-source scoring repository.

### Foundation Powers

#### Scoring System

| Power | How Exercised | Governance Transfer Priority |
|-------|--------------|------------------------------|
| **LLM model selection** | Foundation selects and pins the model version. Published with every scoring run. Changes communicated in advance. | Medium |
| **Prompt authorship** | Hardcoded in open-source scoring repo. Changes go through branch protection with clear changelogs. | Medium |
| **Scoring cadence** | Cron job, default weekly. Foundation can adjust. | Low (transfers first) |
| **Emergency scoring trigger** | Foundation manually triggers scoring service. Same pipeline, same proofs. | High (retain longer) |
| **Emergency UNL fallback** | Foundation publishes hardcoded UNL directly, bypassing scoring. Publicly visible and explained. | Highest (retain longest, possibly permanently) |
| **KYC/KYB eligibility policy** | Foundation selects third-party provider (SumSub) and defines criteria. Does not run verification itself. | Medium-high |
| **Scoring criteria** | Encoded in prompts (see prompt authorship). Publicly visible in repo. | Medium |

#### Publication and Infrastructure

| Power | How Exercised | Governance Transfer Priority |
|-------|--------------|------------------------------|
| **Master account control** | Private key that signs on-chain hash transactions. Most sensitive infrastructure. Must have hardware-level security (HSM) in production. | Very high (→ eventual multisig) |
| **HTTPS endpoint** | Foundation operates the web server hosting UNL JSON. Content must match on-chain hash (enforced by nodes). | Medium (could be replaced by IPFS-only) |
| **Publisher key** | Cryptographic key for signing validator lists. Derived from this key is the master account. | Very high (tied to master account) |
| **Cloudflare AI Gateway account** | Operational infrastructure for proxying LLM calls. | Low |
| **Opacity Network account** | Operational infrastructure for proof generation. | Low |
| **IPFS publication** | Publishing audit trail to IPFS. Inherently trustless (content-addressed). | Low |

#### Network-Level

| Power | How Exercised | Governance Transfer Priority |
|-------|--------------|------------------------------|
| **Amendment voting guidance** | Foundation publishes recommendations. Validators vote independently. | Medium |
| **MAX_UNL_VALIDATORS** | Hardcoded at 35. Research needed before mainnet. | Low-medium |
| **Network parameters** | Flag ledger interval, amendment majority times. Protocol-level changes. | Low |
| **Transaction fees** | Fee structure and levels. Currently foundation-controlled. | Medium (transfers to governance) |

### Phased Decentralization

Each power transfers independently. Lower-risk items transfer first.

**Phase 1 — Operational parameters:**
- Scoring cadence
- Cloudflare/Opacity account management
- MAX_UNL_VALIDATORS (with research backing)

**Phase 2 — Scoring governance:**
- Prompt authorship and scoring criteria
- LLM model selection
- Amendment voting guidance
- Transaction fee governance

**Phase 3 — Identity and infrastructure:**
- KYC/KYB eligibility policy
- HTTPS endpoint (potentially replaced by IPFS-only)
- Network parameters

**Phase 4 — Core trust infrastructure:**
- Master account and publisher key (→ multisig)
- Emergency scoring trigger
- Emergency UNL fallback (may retain foundation veto permanently)

The specific governance mechanism (on-chain voting, multisig, DAO, token-weighted voting) is to be designed separately. The Dynamic UNL architecture is built so any mechanism can plug in — powers are clearly defined and separable.

---

## 9. Scoring Service

The scoring service is a new, standalone, open-source repository that orchestrates the entire scoring and publication pipeline.

### Repository Structure

- **Public repository** under the PostFiat organization (full transparency)
- Contains: data pipeline, LLM orchestration, Opacity integration, IPFS publication, UNL publication, version-controlled prompts
- Branch protection on main (reviews required)
- Automatic deployment workflow (CI/CD via GitHub Actions)
- Only the PostFiat Foundation can trigger manual runs (access-controlled)

### What It Does (Per Scoring Run)

```
PHASE 1: DATA COLLECTION
├── Query VHS PostgreSQL (agreement, topology, manifests, ballots, geo)
├── Query Network Monitoring PostgreSQL (alerts, snapshots)
├── Query on-chain identity records (KYC, institutional domain verification)
├── Check KYC/KYB status via third-party provider API
└── Build validator profile JSON for each eligible validator

PHASE 2: LLM STEP 1 — INDIVIDUAL SCORES
├── Construct system prompt (hardcoded, version-controlled)
├── Send all profiles to LLM via Cloudflare AI Gateway
├── Opacity MPC-TLS notary witnesses the TLS session
├── Receive scores + reasoning for each validator
└── Store Opacity Proof #1

PHASE 3: LLM STEP 2 — DIVERSITY ADJUSTMENT
├── Construct Step 2 prompt (hardcoded, version-controlled)
├── Send Step 1 results + geo/entity/ISP data via Cloudflare AI Gateway
├── Opacity MPC-TLS notary witnesses the TLS session
├── Receive adjusted scores + reasoning
└── Store Opacity Proof #2

PHASE 4: UNL CONSTRUCTION
├── Rank validators by adjusted score (descending)
├── Select top 35 (MAX_UNL_VALIDATORS)
├── Build UNL JSON: {"validators": [{pubkey, score}, ...], "version": N}
└── Compute sha512Half of UNL JSON

PHASE 5: PUBLICATION
├── IPFS: publish all artifacts (inputs, config, outputs, proofs, UNL JSON)
├── HTTPS: upload UNL JSON to endpoint
└── On-chain: send tx with hash + IPFS CID + sequence + config version
```

### Emergency Operations

**Emergency re-scoring:** Foundation triggers the same pipeline manually. Produces full audit trail with proofs. Community can verify it was a legitimate run.

**Custom UNL override:** Foundation publishes a hardcoded UNL directly, bypassing the LLM. No Opacity proof (no LLM was called). An IPFS notice is published explaining the override and the reason. This is a safety net for network continuity, not normal operation.

### Scoring Cadence

- Default: **weekly**
- Foundation can adjust the frequency
- Foundation can trigger emergency runs at any time
- Every run publishes results regardless of whether the UNL changes

### LLM Model Configuration

- Model is **pinned to a specific version** and published with every scoring run
- Model changes are deliberate, announced in advance, and treated like protocol upgrades
- Changes happen only when justified: significantly better model available, current model deprecated, or cost reasons
- The community can verify which model was used via the Opacity proof

### Versioning

Every scoring run publishes the exact version of all components:

| Component | What's Versioned |
|-----------|-----------------|
| Prompt version | System prompts for Step 1 and Step 2 (tracked by hash) |
| Model version | Exact LLM model (e.g., "claude-opus-4-6") |
| Data pipeline version | Code that builds validator profiles |
| Diversity criteria version | Guidelines for Step 2 adjustments |
| Scoring service version | The orchestration code |

---

## 10. End-to-End System Map

### Repository Map

```
postfiatorg/
│
├── postfiatd                       [EXISTS — MODIFY]
│   ├── UNLHashWatcher              New C++ component (prototyped)
│   ├── DynamicUNLManager           New C++ component (prototyped)
│   ├── ValidatorSite integration   Modify existing fetch path
│   ├── BuildLedger integration     Modify existing ledger building
│   ├── Application wiring          Modify initialization
│   └── featureDynamicUNL           New amendment
│
├── validator-history-service       [EXISTS — MODIFY]
│   └── MaxMind GeoIP2 upgrade      Replace GeoLite2 with paid tier
│
├── scoring-onboarding              [EXISTS — NO CHANGES]
│   └── KYC/KYB + domain verification already implemented
│
├── dynamic-unl-scoring             [NEW REPOSITORY]
│   ├── Data pipeline               Collects metrics, builds profiles
│   ├── LLM orchestration           Step 1 + Step 2 calls
│   ├── Opacity integration         MPC-TLS proof generation
│   ├── IPFS publication            Audit trail storage
│   ├── UNL publication             HTTPS + on-chain hash
│   ├── Prompts                     Version-controlled
│   └── CI/CD + manual trigger      Deployment + foundation controls
│
├── network-monitoring              [EXISTS — NO CHANGES]
├── infra-monitoring                [EXISTS — NO CHANGES]
├── explorer                        [EXISTS — NO CHANGES (future: show scores)]
├── testnet-bot                     [EXISTS — NO CHANGES]
└── layer-one-agent                 [EXISTS — NO CHANGES]
```

### Cross-Service Data Flow

```
┌────────────────────────────────────────────────────────────────────────┐
│                                                                        │
│  ┌──────────┐  SQL   ┌─────────────────────┐  SQL   ┌──────────────┐   │
│  │   VHS    │◄────── │  SCORING SERVICE    │──────▶ │  Network     │   │
│  │ Postgres │        │  (new repo)         │        │  Monitoring  │   │
│  └──────────┘        │                     │        │  Postgres    │   │
│                      │                     │        └──────────────┘   │
│  ┌──────────┐  API   │                     │                           │
│  │ KYC/KYB  │◄────── │                     │                           │
│  │ (SumSub) │        │                     │                           │
│  └──────────┘        │                     │                           │
│                      │         │           │                           │
│                      └─────────┼───────────┘                           │
│                                │                                       │
│               ┌────────────────┼────────────────┐                      │
│               │                │                │                      │
│               ▼                ▼                ▼                      │
│        ┌────────────┐  ┌────────────┐  ┌────────────┐                  │
│        │ Cloudflare │  │  Opacity   │  │   IPFS     │                  │
│        │ AI Gateway │  │  Network   │  │            │                  │
│        │     │      │  │ (proofs)   │  │ (audit     │                  │
│        │     ▼      │  │            │  │  trail)    │                  │
│        │  LLM API   │  │            │  │            │                  │
│        └────────────┘  └────────────┘  └────────────┘                  │
│                                │                │                      │
│                                └────────┬───────┘                      │
│                                         │                              │
│                                         ▼                              │
│                              ┌────────────────────┐                    │
│                              │   PFT LEDGER       │                    │
│                              │   (on-chain tx     │                    │
│                              │    with hash +     │                    │
│                              │    IPFS CID)       │                    │
│                              └─────────┬──────────┘                    │
│                                        │                               │
│                                        ▼                               │
│                              ┌────────────────────┐                    │
│                              │   ALL NODES        │                    │
│                              │   (postfiatd)      │                    │
│                              │                    │                    │
│                              │   UNLHashWatcher   │                    │
│                              │   DynamicUNLManager│                    │
│                              │   ValidatorSite    │                    │
│                              │   ValidatorList    │                    │
│                              │   Consensus        │                    │
│                              └────────────────────┘                    │
│                                                                        │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 11. What Needs to Be Built

### New Work

| Component | Description | Effort |
|-----------|-------------|--------|
| **Scoring service repository** | Data pipeline, LLM orchestration, Opacity integration, IPFS publication, UNL publication, CI/CD | ~50% of total work |
| **UNLHashWatcher** (postfiatd) | On-chain hash monitoring. Already prototyped on `feature/dynamic-unl` branch. | Included below |
| **DynamicUNLManager** (postfiatd) | Score-based validator selection. Already prototyped. | Included below |
| **featureDynamicUNL amendment** | Amendment definition. Already prototyped. | Included below |

### Modifications to Existing Code

| Component | Description | Effort |
|-----------|-------------|--------|
| **postfiatd** (BuildLedger, Application, ValidatorSite) | Integration of new components into existing code paths. ValidatorSite integration is the main unprototyped piece. | ~20% of total work |
| **VHS MaxMind upgrade** | Replace GeoLite2 with GeoIP2 paid tier. Configuration + API key change. | ~5% of total work |

### External Service Setup

| Service | Description | Effort |
|---------|-------------|--------|
| **Cloudflare AI Gateway** | Create account, set up gateway instance | ~15% of total work (combined) |
| **Opacity Network** | Set up team, integrate SDK |  |
| **IPFS** | Choose pinning service (Pinata, web3.storage, etc.) or self-host |  |

### No Changes Needed

- network-monitoring, infra-monitoring, explorer, testnet-bot, layer-one-agent
- scoring-onboarding (KYC/KYB and domain verification already built)
- Existing validator/RPC/archive node infrastructure (just needs updated binary)
- Consensus engine, RPC handlers, transaction processors, overlay, nodestore

---

## 12. Deployment Roadmap

```
STEP 1 — Parallel Start
├── postfiatd: refine UNLHashWatcher + DynamicUNLManager from prototype
├── postfiatd: implement ValidatorSite integration (main unprototyped piece)
├── VHS: MaxMind GeoIP2 upgrade
└── External: set up Cloudflare AI Gateway + Opacity accounts

STEP 2 — Scoring Service Development (after Step 1)
├── Data pipeline (depends on VHS with GeoIP2 data)
├── LLM orchestration (depends on Cloudflare AI Gateway)
├── Opacity integration (depends on Opacity account)
├── IPFS publication
├── UNL publication (on-chain + HTTPS)
└── Prompt design and testing

STEP 3 — Integration Testing (after Step 2)
├── Full pipeline test on devnet
├── Proof verification end-to-end
├── UNL application test: scoring service → on-chain → node picks up new UNL
└── Failure mode testing

STEP 4 — Devnet Deployment (after Step 3)
├── Deploy updated postfiatd to devnet validators
├── Enable featureDynamicUNL amendment on devnet
├── Deploy scoring service
└── Run first real scoring cycle

STEP 5 — Testnet Rollout (after Step 4)
├── Deploy to testnet
├── Enable amendment (3-day majority time)
└── Monitor and iterate

STEP 6 — Mainnet Preparation (before mainnet)
├── UNL size research (determine optimal MAX_UNL_VALIDATORS)
├── Security audit of scoring service and postfiatd changes
├── Prompt refinement based on devnet/testnet experience
└── Governance mechanism design

STEP 7 — Mainnet Launch
├── Deploy to mainnet
├── Enable amendment (14-day majority time)
└── Begin phased governance transition
```

---

## Appendix A: Existing Prototype

A working prototype exists on the `feature/dynamic-unl` branch of postfiatd. It includes:
- UNLHashWatcher (header, implementation, comprehensive unit tests)
- DynamicUNLManager (header, implementation, unit tests)
- Integration tests (end-to-end flow with amendment gating)
- BuildLedger and Application.cpp integration
- Amendment definition
- Design documentation (`docs/DynamicUNL.md`)

The prototype proves the node-side mechanics work. What remains is the ValidatorSite integration (hooking hash verification into the actual fetch path) and the entire scoring/publication pipeline.

## Appendix B: UNL JSON Format

The format published to the HTTPS endpoint and verified by nodes:

```json
{
  "validators": [
    {"pubkey": "ED7A82...", "score": 98},
    {"pubkey": "ED3B91...", "score": 97},
    {"pubkey": "EDF4C2...", "score": 95}
  ],
  "version": 1
}
```

Nodes compute `sha512Half` of this JSON and verify it matches the hash published on-chain.

## Appendix C: Key References

| Resource | Location |
|----------|----------|
| Dynamic UNL prototype | `feature/dynamic-unl` branch of postfiatd |
| Prototype design doc | `docs/DynamicUNL.md` on prototype branch |
| Scoring onboarding system | `postfiatorg/scoring-onboarding` repository |
| VHS documentation | `agent-hub/products/blockchain/repos/validator-history-service.md` |
| Network monitoring docs | `agent-hub/products/blockchain/repos/network-monitoring.md` |
| Infrastructure overview | `agent-hub/products/blockchain/INFRASTRUCTURE.md` |
| Opacity Network | https://opacity.network |
| Cloudflare AI Gateway | https://developers.cloudflare.com/ai-gateway/ |
| XRPL consensus analysis | https://arxiv.org/pdf/1802.07242 |
