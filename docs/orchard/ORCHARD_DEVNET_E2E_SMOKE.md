# Orchard Devnet E2E Smoke Runbook

This runbook documents the live devnet `ShieldedPayment` smoke flow that was used successfully on **2026-02-26**.

It is designed for the current devnet topology where:

- Orchard is enabled via `OrchardPrivacy`.
- Public RPC accepts `submit` with `tx_blob`.
- Public RPC does **not** accept server-side signing (`sign` or `submit` with `secret`).

## Canonical Location

The canonical location for this procedure is `postfiatd/docs/orchard/` because it depends on Orchard RPC behavior (`orchard_prepare_payment`, bundle semantics, tx field requirements) rather than environment-specific workflow YAML details.

Use `agent-hub/milestones/halo2/execution-tracker.md` only for execution evidence (hashes, run links, timestamps), not as the primary runbook.

## Verified Example

- Network: `devnet` (`NetworkID=2024`)
- RPC: `https://rpc.devnet.postfiat.org`
- Successful tx hash: `19FB8C256D5F462486C531AD94E79AB0C0E982BE8CAA4E18FA1080F3977D9467`
- Result: `tesSUCCESS`
- Validated ledger: `3952`

## Why This Flow Is Needed

### 1) Public RPC signing is disabled

On current devnet RPC, `sign` and `submit` with `secret` return `notSupported`.  
Only pre-signed submission (`submit` + `tx_blob`) is accepted.

### 2) `orchard_prepare_payment` returns an unsigned transaction payload

For `t_to_z`, returned `tx_json` includes Orchard payload but does not include all final tx envelope fields needed for signing/submission.  
You must set required common fields before signing.

### 3) `postfiatd sign` is a client command

`postfiatd sign ... offline` still uses the node's RPC client path.  
You need a running `postfiatd` process (local container is fine) and must provide a valid config with `--conf`.

## Preconditions

- Docker available locally.
- Python 3 available locally.
- Outbound HTTPS access to `https://rpc.devnet.postfiat.org`.
- A seed authorized for the source account.
- Orchard amendment enabled on devnet.

## Safety Controls

- Use small amounts for smoke (example: `25 XRP` = `25000000` drops).
- Pin `NetworkID` from live `server_info` (do not hardcode blindly).
- Set bounded `LastLedgerSequence` (example: current validated ledger + 20).
- Destroy temporary local signer container after completion.

## End-to-End Procedure

### 1) Export inputs

```bash
export DEVNET_RPC_URL="https://rpc.devnet.postfiat.org"
export DEVNET_SIGN_SEED="<seed>"
```

### 2) Run smoke script

This script:

1. Validates Orchard amendment status.
2. Builds a `t_to_z` tx with `orchard_prepare_payment`.
3. Adds `Sequence`, `Fee`, `NetworkID`, and `LastLedgerSequence`.
4. Signs offline via a temporary local `postfiatd` container.
5. Submits `tx_blob` to devnet RPC.
6. Polls until validated and prints the hash/result.
7. Cleans up signer container.

