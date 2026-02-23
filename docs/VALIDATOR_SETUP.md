# Post Fiat Validator Setup (Testnet)

**Verified:** 2026-02-06 | **Build:** 3.0.0 (`testnet-full-latest`)

## Requirements

- Ubuntu 24.04 x86_64, 8+ GB RAM, 80+ GB SSD, port 2559 open
- Anthropic API key for Claude Code
- Recommended: Hetzner CPX31 or CCX23 (~$25/mo). Avoid ARM (CAX). Any provider works.

## Part 1: Server and user setup

SSH in as root, create a user, copy your SSH key to them, then relog:

```bash
ssh root@<IP>
adduser postfiat && usermod -aG sudo postfiat
cp -r ~/.ssh /home/postfiat/.ssh && chown -R postfiat:postfiat /home/postfiat/.ssh
# log out, then:
ssh postfiat@<IP>
```

## Part 2: Install Claude Code

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
sudo npm install -g @anthropic-ai/claude-code
sudo bash -c 'echo "postfiat ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/postfiat && chmod 440 /etc/sudoers.d/postfiat'
claude --dangerously-skip-permissions
```

Paste your Anthropic API key when prompted.

## Part 3: Paste into Claude Code

Copy this entire block and paste it. Claude does the rest.

```
You are in YOLO mode on Ubuntu 24.04. Do a full PostFiat testnet validator setup. No questions. No explanations. Just run commands.

1. sudo apt update && sudo apt install -y git curl python3 docker.io docker-compose-v2
   sudo systemctl enable --now docker && sudo usermod -aG docker $USER
   If ufw is active, run: sudo ufw allow 2559/tcp

2. mkdir -p ~/repos
   git clone -b testnet https://github.com/postfiatorg/postfiatd ~/repos/postfiatd
   sudo mkdir -p /opt/postfiatd && sudo chown $USER:$USER /opt/postfiatd

3. cp ~/repos/postfiatd/cfg/postfiatd-testnet-light.cfg /opt/postfiatd/postfiatd.cfg
   cp ~/repos/postfiatd/cfg/validators-testnet.txt /opt/postfiatd/validators.txt

4. In /opt/postfiatd/postfiatd.cfg, insert before [node_db] if missing:
   [ledger_history]
   256

5. sg docker -c "docker pull agtipft/postfiatd:testnet-full-latest"
   sg docker -c "docker run --rm --entrypoint validator-keys -v /opt/postfiatd:/root/.ripple agtipft/postfiatd:testnet-full-latest create_keys"
   sg docker -c "docker run --rm --entrypoint validator-keys -v /opt/postfiatd:/root/.ripple agtipft/postfiatd:testnet-full-latest create_token" 2>&1 | sed -n '/^\[validator_token\]$/,$ { /^\[/d; p }' | tr -d '\n ' > /opt/postfiatd/validator-token.single
   Verify: ls -la /opt/postfiatd/validator-keys.json && [ -s /opt/postfiatd/validator-token.single ] || echo "ERROR: empty token"

6. Remove any existing [validator_token] from postfiatd.cfg. Read /opt/postfiatd/validator-token.single and append:
   [validator_token]
   <actual contents of validator-token.single>

7. Write /opt/postfiatd/docker-compose.yml:
   services:
     postfiatd:
       image: agtipft/postfiatd:testnet-full-latest
       container_name: postfiatd
       restart: unless-stopped
       ports: ["2559:2559","5005:5005","6005:6005","6006:6006","50051:50051"]
       volumes:
         - ./postfiatd.cfg:/etc/postfiatd/postfiatd.cfg:ro
         - ./validators.txt:/etc/postfiatd/validators.txt:ro
         - postfiatd-data:/var/lib/postfiatd
         - postfiatd-logs:/var/log/postfiatd
   volumes:
     postfiatd-data:
     postfiatd-logs:
   CRITICAL: Both cfg and validators.txt MUST be bind-mounted. Built-in validators.txt has a stale VL key.

8. cd /opt/postfiatd && sg docker -c "docker compose pull" && sg docker -c "docker compose up -d --force-recreate"

9. Wait 60s. Run server_info. Confirm build 3.0.0, peers > 0, validation_quorum is small (NOT 4294967295). If quorum is wrong, validators.txt bind-mount failed.

10. Print docker compose ps and the public key from validator-keys.json.
```

## Part 4: Verify

After Claude finishes, monitor sync (should reach `full` in a few minutes):

```bash
watch -n 5 'curl -s -X POST http://localhost:5005/ -H "Content-Type: application/json" \
  -d "{\"method\":\"server_info\",\"params\":[{}]}" | python3 -m json.tool \
  | grep -E "server_state|complete_ledgers|peers"'
```

Share your **validator public key** (from `cat /opt/postfiatd/validator-keys.json`) with network operators to get added to the UNL.

## Reinstall (preserves keys)

Paste into Claude Code if you need to wipe and start over:

```
YOLO mode. Clean reinstall PostFiat testnet validator. Preserve keys. No questions.

1. cd /opt/postfiatd && sg docker -c "docker compose down"
2. cp /opt/postfiatd/validator-keys.json ~/validator-keys.json.backup
   cp /opt/postfiatd/validator-token.single ~/validator-token.single.backup
3. sg docker -c "docker volume rm postfiatd_postfiatd-data postfiatd_postfiatd-logs" || true
4. cd ~/repos/postfiatd && git pull
   cp cfg/postfiatd-testnet-light.cfg /opt/postfiatd/postfiatd.cfg
   cp cfg/validators-testnet.txt /opt/postfiatd/validators.txt
5. Insert [ledger_history] 256 before [node_db] in postfiatd.cfg if missing
6. Read ~/validator-token.single.backup, append [validator_token] block to postfiatd.cfg
7. Verify docker-compose.yml bind-mounts both .cfg and validators.txt, uses testnet-full-latest
8. cd /opt/postfiatd && sg docker -c "docker compose pull && docker compose up -d --force-recreate"
9. Wait 60s, verify server_info shows 3.0.0 and quorum is healthy
```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `validation_quorum: 4294967295` | validators.txt has wrong VL key -- bind-mount repo's `validators-testnet.txt` |
| `complete_ledgers: "empty"` 5+ min | Same as above, check logs for "expired validator list" |
| `peers: 0` | Open port 2559 TCP in firewall |
| Build shows 2.x | Use image `testnet-full-latest`, not `testnet-latest` |
| Container restarting | `docker compose logs postfiatd` -- likely bad config syntax |

**Useful:** `docker compose -f /opt/postfiatd/docker-compose.yml logs -f postfiatd`
