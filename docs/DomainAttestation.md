# Domain Attestation Guide

How to attest a domain for a single running Post Fiat validator when you have the master keys stored locally.

## Prerequisites

- A running postfiatd validator (in `proposing` or `full` state)
- SSH access to the validator server
- The validator's master key file (`validator-keys.json`) stored locally
- The key file must already have the domain set (check for `"domain": "yourdomain.com"` in the JSON)
- Access to publish files at `https://yourdomain.com/.well-known/pft-ledger.toml`

## Verify Before

Check the validator's current domain status. Use the `validator_info` RPC method (`server_info` does not expose the domain field):

```bash
ssh root@<VALIDATOR_IP> "docker exec postfiatd curl -s http://localhost:5005/ -X POST \
  -H 'Content-Type: application/json' \
  -d '{\"method\":\"validator_info\"}'" | python3 -c "
import sys,json
d = json.load(sys.stdin)['result']
print('state:', 'running')
print('domain:', d.get('domain', 'NOT SET'))
print('seq:', d.get('seq', '?'))
print('master_key:', d.get('master_key', '?'))
"
```

If domain shows `NOT SET`, proceed with attestation.

## If Domain Is Not Set in the Key File

If your `validator-keys.json` does not contain a `"domain"` field, you need to set it first. Copy the key file to the validator, run `set_domain`, then copy it back:

```bash
# Copy keys to server
scp validator-keys.json root@<VALIDATOR_IP>:/tmp/validator-keys.json

# Copy into container, set domain, copy back out
ssh root@<VALIDATOR_IP> "
  docker exec postfiatd mkdir -p /root/.ripple
  docker cp /tmp/validator-keys.json postfiatd:/root/.ripple/validator-keys.json
  docker exec postfiatd validator-keys set_domain <YOUR_DOMAIN>
  docker cp postfiatd:/root/.ripple/validator-keys.json /tmp/validator-keys.json
"

# Copy updated keys back to your local machine
scp root@<VALIDATOR_IP>:/tmp/validator-keys.json validator-keys.json

# Clean up keys from server
ssh root@<VALIDATOR_IP> "
  docker exec postfiatd rm -rf /root/.ripple
  rm /tmp/validator-keys.json
"
```

Save the updated `validator-keys.json` securely. It now contains the domain claim.

Note: `set_domain` also outputs a new validator token. You can use that token directly and skip the `attest_domain` + `create_token` steps below. The token from `set_domain` already has the domain baked into its manifest.

## Attestation Steps

### 1. Generate attestation and token

Copy the master key file to the validator, generate the attestation string and a new token, then remove the keys immediately. The `/root/.ripple` directory does not exist in the container by default and must be created first:

```bash
# Copy keys to server
scp validator-keys.json root@<VALIDATOR_IP>:/tmp/validator-keys.json

# Run attestation and token generation
ssh root@<VALIDATOR_IP> "
  docker exec postfiatd mkdir -p /root/.ripple
  docker cp /tmp/validator-keys.json postfiatd:/root/.ripple/validator-keys.json
  echo '=== ATTESTATION ==='
  docker exec postfiatd validator-keys attest_domain
  echo '=== TOKEN ==='
  docker exec postfiatd validator-keys create_token
  echo '=== CLEANUP ==='
  docker exec postfiatd rm -rf /root/.ripple
  rm /tmp/validator-keys.json
  echo 'Keys removed'
"
```

Save both outputs:
- The **attestation** string (long hex value) for the `pft-ledger.toml` file
- The **token** (multi-line base64 block under `[validator_token]`) for the node config

### 2. Inject the new token

The token is a multi-line base64 string. Do not try to pass it inline via shell string interpolation because line breaks get corrupted. Write it to a file on the server first, then inject it into the config:

```bash
# Write the multi-line token to a file on the server
# Paste the exact base64 lines between the heredoc markers
ssh root@<VALIDATOR_IP> 'cat > /tmp/new-token.txt << '"'"'TOKENEOF'"'"'
<PASTE THE MULTI-LINE BASE64 TOKEN HERE>
TOKENEOF'

# Replace the old token in the config and restart
ssh root@<VALIDATOR_IP> '
  docker cp postfiatd:/etc/postfiatd/postfiatd.cfg /tmp/postfiatd.cfg
  sed -i "/^\[validator_token\]/,/^$/d" /tmp/postfiatd.cfg
  printf "\n[validator_token]\n" >> /tmp/postfiatd.cfg
  cat /tmp/new-token.txt >> /tmp/postfiatd.cfg
  printf "\n" >> /tmp/postfiatd.cfg
  docker cp /tmp/postfiatd.cfg postfiatd:/etc/postfiatd/postfiatd.cfg
  rm /tmp/new-token.txt /tmp/postfiatd.cfg
  docker restart postfiatd
  echo "Token updated, container restarted"
'
```

### 3. Wait for the validator to rejoin consensus

After restart, the validator needs 30-90 seconds to sync and start proposing again:

```bash
ssh root@<VALIDATOR_IP> "docker exec postfiatd curl -s http://localhost:5005/ -X POST \
  -H 'Content-Type: application/json' \
  -d '{\"method\":\"server_info\"}'" | python3 -c "
import sys,json
info = json.load(sys.stdin)['result']['info']
print('state:', info.get('server_state'))
"
```

Wait until it shows `proposing` (validator) or `full` (RPC node).

## Verify After

Run the same `validator_info` check from the beginning. The domain should now show your domain and the sequence number should have incremented:

```bash
ssh root@<VALIDATOR_IP> "docker exec postfiatd curl -s http://localhost:5005/ -X POST \
  -H 'Content-Type: application/json' \
  -d '{\"method\":\"validator_info\"}'" | python3 -c "
import sys,json
d = json.load(sys.stdin)['result']
print('domain:', d.get('domain', 'NOT SET'))
print('seq:', d.get('seq', '?'))
"
```

Expected output after attestation:
```
domain: yourdomain.com
seq: 2
```

If domain still shows `NOT SET`, check the container logs for token errors: `docker logs postfiatd 2>&1 | grep -i 'invalid\|token\|fatal'`. The most common cause is a corrupted token from inline shell injection rather than the file-based approach above.

## Publish the TOML File

Add the validator's attestation to `https://yourdomain.com/.well-known/pft-ledger.toml`:

```toml
[[VALIDATORS]]
public_key = "<VALIDATOR_PUBLIC_KEY>"
attestation = "<ATTESTATION_STRING>"
```

If multiple validators share the same domain, each gets its own `[[VALIDATORS]]` entry in the same file. The public key is the validator's master public key (starts with `nH...`), and the attestation is the hex string from step 1.

## Rolling Restart Safety

When attesting multiple validators that share a UNL, do them one at a time. PFT Ledger consensus requires 80% UNL agreement. In a 4-validator UNL, restarting 2 simultaneously drops you to 50% and consensus stalls.

1. Complete all steps above for one validator
2. Verify it reaches `proposing` state
3. Only then move to the next validator

## Key Safety

- Never leave `validator-keys.json` on the validator server after attestation
- The master key file should only exist on the server for the seconds it takes to run the commands
- Store master keys in a secure offline location
- If master keys are compromised, use `validator-keys revoke_keys` immediately