```bash
python3 - <<'PY'
import json
import os
import subprocess
import time
import urllib.request

RPC = os.environ["DEVNET_RPC_URL"]
SEED = os.environ["DEVNET_SIGN_SEED"]
SIGN_CONTAINER = "postfiatd-local-sign"

SOURCE_ACCOUNT = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"
RECIPIENT_ORCHARD = "62B565D0A77917E0CBE360A30D081259A35DB3186AC533278632BFA6003E09576EC955B72EC71FDA33E39D"
AMOUNT_DROPS = "25000000"  # 25 XRP


def rpc(method, params, timeout=90):
    payload = json.dumps({"method": method, "params": params}).encode()
    req = urllib.request.Request(
        RPC, data=payload, headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def sh(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def ensure_signer_container():
    if sh(["docker", "ps", "-a", "--format", "{{.Names}}"]).stdout.splitlines().count(
        SIGN_CONTAINER
    ):
        sh(["docker", "rm", "-f", SIGN_CONTAINER])
    started = sh(
        ["docker", "run", "-d", "--name", SIGN_CONTAINER, "agtipft/postfiatd:devnet-light-latest"]
    )
    if started.returncode != 0:
        raise RuntimeError(f"failed to start signer container: {started.stderr}")
    time.sleep(4)


def cleanup_signer_container():
    sh(["docker", "rm", "-f", SIGN_CONTAINER])


try:
    server_info = rpc("server_info", [{}], timeout=30)
    info = server_info["result"]["info"]
    network_id = info["network_id"]
    validated_ledger = int(info["validated_ledger"]["seq"])

    feature = rpc("feature", [{"feature": "OrchardPrivacy"}], timeout=30)["result"]
    # feature response is keyed by amendment hash; find OrchardPrivacy object.
    orchard = None
    for _, value in feature.items():
        if isinstance(value, dict) and value.get("name") == "OrchardPrivacy":
            orchard = value
            break
    if not orchard or orchard.get("enabled") is not True:
        raise RuntimeError("OrchardPrivacy is not enabled on target network")

    acct = rpc("account_info", [{"account": SOURCE_ACCOUNT, "ledger_index": "validated"}], timeout=30)
    sequence = acct["result"]["account_data"]["Sequence"]

    prep = rpc(
        "orchard_prepare_payment",
        [
            {
                "payment_type": "t_to_z",
                "source_account": SOURCE_ACCOUNT,
                "recipient": RECIPIENT_ORCHARD,
                "amount": AMOUNT_DROPS,
            }
        ],
        timeout=180,
    )["result"]
    if prep.get("status") != "success":
        raise RuntimeError(f"prepare failed: {json.dumps(prep)}")

    tx_json = prep["tx_json"]
    tx_json["Sequence"] = sequence
    tx_json["Fee"] = "10"
    tx_json["NetworkID"] = network_id
    tx_json["LastLedgerSequence"] = validated_ledger + 20

    ensure_signer_container()

    sign_cmd = [
        "docker",
        "exec",
        SIGN_CONTAINER,
        "postfiatd",
        "--conf",
        "/etc/postfiatd/postfiatd.cfg",
        "sign",
        SEED,
        json.dumps(tx_json, separators=(",", ":")),
        "offline",
    ]
    signed = sh(sign_cmd)
    if signed.returncode != 0:
        raise RuntimeError(f"sign failed: {signed.stdout} {signed.stderr}")

    # Some builds print preamble lines before JSON; parse from first "{"
    body = signed.stdout
    start = body.find("{")
    if start > 0:
        body = body[start:]
    sign_res = json.loads(body)["result"]
    tx_blob = sign_res["tx_blob"]
    tx_hash = sign_res["tx_json"]["hash"]

    submit = rpc("submit", [{"tx_blob": tx_blob}], timeout=120)["result"]
    print("SUBMIT", json.dumps(
        {
            "engine_result": submit.get("engine_result"),
            "accepted": submit.get("accepted"),
            "hash": submit.get("tx_json", {}).get("hash", tx_hash),
        }
    ))

    for _ in range(45):
        time.sleep(2)
        tx = rpc("tx", [{"transaction": tx_hash}], timeout=30)["result"]
        if tx.get("validated") is True:
            final = {
                "hash": tx_hash,
                "validated": True,
                "ledger_index": tx.get("ledger_index"),
                "TransactionResult": tx.get("meta", {}).get("TransactionResult"),
                "TransactionType": tx.get("TransactionType"),
                "Amount": tx.get("Amount"),
                "NetworkID": tx.get("NetworkID"),
            }
            print("FINAL", json.dumps(final))
            break
    else:
        print("FINAL", json.dumps({"hash": tx_hash, "validated": False}))
finally:
    cleanup_signer_container()
PY
```

### 3) Success criteria

- `SUBMIT.engine_result == "tesSUCCESS"`
- `FINAL.validated == true`
- `FINAL.TransactionResult == "tesSUCCESS"`
- `FINAL.TransactionType == "ShieldedPayment"`

## How the Flow Works Internally

1. `orchard_prepare_payment` constructs a Halo2-backed Orchard bundle and returns transaction JSON.
2. The client fills ledger-dependent common fields:
   - `Sequence`: current account sequence.
   - `Fee`: fee to pay (in drops).
   - `NetworkID`: must match devnet.
   - `LastLedgerSequence`: bounded validity window.
3. `postfiatd sign ... offline` locally signs and serializes the transaction into `tx_blob`.
4. Devnet RPC validates and applies the signed blob through consensus.
5. `tx` confirms validated metadata and final `TransactionResult`.

## Common Failure Modes

### `error: notSupported` for `sign` or `submit` with `secret`

Expected on public devnet RPC. Use local offline signing and submit `tx_blob`.

### `error_what: no response from server` when signing

Cause: running `postfiatd sign` without a running node config.  
Fix: run signer against a live local node and include:

```bash
postfiatd --conf /etc/postfiatd/postfiatd.cfg sign ...
```

### `temDISABLED`

Cause: `OrchardPrivacy` not enabled on target network.  
Fix: verify amendment state with `feature` before running smoke.

### `tefPAST_SEQ`

Cause: stale `Sequence`.  
Fix: fetch fresh `account_info` immediately before preparing/signing.

### `invalidParams: Insufficient wallet balance` during `z_to_z`

For historical notes, wallet scan horizon may not include older ledgers in current implementation.  
For reliable smoke, prefer fresh `t_to_z` then `submit`.

## Post-Run Evidence to Record

Record in `agent-hub/milestones/halo2/execution-tracker.md`:

- UTC timestamp of run.
- RPC endpoint used.
- tx hash.
- ledger index.
- result code (`tesSUCCESS` expected).
- any deviations from this runbook.
