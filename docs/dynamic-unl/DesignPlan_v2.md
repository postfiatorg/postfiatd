# Dynamic UNL: Upgraded Design Plan

This document supersedes `DesignPlan_v1.md`. Key changes: distributed LLM scoring via Oracle Nodes, TLSNotary proofs on all data sources, Opacity Network for data collection attestation, foundation TLSNotary server for Oracle Node LLM call attestation, and commit-reveal mechanism for submission integrity.

---

## 1. What is Dynamic UNL?

The Unique Node List (UNL) determines which validators participate in consensus. On XRPL, this list is effectively hardcoded — controlled by a single entity (Ripple) with no on-chain accountability or transparency into how validators are selected.

Dynamic UNL replaces this with an automated, transparent, LLM-driven system where validators are scored by multiple independent Oracle Nodes, with cryptographic proof that each score was genuinely produced by a real LLM API call. Final scores are aggregated via median across all Oracle Nodes, eliminating single-entity control over the most critical network decision.

### Core Differentiators from XRPL

| Aspect | XRPL | PostFiat Dynamic UNL |
|--------|------|---------------------|
| UNL selection | Manual, opaque | Automated, LLM-scored by distributed Oracle Nodes |
| Accountability | None on-chain | Full audit trail on-chain + IPFS |
| Validator criteria | Undisclosed | Published scoring criteria and reasoning |
| Update mechanism | Publisher signs list | On-chain hash + score-based selection |
| Proof of computation | None | Opacity Network (data collection) + TLSNotary (LLM calls) cryptographic proofs |
| Scoring trust model | N/A | Distributed — multiple Oracle Nodes independently score, median aggregation |
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
4. On approval, the foundation queries the SumSub API to confirm the result — this query is attested by **Opacity Network** (a decentralized MPC-TLS notary network built on TLSNotary) to prove the data genuinely came from SumSub
5. The result is published on-chain by the foundation's master account as a memo transaction:

```json
{
  "type": "pf_identity_v1",
  "validator_address": "rXXX...",
  "identity_type": "kyc",
  "proof_hash": "<sumsub_applicant_id>",
  "proof_type": "sumsub_kyc_opacity",
  "proof_cid": "<IPFS CID of TLSNotary proof>",
  "decision": "approved",
  "verified_at": "2025-06-15T12:00:00Z"
}
```

The Opacity proof stored on IPFS cryptographically proves the SumSub API returned an approval for this applicant. Opacity uses a decentralized notary network (multiple independent notaries with EigenLayer economic security), providing stronger trust guarantees than a single notary. Anyone can fetch the proof via the CID and verify it independently.

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
4. Foundation queries the DNS record via **DNS-over-HTTPS** (DoH) using Cloudflare (`cloudflare-dns.com/dns-query`) or Google (`dns.google/resolve`)
5. This DoH query is attested by **Opacity Network**, producing a cryptographic proof that the DNS provider's API returned the expected TXT record
6. Foundation verifies the signature against the validator's public key
7. On success, published on-chain with TLSNotary proof on IPFS:

```json
{
  "type": "pf_identity_v1",
  "validator_address": "rXXX...",
  "identity_type": "university",
  "identifier": "stanford.edu",
  "proof_hash": "<sha256_of_verification_data>",
  "proof_type": "dns_txt_opacity",
  "proof_cid": "<IPFS CID of TLSNotary proof>",
  "verified_at": "2025-06-15T12:00:00Z"
}
```

Anyone can fetch the Opacity proof from IPFS and verify that the DoH provider genuinely returned the DNS record at the time of verification.

Institutional domain verification is not mandatory for UNL inclusion but positively influences the validator's score — it demonstrates the validator is operated by a legitimate, identifiable institution.

#### On-Chain Publication of Identity

All identity verification results (KYC approvals, institutional domain verifications) are published on-chain by the foundation's master account. This creates an immutable, publicly queryable record of which validators have verified identities. The scoring data pipeline reads these on-chain records when building validator profiles.

### Oracle Node Registration

Oracle Nodes are a new node type that provides distributed LLM scoring for the Dynamic UNL system. Any entity can register as an Oracle Node.

**Registration requirements:**
- Bond **100,000 PFT** on-chain (locked while active, returned in full on deregistration — not slashable)
- Link a **Task Node address** (the on-chain address associated with the operator's Task Node identity)
- Complete a **test round** demonstrating TLSNotary capability (successfully produce and submit a valid TLSNotary proof of an LLM call through the foundation's notary)

**Liveness requirements:**
- Oracle Nodes must participate in at least 1 of the last 4 scoring rounds
- Failure to meet liveness requirements results in the node being marked inactive
- Inactive nodes are excluded from quorum calculations
- Inactive nodes can reactivate by participating in the next scoring round

**Deregistration:**
- Oracle Node submits a deregistration transaction
- PFT bond is unlocked and returned in full
- Node is removed from the active registry

### Existing Infrastructure

The onboarding system already exists in the `scoring-onboarding` repository:
- SumSub KYC integration (WebSDK, webhook processing, on-chain publishing)
- DNS-based institutional domain verification
- On-chain identity publishing via memo transactions
- Automatic retry for failed on-chain publishes
- Wallet authorization for portal access

---

## 4. Validator Scoring

The scoring system is the core differentiator of PostFiat's Dynamic UNL. Multiple independent Oracle Nodes each call a pinned LLM with the same input data and prompts, producing independent scores that are aggregated via median. This distributes trust across multiple parties — no single entity controls the scoring outcome.

### Architecture

```
KYC/KYB gate (binary: eligible or not)
        │
        ↓ only verified validators proceed
        │
Data pipeline (foundation, Opacity-attested)
        │   Queries VHS, Network Monitoring, MaxMind, SumSub
        │   Each query attested by Opacity Network (decentralized MPC-TLS)
        │   Builds validator profile JSON for each eligible validator
        │   Publishes snapshot + TLSNotary proofs → IPFS
        │
        ↓
        │
Scoring round announced on-chain
        │   Foundation tx with snapshot IPFS CID + round number + deadlines
        │
        ↓
        │
Oracle Nodes independently score (distributed)
        │   Each Oracle Node:
        │   ├── Fetches snapshot from IPFS
        │   ├── Verifies TLSNotary proofs on data
        │   ├── Calls pinned LLM API (Step 1: individual scores)
        │   │   └── Attested by foundation's TLSNotary notary
        │   ├── Calls pinned LLM API (Step 2: diversity adjustment)
        │   │   └── Attested by foundation's TLSNotary notary
        │   └── Creates presentations (selective disclosure)
        │
        ↓
        │
Commit-reveal
        │   Commit: each Oracle Node submits hash(scores + salt + round_number) on-chain
        │   Reveal: each Oracle Node publishes scores + proofs to IPFS, submits CID on-chain
        │
        ↓
        │
Aggregation
        │   Verify all TLSNotary proofs
        │   Verify all reveals match commits
        │   Compute median score per validator across all valid Oracle Node submissions
        │
        ↓
        │
Top 35 validators by median adjusted score → published as UNL
```

### Why Oracle Nodes Instead of a Single Scorer

The UNL determines who participates in consensus — it is the most critical decision in the network. A single scorer (even with cryptographic proofs) is a single point of trust:

- Proofs prove the LLM was called, but don't prevent calling it multiple times and publishing the preferred result
- A single scorer can be compromised, coerced, or make errors
- Multiple independent scorers with median aggregation provide redundancy, outlier resistance, and reduced single-entity power
- This pattern is proven in blockchain oracle networks (Chainlink, Ritual.net, EigenLayer AVS)

### Why Two Steps

Step 1 answers: "How good is this validator on its own merits?"
Step 2 answers: "Given all the candidates, what combination produces the most resilient network?"

Geographic diversity and entity concentration are relative to the current set, not properties of an individual validator. A validator in Tokyo scores differently depending on whether there are 0 or 12 other validators in Asia.

Each Oracle Node performs both steps independently. The geographic, ISP, and datacenter data needed for Step 2 is included in the data snapshot (sourced from MaxMind, attested by TLSNotary).

### Why All Validators in One Prompt

The LLM sees all candidates together in a single call per step:
- Comparative context matters: a 97% agreement score means different things if the average is 99% vs 95%
- The LLM can spot anomalies better when it sees the full distribution
- One call is cheaper and faster than N separate calls
- Even 100 validator profiles (~1-2KB JSON each) fit within modern context windows

### Data Pipeline: What Feeds the LLM

The foundation collects data from existing PostFiat infrastructure and builds a structured JSON profile for each validator. Each data fetch is attested by **Opacity Network** — a decentralized MPC-TLS notary network built on TLSNotary — to prove the data genuinely came from the claimed source. Using Opacity (rather than the foundation's own TLSNotary notary) prevents self-attestation: the foundation cannot notarize its own data fetches.

**Data sources and their Opacity attestation:**

| Source | Data | Proof |
|--------|------|-------|
| VHS API | Agreement scores (1h/24h/30d), uptime, latency, peer connections, server version, manifests, amendment voting, fee votes, domain verification, WebSocket health | Opacity Network attests VHS API response |
| Network Monitoring API | Alert history (agreement drops, offline events, version mismatches), severity, resolution times, consecutive missed checks | Opacity Network attests Network Monitoring API response |
| MaxMind GeoIP2 API | Continent, country, city, ISP, datacenter for each validator | Opacity Network attests MaxMind API response |
| On-chain identity records | KYC/KYB status, institutional domain verification status | Directly verifiable from ledger (no attestation needed) |

**Why VHS and Network Monitoring need Opacity proofs:** Although these are PostFiat's own services with public APIs, the data snapshot is consumed by Oracle Nodes who must trust that the foundation didn't alter the data between fetching and publishing. Opacity proofs on the API responses prove the snapshot matches what the services actually returned.

**Fallback:** If Opacity Network is unavailable, the foundation may use a public third-party TLSNotary notary server (e.g., PSE's reference notary). Proofs generated via fallback are clearly marked in the proof metadata (`notary_type: "tlsnotary_fallback"`). The community can distinguish Opacity-attested proofs from fallback proofs. Fallback proofs provide weaker trust guarantees (single notary instead of decentralized network).

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

**Input:** The Oracle Node's own Step 1 results + geographic/entity/ISP distribution data from the snapshot.

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

### Aggregation: From Oracle Node Scores to UNL

After all Oracle Nodes reveal their scores:

1. Verify each Oracle Node's TLSNotary proofs (both Step 1 and Step 2)
2. Verify each reveal matches its commit hash
3. Discard submissions with invalid proofs or mismatched commits
4. For each validator: compute the **median** of all valid Oracle Node adjusted scores
5. Rank validators by median score (descending)
6. Select top 35 (`MAX_UNL_VALIDATORS`)
7. Build UNL JSON

**Why median:** Median is resistant to outliers. Even if a small number of Oracle Nodes submit manipulated scores, the median remains close to the honest consensus. Mean would be vulnerable to extreme values.

### Anti-Gaming Properties

- **PFT bond** (100,000 PFT) prevents Sybil attacks — creating majority Oracle Nodes is economically prohibitive
- **Commit-reveal** prevents submission-order gaming — nodes can't see others' scores before committing
- **Temperature 0** minimizes LLM output variance across runs, reducing cherry-picking opportunity
- **Median aggregation** resists outlier manipulation from minority dishonest nodes
- **TLSNotary proofs** prevent score fabrication — every LLM call is cryptographically attested
- **Prompt verification** via TLSNotary selective disclosure — verifiers confirm the prescribed prompt was used
- **Input verification** — Oracle Nodes must use the published snapshot (verifiable via input data hash in TLSNotary presentation)
- **KYC/KYB gate** on validators prevents fake validators from entering the scoring pool

### Non-Determinism

LLMs are non-deterministic. Even with temperature 0, the same inputs may produce slightly different scores across different API calls. This is accepted and leveraged:

- Different Oracle Nodes will produce slightly different scores — this is a feature, not a bug
- Median aggregation smooths variance naturally
- Structured JSON output mode prevents format drift
- Every scoring round is published regardless of whether the UNL changes, so the community can track score stability across Oracle Nodes
- The important thing is that the process is transparent, distributed, and provably not fraudulent

---

## 5. Proof of Computation

### The Trust Problem

Publishing inputs, prompts, and outputs proves transparency but doesn't prove the LLM was actually called. Without cryptographic proof, any party (the foundation or an Oracle Node) could fabricate plausible-looking scores. The community needs hard evidence at every stage: data collection, LLM scoring, and submission.

### Solution: Two-Layer Proof Architecture

The system uses two complementary MPC-TLS technologies, each chosen for the trust properties it provides in its specific role:

**Opacity Network (for foundation data collection):**
- Opacity is a decentralized MPC-TLS notary network built on top of TLSNotary, with multiple independent notaries, EigenLayer economic security (staking + slashing), and Intel SGX TEE anti-collusion guarantees
- Used when: Foundation fetches data from VHS, Network Monitoring, MaxMind, and SumSub APIs
- Trust model: Foundation (Prover) ≠ Opacity's decentralized notary network (Notary) — prevents self-attestation with stronger guarantees than a single notary
- Why Opacity here: The foundation cannot notarize its own data fetches — that would be self-attestation. Opacity provides a decentralized third party with economic skin in the game
- Fallback: If Opacity is unavailable, foundation uses a public third-party TLSNotary notary server (e.g., PSE's reference notary). Fallback proofs are marked `notary_type: "tlsnotary_fallback"` and provide weaker trust (single notary vs decentralized network)

**Foundation's TLSNotary Notary Server (for Oracle Node LLM calls):**
- Foundation operates TLSNotary `notary-server` instances with PostFiat-specific customizations
- Used when: Oracle Nodes call the LLM API during scoring rounds
- Trust model: Oracle Node (Prover) ≠ Foundation (Notary) — neither can forge a proof alone
- MPC-TLS property: Foundation cannot see the LLM prompt or response (only encrypted traffic)
- Why TLSNotary here: The trust separation is already built into the protocol (Oracle Node ≠ Foundation). No need for a third-party notary network — the two-party MPC-TLS model already prevents cheating by either side
- Censorship risk: Foundation could refuse to notarize a specific Oracle Node — this is publicly detectable (the node's commit would be missing) and is a governance concern, not a cryptographic one

### Why Two Different Technologies

Opacity is used where the foundation would otherwise be attesting its own actions (data collection). The decentralized notary network ensures no single party — including the foundation — can fabricate data proofs.

TLSNotary is used where the trust separation already exists between two different parties (Oracle Node vs Foundation). Adding Opacity here would provide no additional security benefit — the Oracle Node and the Foundation are already distinct entities in the MPC protocol — while adding unnecessary external dependency and cost.

### What TLSNotary Proofs Cover

**Data collection (attested by Opacity Network):**
| Data Fetch | What the Proof Demonstrates |
|-----------|---------------------------|
| VHS API response | Validator metrics genuinely came from VHS at the stated time |
| Network Monitoring API response | Alert data genuinely came from the monitoring service |
| MaxMind GeoIP2 API response | Geographic data genuinely came from MaxMind |
| SumSub KYC API response | KYC approval genuinely came from SumSub |
| DNS-over-HTTPS response | DNS TXT record genuinely existed at the queried domain (for institutional domain verification) |

**LLM scoring (attested by foundation's notary):**
| LLM Call | What the Proof Demonstrates |
|---------|---------------------------|
| Step 1 API call | Oracle Node sent the prescribed prompt with the published snapshot to the pinned LLM, and received the attested response |
| Step 2 API call | Oracle Node sent the diversity adjustment prompt with Step 1 results to the pinned LLM, and received the attested response |

### Selective Disclosure

Oracle Node TLSNotary presentations reveal:
- **API endpoint** (e.g., `api.anthropic.com/v1/messages`) — proves the correct provider was called
- **Model name** (e.g., `claude-opus-4-6`) — proves the pinned model was used
- **Prompt hash** — proves the prescribed prompt was used (full prompt is in the open-source repo for verification)
- **Input data hash** — proves the published snapshot was used as input
- **Full LLM response** — the actual scores and reasoning

Oracle Node TLSNotary presentations redact:
- **API key** — private to the Oracle Node operator

### Verification

Anyone can verify any TLSNotary proof:
1. Fetch the proof from IPFS using the CID published on-chain
2. Verify the TLSNotary attestation (Notary's signature over the session transcript)
3. Verify the selective disclosure (revealed fields match the claimed content)
4. Cross-reference: API endpoint matches pinned provider, model matches pinned version, prompt hash matches repo, input hash matches published snapshot

### Infrastructure Requirements

**PostFiat TLSNotary notary server:** Based on the open-source TLSNotary `notary-server` reference implementation. Customizations include: PostFiat-specific logging and metrics, rate limiting per registered Oracle Node, health monitoring, and API endpoints for Oracle Nodes to initiate notarization sessions. Foundation operates redundant instances for availability.

**Opacity Network account:** Foundation maintains an Opacity team account for data collection attestation. Opacity routes API calls through Cloudflare AI Gateway and generates MPC-TLS proofs via its decentralized notary network. Configuration requires `OPACITY_TEAM_ID`, `OPACITY_CLOUDFLARE_NAME`, and `OPACITY_PROVER_URL`. Fallback: a public third-party TLSNotary notary server (e.g., PSE's reference notary) if Opacity is unavailable.

**IPFS pinning service:** All proofs are published to IPFS. The foundation uses a pinning service (Pinata, web3.storage, or similar) to ensure proof persistence.

---

## 6. Publication and Transparency

### What Gets Published (Every Scoring Round)

Every scoring round publishes the complete audit trail, even if the UNL doesn't change:

| Artifact | Where Published |
|----------|----------------|
| Data snapshot (validator profiles) | IPFS |
| Opacity proofs of data collection (VHS, MaxMind, SumSub, Network Monitoring) | IPFS |
| Scoring configuration (model version, prompt versions, pipeline version) | IPFS |
| Round announcement (snapshot CID, round number, deadlines) | On-chain transaction |
| Each Oracle Node's commit hash | On-chain transaction (one per Oracle Node) |
| Each Oracle Node's reveal (scores + reasoning + TLSNotary proofs) | IPFS (one CID per Oracle Node, submitted on-chain) |
| Aggregation report (median scores, included/excluded nodes, quorum info) | IPFS |
| Final UNL JSON | IPFS + HTTPS endpoint |
| UNL hash + audit trail IPFS CID + sequence + config version | On-chain transaction |

### Commit-Reveal Mechanism

The commit-reveal mechanism prevents Oracle Nodes from seeing each other's scores before submitting.

**Commit phase:**
1. Each Oracle Node computes their final adjusted scores (Step 2 output)
2. Generates a random salt
3. Submits `sha256(scores_json + salt + round_number)` on-chain as a memo transaction to the designated round account
4. Deadline: commit must arrive before the commit window closes

**Reveal phase:**
1. After the commit deadline, each Oracle Node publishes to IPFS:
   - Full scores JSON (Step 1 + Step 2 results with reasoning)
   - The salt used in the commit
   - TLSNotary proofs for both LLM calls (Step 1 and Step 2)
2. Submits the IPFS CID on-chain as a memo transaction
3. Anyone can verify: `sha256(scores_json + salt + round_number)` matches the commit

**Invalid submissions (discarded during aggregation):**
- Commit without a corresponding reveal
- Reveal that doesn't match commit hash
- TLSNotary proof that fails verification
- Proof showing wrong model, wrong prompt, or wrong input data

### Why IPFS

IPFS provides permanent, content-addressed, decentralized storage:
- Content-addressed: the CID (Content Identifier) is a cryptographic hash of the content, making tampering impossible
- Decentralized: available from any IPFS gateway, not dependent on foundation servers
- Permanent: content persists as long as it's pinned
- Verifiable: anyone can confirm the content at a CID matches what was claimed

The IPFS CID is published on-chain alongside the UNL hash, creating a link from the immutable ledger to the complete audit trail.

### How Anyone Can Verify Scoring

```
1. Find the round announcement tx on-chain
   └── Extract: snapshot IPFS CID, round number, deadlines

2. Fetch data snapshot from IPFS
   └── Verify Opacity proofs on data collection (VHS, MaxMind, etc.)

3. Find all commit txs for this round
   └── List all Oracle Nodes that committed

4. Find all reveal txs for this round
   └── Fetch each Oracle Node's reveal from IPFS
   └── Verify: sha256(scores_json + salt + round_number) == commit hash
   └── Verify: TLSNotary proofs for LLM calls (correct model, prompt, input)

5. Recompute aggregation
   └── Compute median score per validator across all valid reveals
   └── Verify top 35 match the published UNL

6. Verify UNL hash
   └── Compute sha512Half(UNL JSON) == hash from final on-chain tx

7. Review reasoning
   └── Read each Oracle Node's LLM reasoning for each validator's score
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

The PostFiat Foundation is the steward of the Dynamic UNL system. At launch, it holds centralized authority exercised transparently. This authority is designed to be progressively transferred to community governance. Even while centralized, every action is publicly visible and auditable via on-chain transactions, IPFS publications, TLSNotary proofs, and the open-source scoring repository.

Compared to the previous design (where the foundation ran the LLM scoring directly), the foundation's role is reduced: it collects and publishes data (with TLSNotary proofs), operates the TLSNotary notary for Oracle Nodes, and aggregates results. The actual scoring decisions are made by independent Oracle Nodes.

### Foundation Powers

#### Scoring System

| Power | How Exercised | Governance Transfer Priority |
|-------|--------------|------------------------------|
| **LLM model selection** | Foundation selects and pins the model version. Published with every scoring run. Changes communicated in advance. | Medium |
| **Prompt authorship** | Hardcoded in open-source scoring repo. Changes go through branch protection with clear changelogs. | Medium |
| **Scoring cadence** | Default weekly. Foundation can adjust. | Low (transfers first) |
| **Emergency scoring trigger** | Foundation manually triggers a scoring round. Same pipeline, same proofs. | High (retain longer) |
| **Emergency UNL fallback** | Foundation publishes hardcoded UNL directly, bypassing Oracle Node scoring. Publicly visible and explained. No TLSNotary proofs (no LLM was called). An IPFS notice is published explaining the override. | Highest (retain longest, possibly permanently) |
| **KYC/KYB eligibility policy** | Foundation selects third-party provider (SumSub) and defines criteria. Does not run verification itself. | Medium-high |
| **Scoring criteria** | Encoded in prompts (see prompt authorship). Publicly visible in repo. | Medium |

#### Data Collection and Proof Infrastructure

| Power | How Exercised | Governance Transfer Priority |
|-------|--------------|------------------------------|
| **Data snapshot publication** | Foundation collects data from VHS, Network Monitoring, MaxMind, SumSub with Opacity proofs. Publishes snapshot to IPFS. | Medium (could be rotated to community operators) |
| **TLSNotary notary operation** | Foundation operates notary-server for Oracle Node LLM call attestation. Can refuse to notarize (censorship vector, publicly detectable). | High (→ eventual decentralized notary network) |
| **Opacity Network account** | Foundation maintains Opacity team account for data collection attestation. Decentralized notary network provides stronger trust than single-notary alternatives. | Low |
| **IPFS publication** | Publishing audit trail to IPFS. Inherently trustless (content-addressed). | Low |

#### Publication and Trust Infrastructure

| Power | How Exercised | Governance Transfer Priority |
|-------|--------------|------------------------------|
| **Master account control** | Private key that signs on-chain hash transactions. Most sensitive infrastructure. Must have hardware-level security (HSM) in production. | Very high (→ eventual multisig) |
| **HTTPS endpoint** | Foundation operates the web server hosting UNL JSON. Content must match on-chain hash (enforced by nodes). | Medium (could be replaced by IPFS-only) |
| **Publisher key** | Cryptographic key for signing validator lists. Derived from this key is the master account. | Very high (tied to master account) |
| **Aggregation and UNL construction** | Foundation runs the aggregation service that verifies proofs, computes median, builds UNL. Deterministic and auditable — anyone can independently recompute. | Medium (could be rotated) |

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
- Opacity Network account management
- MAX_UNL_VALIDATORS (with research backing)
- IPFS pinning service management

**Phase 2 — Scoring governance:**
- Prompt authorship and scoring criteria
- LLM model selection
- Amendment voting guidance
- Transaction fee governance
- Data snapshot publication (rotate to community operators)

**Phase 3 — Proof infrastructure:**
- TLSNotary notary operation (→ decentralized notary network or community-operated notaries)
- KYC/KYB eligibility policy
- HTTPS endpoint (potentially replaced by IPFS-only)
- Aggregation service (community-operated, verifiable by anyone)
- Network parameters

**Phase 4 — Core trust infrastructure:**
- Master account and publisher key (→ multisig)
- Emergency scoring trigger
- Emergency UNL fallback (may retain foundation veto permanently)

The specific governance mechanism (on-chain voting, multisig, DAO, token-weighted voting) is to be designed separately. The Dynamic UNL architecture is built so any mechanism can plug in — powers are clearly defined and separable.

---

## 9. Scoring Service

The scoring service is a new, standalone, open-source repository that contains three components: the data collection pipeline, the Oracle Node client, and the aggregation service.

### Repository Structure

```
dynamic-unl-scoring/
│
├── pipeline/                    Data collection pipeline (run by foundation)
│   ├── collectors/              VHS, Network Monitoring, MaxMind, SumSub fetchers
│   ├── opacity/                 Opacity Network client integration (data attestation)
│   ├── snapshot/                Snapshot builder and IPFS publisher
│   └── round-announcer/         On-chain round announcement
│
├── oracle-node/                 Oracle Node client (run by Oracle Node operators)
│   ├── fetcher/                 Snapshot fetcher and proof verifier
│   ├── scorer/                  LLM API caller (Step 1 + Step 2)
│   ├── tlsnotary/               TLSNotary client integration (foundation notary)
│   ├── presenter/               Selective disclosure presentation builder
│   └── submitter/               Commit-reveal submission handler
│
├── aggregator/                  Aggregation service (run by foundation, verifiable by anyone)
│   ├── verifier/                TLSNotary proof verifier + commit-reveal checker
│   ├── aggregator/              Median computation + UNL builder
│   └── publisher/               IPFS + HTTPS + on-chain publication
│
├── prompts/                     Version-controlled LLM prompts
│   ├── step1-individual.md      Step 1 system prompt
│   └── step2-diversity.md       Step 2 system prompt
│
└── config/                      Scoring configuration
    ├── model.json               Pinned model version
    └── round-params.json        Timing windows, quorum rules
```

- **Public repository** under the PostFiat organization (full transparency)
- Branch protection on main (reviews required)
- CI/CD via GitHub Actions
- Foundation triggers scoring rounds (access-controlled)
- Oracle Node operators run the `oracle-node/` component independently

### Scoring Round Lifecycle

```
PHASE 1: DATA SNAPSHOT (~30 minutes)
├── Foundation queries VHS API (attested by Opacity Network)
├── Foundation queries Network Monitoring API (attested by Opacity Network)
├── Foundation queries MaxMind GeoIP2 API (attested by Opacity Network)
├── Foundation queries SumSub API for active validators (attested by Opacity Network)
├── Foundation queries on-chain identity records (no attestation needed — ledger data)
├── Build validator profile JSON for each eligible validator
├── Publish snapshot + all Opacity proofs → IPFS
└── Get snapshot IPFS CID

PHASE 2: ROUND ANNOUNCEMENT
├── Foundation sends on-chain tx:
│   memo = {
│     "type": "pf_scoring_round_v1",
│     "round_number": 42,
│     "snapshot_cid": "<IPFS CID>",
│     "commit_deadline": "<ISO timestamp>",
│     "reveal_deadline": "<ISO timestamp>",
│     "scoring_config_version": "v1.2.3"
│   }
└── Oracle Nodes detect the announcement

PHASE 3: ORACLE NODE SCORING (~6 hours)
├── Each Oracle Node fetches snapshot from IPFS
├── Verifies Opacity proofs on data collection
├── Calls pinned LLM with Step 1 prompt + all validator profiles
│   └── TLSNotary attested by foundation's notary
├── Calls pinned LLM with Step 2 prompt + own Step 1 results + geo data
│   └── TLSNotary attested by foundation's notary
└── Builds presentations (selective disclosure)

PHASE 4: COMMIT (~3 hours)
├── Each Oracle Node computes sha256(scores_json + salt + round_number)
└── Submits commit hash on-chain (memo tx)

PHASE 5: REVEAL (~3 hours)
├── Each Oracle Node publishes to IPFS:
│   scores_json, salt, TLSNotary proofs (Step 1 + Step 2)
└── Submits IPFS CID on-chain (memo tx)

PHASE 6: AGGREGATION (~1 hour)
├── Verify all TLSNotary proofs
├── Verify all reveals match commits
├── Discard invalid submissions
├── Check quorum: valid submissions ≥ max(5, ⅔ of active Oracle Nodes)
│   └── If quorum not met: round fails, current UNL persists
├── Compute median adjusted score per validator
├── Select top 35 (MAX_UNL_VALIDATORS)
├── Build UNL JSON
└── Compute sha512Half of UNL JSON

PHASE 7: PUBLICATION
├── IPFS: publish aggregation report + UNL JSON
├── HTTPS: upload UNL JSON to endpoint
└── On-chain: send tx with hash + IPFS CID + sequence + config version
```

**Total round duration:** ~13 hours. With weekly cadence, this fits comfortably.

### Emergency Operations

**Emergency re-scoring:** Foundation triggers a new scoring round with shortened windows. Same pipeline, same proofs. Community can verify it was a legitimate run.

**Custom UNL override:** Foundation publishes a hardcoded UNL directly, bypassing Oracle Node scoring. No TLSNotary proofs for LLM calls (no LLM was called). An IPFS notice is published explaining the override and the reason. This is a safety net for network continuity, not normal operation.

### Scoring Cadence

- Default: **weekly**
- Foundation can adjust the frequency
- Foundation can trigger emergency rounds at any time
- Every round publishes results regardless of whether the UNL changes

### LLM Model Configuration

- Model is **pinned to a specific version** and published with every scoring round
- Model changes are deliberate, announced in advance, and treated like protocol upgrades
- Changes happen only when justified: significantly better model available, current model deprecated, or cost reasons
- The community can verify which model was used via the TLSNotary proof selective disclosure (the model field is revealed in every Oracle Node's presentation)

### Versioning

Every scoring round publishes the exact version of all components:

| Component | What's Versioned |
|-----------|-----------------|
| Prompt version | System prompts for Step 1 and Step 2 (tracked by hash) |
| Model version | Exact LLM model (e.g., "claude-opus-4-6") |
| Data pipeline version | Code that builds validator profiles |
| Diversity criteria version | Guidelines for Step 2 adjustments |
| Scoring service version | The orchestration code |
| Oracle Node client version | The client code Oracle Nodes run |

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
├── scoring-onboarding              [EXISTS — MODIFY]
│   ├── KYC/KYB + domain verification already implemented
│   └── Add Opacity proof generation for KYC and domain verification
│
├── dynamic-unl-scoring             [NEW REPOSITORY]
│   ├── pipeline/                   Data collection with Opacity proofs
│   ├── oracle-node/                Oracle Node client software
│   ├── aggregator/                 Proof verification + median + UNL publication
│   ├── prompts/                    Version-controlled scoring prompts
│   └── config/                     Model pinning, round parameters
│
├── network-monitoring              [EXISTS — NO CHANGES]
├── infra-monitoring                [EXISTS — NO CHANGES]
├── explorer                        [EXISTS — NO CHANGES (future: show scores)]
├── testnet-bot                     [EXISTS — NO CHANGES]
└── layer-one-agent                 [EXISTS — NO CHANGES]
```

### Cross-Service Data Flow

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                                                                              │
│  ┌──────────┐       ┌─────────────────────┐       ┌──────────────┐          │
│  │   VHS    │  API  │  DATA COLLECTION    │  API  │  Network     │          │
│  │ Postgres │──────▶│  PIPELINE           │◀──────│  Monitoring  │          │
│  └──────────┘       │  (foundation)       │       │  Postgres    │          │
│                     │                     │       └──────────────┘          │
│  ┌──────────┐  API  │                     │  API  ┌──────────────┐          │
│  │ SumSub   │──────▶│                     │◀──────│  MaxMind     │          │
│  │ KYC/KYB  │       │                     │       │  GeoIP2      │          │
│  └──────────┘       └──────────┬──────────┘       └──────────────┘          │
│                                │                                             │
│           All data fetches attested by OPACITY NETWORK (decentralized)      │
│                                │                                             │
│                                ▼                                             │
│                     ┌─────────────────────┐                                  │
│                     │  IPFS               │                                  │
│                     │  (data snapshot +   │                                  │
│                     │   TLSNotary proofs) │                                  │
│                     └──────────┬──────────┘                                  │
│                                │                                             │
│              ┌─────────────────┼─────────────────┐                           │
│              │                 │                  │                           │
│              ▼                 ▼                  ▼                           │
│     ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                    │
│     │ Oracle Node  │  │ Oracle Node  │  │ Oracle Node  │  ...               │
│     │      A       │  │      B       │  │      C       │                    │
│     │              │  │              │  │              │                    │
│     │ Step 1: LLM  │  │ Step 1: LLM  │  │ Step 1: LLM  │                    │
│     │ Step 2: LLM  │  │ Step 2: LLM  │  │ Step 2: LLM  │                    │
│     └──────┬───────┘  └──────┬───────┘  └──────┬───────┘                    │
│            │                 │                  │                             │
│            │    LLM calls attested by FOUNDATION TLSNotary notary           │
│            │                 │                  │                             │
│            ▼                 ▼                  ▼                             │
│     ┌──────────────────────────────────────────────────┐                     │
│     │  COMMIT-REVEAL ON PFT LEDGER                     │                     │
│     │  1. Each Oracle Node commits hash on-chain       │                     │
│     │  2. Each Oracle Node reveals scores + proofs     │                     │
│     └──────────────────────┬───────────────────────────┘                     │
│                            │                                                 │
│                            ▼                                                 │
│              ┌─────────────────────────┐                                     │
│              │  AGGREGATION SERVICE    │                                     │
│              │  (foundation, auditable)│                                     │
│              │                         │                                     │
│              │  Verify proofs          │                                     │
│              │  Check commit-reveal    │                                     │
│              │  Compute median         │                                     │
│              │  Build UNL JSON         │                                     │
│              └────────────┬────────────┘                                     │
│                           │                                                  │
│              ┌────────────┼────────────┐                                     │
│              │            │            │                                     │
│              ▼            ▼            ▼                                     │
│         ┌────────┐  ┌────────┐  ┌──────────┐                                │
│         │ IPFS   │  │ HTTPS  │  │ On-chain │                                │
│         │ (full  │  │ (UNL   │  │ (hash +  │                                │
│         │  audit │  │  JSON) │  │  CID +   │                                │
│         │  trail)│  │        │  │  seq)    │                                │
│         └────────┘  └────────┘  └─────┬────┘                                │
│                                       │                                      │
│                                       ▼                                      │
│                            ┌────────────────────┐                            │
│                            │   ALL NODES         │                            │
│                            │   (postfiatd)       │                            │
│                            │                     │                            │
│                            │   UNLHashWatcher    │                            │
│                            │   DynamicUNLManager │                            │
│                            │   ValidatorSite     │                            │
│                            │   ValidatorList     │                            │
│                            │   Consensus         │                            │
│                            └────────────────────┘                            │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 11. What Needs to Be Built

### New Work

| Component | Description |
|-----------|-------------|
| **Scoring service repository** | Data pipeline with TLSNotary integration, Oracle Node client, aggregation service, commit-reveal protocol, IPFS publication, UNL publication, version-controlled prompts, CI/CD |
| **TLSNotary notary server deployment** | Foundation-operated `notary-server` instances for Oracle Node LLM call attestation |
| **Oracle Node registration system** | On-chain bond mechanism (100k PFT), Task Node address linking, liveness tracking |
| **Commit-reveal protocol** | On-chain memo format for commits and reveals, verification logic |

### Modifications to Existing Code

| Component | Description |
|-----------|-------------|
| **postfiatd** (BuildLedger, Application, ValidatorSite) | Integration of UNLHashWatcher and DynamicUNLManager into existing code paths. UNLHashWatcher and DynamicUNLManager already prototyped on `feature/dynamic-unl` branch. ValidatorSite integration is the main unprototyped piece. |
| **VHS MaxMind upgrade** | Replace GeoLite2 with GeoIP2 paid tier. Configuration + API key change. |
| **scoring-onboarding** | Add Opacity proof generation for KYC (SumSub API verification) and institutional domain verification (DoH query). Update on-chain memo format to include `proof_cid`. |

### External Service Setup

| Service | Description |
|---------|-------------|
| **Opacity Network** | Team account with Opacity Network for data collection attestation (decentralized MPC-TLS notary network). Fallback: public TLSNotary notary server. |
| **IPFS pinning service** | Pinata, web3.storage, or similar for persistent proof and audit trail storage |
| **LLM API access** | Each Oracle Node operator obtains their own API key for the pinned provider |

### No Changes Needed

- network-monitoring, infra-monitoring, explorer, testnet-bot, layer-one-agent
- Existing validator/RPC/archive node infrastructure (just needs updated binary)
- Consensus engine, RPC handlers, transaction processors, overlay, nodestore

---

## 12. Deployment Roadmap

```
STEP 1 — Parallel Start
├── postfiatd: refine UNLHashWatcher + DynamicUNLManager from prototype
├── postfiatd: implement ValidatorSite integration (main unprototyped piece)
├── VHS: MaxMind GeoIP2 upgrade
├── Deploy foundation TLSNotary notary server (test instance)
└── Set up Opacity Network team account

STEP 2 — Data Collection Pipeline
├── Build VHS, Network Monitoring, MaxMind, SumSub data collectors
├── Integrate Opacity SDK for data collection attestation
├── Build snapshot builder and IPFS publisher
├── Test: verify Opacity proofs on all data sources end-to-end
└── Update scoring-onboarding with Opacity proof generation

STEP 3 — Oracle Node Client
├── Build LLM API caller with TLSNotary integration (foundation notary)
├── Build selective disclosure presentation builder
├── Build commit-reveal submission handler
├── Design and implement on-chain memo formats for rounds, commits, reveals
└── Test: single Oracle Node end-to-end (fetch → score → commit → reveal)

STEP 4 — Oracle Node Registration
├── Implement PFT bond mechanism on-chain
├── Implement Task Node address linking
├── Implement liveness tracking
├── Build registration portal or CLI tool
└── Test: registration, scoring participation, deregistration

STEP 5 — Aggregation and Integration Testing
├── Build aggregation service (proof verification, commit-reveal checking, median)
├── Build UNL construction and publication
├── End-to-end integration test on devnet with multiple Oracle Nodes
├── Proof verification end-to-end
├── UNL application test: aggregation → on-chain → node picks up new UNL
└── Failure mode testing (invalid proofs, missed commits, quorum failures)

STEP 6 — Devnet Deployment
├── Deploy updated postfiatd to devnet validators
├── Enable featureDynamicUNL amendment on devnet
├── Deploy data collection pipeline + aggregation service
├── Recruit initial Oracle Node operators (minimum 5)
└── Run first real scoring round with Oracle Nodes

STEP 7 — Testnet Rollout
├── Deploy to testnet
├── Enable amendment (3-day majority time)
├── Expand Oracle Node set
└── Monitor and iterate (round timing, quorum thresholds, prompt refinement)

STEP 8 — Mainnet Preparation
├── UNL size research (determine optimal MAX_UNL_VALIDATORS)
├── Security audit of scoring service, Oracle Node client, and postfiatd changes
├── Prompt refinement based on devnet/testnet experience
├── TLSNotary notary server hardening (redundancy, DDoS protection)
├── Governance mechanism design
└── Oracle Node operator onboarding for mainnet

STEP 9 — Mainnet Launch
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

The prototype proves the node-side mechanics work. What remains is the ValidatorSite integration (hooking hash verification into the actual fetch path) and the entire scoring/publication pipeline with Oracle Nodes.

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

The `score` field contains the median adjusted score across all Oracle Nodes for that validator. Nodes compute `sha512Half` of this JSON and verify it matches the hash published on-chain.

## Appendix C: Key References

| Resource | Location |
|----------|----------|
| Dynamic UNL prototype | `feature/dynamic-unl` branch of postfiatd |
| Prototype design doc | `docs/DynamicUNL.md` on prototype branch |
| Scoring onboarding system | `postfiatorg/scoring-onboarding` repository |
| VHS documentation | `agent-hub/products/blockchain/repos/validator-history-service.md` |
| Network monitoring docs | `agent-hub/products/blockchain/repos/network-monitoring.md` |
| Infrastructure overview | `agent-hub/products/blockchain/INFRASTRUCTURE.md` |
| TLSNotary protocol | https://tlsnotary.org |
| TLSNotary GitHub | https://github.com/tlsnotary/tlsn |
| TLSNotary notary-server | https://github.com/tlsnotary/tlsn/tree/main/crates/notary/server |
| Opacity Network | https://opacity.network |
| Opacity Network docs | https://docs.opacity.network |
| Opacity Labs GitHub | https://github.com/OpacityLabs |
| XRPL consensus analysis | https://arxiv.org/pdf/1802.07242 |

## Appendix D: Oracle Node Registration and Lifecycle

### Registration

1. Operator bonds 100,000 PFT on-chain (locked to their account, cannot be spent while bonded)
2. Operator submits a registration memo transaction linking their Task Node address:
   ```json
   {
     "type": "pf_oracle_register_v1",
     "task_node_address": "rYYY...",
     "notary_endpoint": "<foundation notary URL>"
   }
   ```
3. Foundation verifies the bond and Task Node link
4. Operator completes a test round: fetches a test snapshot, calls the LLM, produces TLSNotary proofs, submits a test commit-reveal
5. On successful test round, the Oracle Node is marked active

### Active Participation

- Oracle Nodes are expected to participate in every scoring round
- Liveness requirement: participate in at least 1 of the last 4 rounds
- Missing more than 3 consecutive rounds → marked inactive
- Inactive nodes are excluded from quorum calculations
- Reactivation: participate in the next round

### Deregistration

1. Operator submits a deregistration memo transaction
2. Bond (100,000 PFT) is unlocked after a cooldown period (e.g., 7 days)
3. Node is removed from the active registry

### Quorum Rules

- Valid submissions required: **max(5, ⅔ of active Oracle Nodes)**
- If quorum is not met, the round fails and the current UNL persists
- The foundation can trigger an emergency round with the same quorum requirements
- If repeated quorum failures occur, the foundation retains the emergency UNL fallback power

## Appendix E: Commit-Reveal Protocol Specification

### Commit Transaction Memo Format

```json
{
  "type": "pf_oracle_commit_v1",
  "round_number": 42,
  "commit_hash": "<sha256_hex(scores_json + salt + round_number)>"
}
```

### Reveal Transaction Memo Format

```json
{
  "type": "pf_oracle_reveal_v1",
  "round_number": 42,
  "reveal_cid": "<IPFS CID of reveal payload>"
}
```

### Reveal Payload (stored on IPFS)

```json
{
  "round_number": 42,
  "oracle_node": "rZZZ...",
  "salt": "<random_hex_string>",
  "scores": {
    "step1": [
      {
        "validator": "ED2677AB...",
        "score": 95,
        "reasoning": "...",
        "flags": []
      }
    ],
    "step2": [
      {
        "validator": "ED2677AB...",
        "raw_score": 95,
        "adjusted_score": 97,
        "adjustment_reasoning": "..."
      }
    ]
  },
  "proofs": {
    "step1_proof_cid": "<IPFS CID of Step 1 TLSNotary presentation>",
    "step2_proof_cid": "<IPFS CID of Step 2 TLSNotary presentation>"
  },
  "metadata": {
    "model": "claude-opus-4-6",
    "prompt_step1_hash": "<sha256>",
    "prompt_step2_hash": "<sha256>",
    "snapshot_cid": "<IPFS CID of data snapshot used>",
    "oracle_client_version": "1.0.0"
  }
}
```

### Verification Steps

1. Fetch reveal payload from IPFS using `reveal_cid`
2. Compute `sha256(scores_json + salt + round_number)` and verify it matches `commit_hash`
3. Verify `snapshot_cid` matches the round announcement's snapshot CID
4. Fetch TLSNotary presentations from IPFS
5. Verify each presentation: Notary attestation valid, API endpoint correct, model matches, prompt hash matches, input data hash matches snapshot
6. Extract LLM responses from presentations and verify they match the claimed scores

## Appendix F: TLSNotary Proof Structure

### What a TLSNotary Presentation Contains

A TLSNotary presentation is the artifact published by a Prover (Oracle Node or foundation) that selectively reveals parts of a TLS session:

- **Notary attestation**: Cryptographic signature from the Notary over the TLS session transcript (proves the session was real)
- **Server identity**: The TLS certificate chain of the server (proves which server was contacted)
- **Revealed request fields**: Selectively disclosed parts of the HTTPS request (e.g., API endpoint, model parameter, prompt)
- **Revealed response fields**: Selectively disclosed parts of the HTTPS response (e.g., LLM output)
- **Redacted fields**: Parts of the request/response that are hidden (e.g., API key in Authorization header)

### Presentation Fields for Oracle Node LLM Calls

| Field | Revealed/Redacted | Purpose |
|-------|-------------------|---------|
| API endpoint URL | Revealed | Proves the correct LLM provider was called |
| HTTP method | Revealed | Confirms POST request |
| `model` parameter | Revealed | Proves the pinned model was used |
| `temperature` parameter | Revealed | Proves temperature 0 was used |
| `system` prompt | Revealed (or hash) | Proves the prescribed prompt was used |
| `messages` content (input data) | Revealed (or hash) | Proves the published snapshot was used |
| `Authorization` header | Redacted | Protects the Oracle Node's API key |
| Response body | Revealed | The actual LLM output (scores + reasoning) |
| Response status code | Revealed | Confirms successful API call (200) |

### Presentation Fields for Foundation Data Collection

| Field | Revealed/Redacted | Purpose |
|-------|-------------------|---------|
| API endpoint URL | Revealed | Proves the correct data source was queried |
| Request parameters | Revealed | Proves what data was requested |
| `Authorization` header | Redacted | Protects API keys (MaxMind, SumSub) |
| Response body | Revealed | The actual data returned |
| Response status code | Revealed | Confirms successful API call |
