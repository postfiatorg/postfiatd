# Post Fiat Validator Setup (Testnet)

**Last verified:** 2026-02-06 | **Build:** 3.0.0 (`testnet-full-latest`)

---

## What you need

- A cloud server running **Ubuntu 24.04** with **at least 8 GB RAM** (16-32 GB recommended)
- An SSH key pair
- An Anthropic API key (for Claude Code)

---

## Part 1: Get a server

### Option A: Hetzner (recommended, ~$25/mo)

1. Go to [console.hetzner.cloud](https://console.hetzner.cloud) and create a project
2. Create a server:
   - Location: any
   - Image: **Ubuntu 24.04**
   - Type: **CPX31** (4 vCPU, 8 GB) or **CCX23** (4 vCPU, 16 GB) for more headroom
   - **Use x86/Intel.** Avoid ARM (CAX) instances unless Intel is unavailable -- ARM requires QEMU emulation and is slower
3. Under SSH keys, click "Add SSH Key" and paste your public key
   - If you don't have one: `ssh-keygen -t ed25519 -C "postfiat"` then copy `~/.ssh/id_ed25519.pub`
4. Create the server and note the IP address

### Option B: Other providers

Any provider works (Vultr, DigitalOcean, AWS, etc). Requirements:
- Ubuntu 24.04 x86_64
- 8+ GB RAM, 80+ GB disk (SSD)
- Open port **2559** (peer protocol) in your firewall

---

## Part 2: Initial server setup

SSH into your server:

```bash
ssh root@<YOUR_IP>
```

Create a non-root user and give it sudo:

```bash
adduser postfiat
usermod -aG sudo postfiat
```

Log out and reconnect as the new user:

```bash
ssh postfiat@<YOUR_IP>
```

---

## Part 3: Install Claude Code

Run these commands one at a time:

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
sudo npm install -g @anthropic-ai/claude-code
```

Grant passwordless sudo (needed for the agent to work unattended):

```bash
sudo bash -c 'echo "postfiat ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/postfiat && chmod 440 /etc/sudoers.d/postfiat'
```

Launch Claude Code in YOLO mode:

```bash
claude --dangerously-skip-permissions
```

It will ask for your Anthropic API key on first launch. Paste it in.

---

## Part 4: Paste this into Claude Code

Once Claude Code is running, copy and paste the entire block below. Claude will execute everything automatically.

```
You are operating in YOLO / danger mode on Ubuntu 24.04.
Goal: full PostFiat testnet validator setup from scratch.

Execute all steps below in order. Do not ask questions. Do not explain. Just run commands.

STEP 1 - INSTALL DEPENDENCIES
sudo apt update && sudo apt install -y git curl python3 docker.io docker-compose-v2
sudo systemctl enable --now docker
sudo usermod -aG docker $USER

STEP 2 - CLONE REPO AND PREPARE DIRECTORIES
mkdir -p ~/repos
git clone -b testnet https://github.com/postfiatorg/postfiatd ~/repos/postfiatd
sudo mkdir -p /opt/postfiatd && sudo chown $USER:$USER /opt/postfiatd

STEP 3 - COPY CONFIG FILES
cp ~/repos/postfiatd/cfg/postfiatd-testnet-light.cfg /opt/postfiatd/postfiatd.cfg
cp ~/repos/postfiatd/cfg/validators-testnet.txt /opt/postfiatd/validators.txt

STEP 4 - ADD LOW-DISK HISTORY SETTING
In /opt/postfiatd/postfiatd.cfg, if [ledger_history] is missing, insert these two lines immediately before [node_db]:

[ledger_history]
256

STEP 5 - GENERATE VALIDATOR KEYS (run ONCE only)
sg docker -c "docker pull agtipft/postfiatd:testnet-full-latest"

sg docker -c "docker run --rm --entrypoint validator-keys -v /opt/postfiatd:/root/.ripple \
  agtipft/postfiatd:testnet-full-latest create_keys"

sg docker -c "docker run --rm --entrypoint validator-keys -v /opt/postfiatd:/root/.ripple \
  agtipft/postfiatd:testnet-full-latest create_token" 2>&1 \
  | sed -n '/^\[validator_token\]$/,$ { /^\[/d; p }' \
  | tr -d '\n ' > /opt/postfiatd/validator-token.single

Verify both files exist and token is non-empty:
ls -la /opt/postfiatd/validator-keys.json
[ -s /opt/postfiatd/validator-token.single ] || echo "ERROR: token file is empty"

STEP 6 - APPEND VALIDATOR TOKEN TO CONFIG
Remove any existing [validator_token] blocks from postfiatd.cfg, then append exactly:

[validator_token]
<CONTENTS OF /opt/postfiatd/validator-token.single>

Read the file contents and paste the actual base64 blob, not the placeholder.

STEP 7 - CREATE DOCKER COMPOSE FILE
Write /opt/postfiatd/docker-compose.yml with this exact content:

services:
  postfiatd:
    image: agtipft/postfiatd:testnet-full-latest
    container_name: postfiatd
    restart: unless-stopped
    ports:
      - "2559:2559"
      - "5005:5005"
      - "6005:6005"
      - "6006:6006"
      - "50051:50051"
    volumes:
      - ./postfiatd.cfg:/etc/postfiatd/postfiatd.cfg:ro
      - ./validators.txt:/etc/postfiatd/validators.txt:ro
      - postfiatd-data:/var/lib/postfiatd
      - postfiatd-logs:/var/log/postfiatd

volumes:
  postfiatd-data:
  postfiatd-logs:

CRITICAL: Both postfiatd.cfg and validators.txt MUST be bind-mounted. The image's built-in validators.txt has an outdated VL publisher key and will prevent syncing.

STEP 8 - START VALIDATOR
cd /opt/postfiatd
sg docker -c "docker compose pull"
sg docker -c "docker compose up -d --force-recreate"

STEP 9 - VERIFY
Wait 60 seconds, then run:

curl -s -X POST http://localhost:5005/ \
  -H "Content-Type: application/json" \
  -d '{"method":"server_info","params":[{}]}'

Check that:
- build_version is 3.0.0
- server_state is connected, syncing, tracking, or full
- peers is > 0
- validation_quorum is a small number (like 4 or 5), NOT 4294967295

If validation_quorum shows 4294967295 or the log shows "expired validator list", the validators.txt is wrong. Verify it is bind-mounted and contains the key ED3F1E0DA736FCF99BE2880A60DBD470715C0E04DD793FB862236B070571FC09E2.

STEP 10 - PRINT STATUS
sg docker -c "docker compose -f /opt/postfiatd/docker-compose.yml ps"

Print the validator's public key from validator-keys.json so the user can share it with the network operators.
```

---

## Part 5: Verify it worked

After Claude finishes, your validator should reach `server_state: "full"` within a few minutes. You can monitor progress:

```bash
# Watch sync status
watch -n 5 'curl -s -X POST http://localhost:5005/ \
  -H "Content-Type: application/json" \
  -d "{\"method\":\"server_info\",\"params\":[{}]}" \
  | python3 -m json.tool | grep -E "server_state|complete_ledgers|peers|validation_quorum"'

# Check validator identity
cat /opt/postfiatd/validator-keys.json

# View logs
docker compose -f /opt/postfiatd/docker-compose.yml logs -f postfiatd
```

**Share your validator public key** (from `validator-keys.json`) with the Post Fiat network operators so they can add you to the UNL.

---

## Reinstalling / Starting Fresh

If you need to wipe and reinstall (e.g. broken state, switching images, corrupted DB):

```
You are operating in YOLO / danger mode on Ubuntu 24.04.
Goal: clean reinstall of PostFiat testnet validator. Preserve validator keys.

Execute all steps. Do not ask questions.

STEP 1 - STOP AND REMOVE CONTAINERS
cd /opt/postfiatd
sg docker -c "docker compose down"

STEP 2 - BACK UP KEYS (critical -- do not lose these)
cp /opt/postfiatd/validator-keys.json ~/validator-keys.json.backup
cp /opt/postfiatd/validator-token.single ~/validator-token.single.backup
echo "Keys backed up to home directory"

STEP 3 - WIPE DATA VOLUMES (removes ledger DB, forces full resync)
sg docker -c "docker volume rm postfiatd_postfiatd-data postfiatd_postfiatd-logs" || true

STEP 4 - UPDATE CONFIG FILES FROM REPO
cd ~/repos/postfiatd && git pull
cp ~/repos/postfiatd/cfg/postfiatd-testnet-light.cfg /opt/postfiatd/postfiatd.cfg
cp ~/repos/postfiatd/cfg/validators-testnet.txt /opt/postfiatd/validators.txt

STEP 5 - RE-ADD LOW-DISK HISTORY
In /opt/postfiatd/postfiatd.cfg, insert before [node_db] if missing:

[ledger_history]
256

STEP 6 - RE-ADD VALIDATOR TOKEN
Restore the backed-up token. Read ~/validator-token.single.backup and append to postfiatd.cfg:

[validator_token]
<CONTENTS OF ~/validator-token.single.backup>

STEP 7 - ENSURE DOCKER COMPOSE IS CORRECT
Verify /opt/postfiatd/docker-compose.yml bind-mounts both files:
  - ./postfiatd.cfg:/etc/postfiatd/postfiatd.cfg:ro
  - ./validators.txt:/etc/postfiatd/validators.txt:ro
And uses image: agtipft/postfiatd:testnet-full-latest

STEP 8 - PULL AND START
cd /opt/postfiatd
sg docker -c "docker compose pull"
sg docker -c "docker compose up -d --force-recreate"

STEP 9 - VERIFY
Wait 60 seconds then check server_info. Confirm build 3.0.0, server_state progressing toward full, validation_quorum is small (not 4294967295).
```

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `validation_quorum: 4294967295` | Expired/wrong validator list | Bind-mount correct `validators.txt` from repo |
| `complete_ledgers: "empty"` for 5+ min | Usually the VL issue above | Check logs for "expired validator list" |
| `peers: 0` | Firewall blocking port 2559 | Open port 2559 TCP inbound |
| Container keeps restarting | Bad config syntax | Check `docker compose logs postfiatd` |
| `server_state` stuck on `connected` | VL issue or not enough peers | Verify validators.txt key, check peer IPs in config |
| Old build version (2.x) | Using `testnet-latest` tag | Switch to `testnet-full-latest` |

### Useful commands

```bash
# Container status
docker compose -f /opt/postfiatd/docker-compose.yml ps

# Live logs
docker compose -f /opt/postfiatd/docker-compose.yml logs -f postfiatd

# Server info (quick check)
curl -s -X POST http://localhost:5005/ \
  -H "Content-Type: application/json" \
  -d '{"method":"server_info","params":[{}]}' | python3 -m json.tool

# Validator info (from inside container)
docker exec postfiatd curl -s -X POST http://127.0.0.1:5005/ \
  -H "Content-Type: application/json" \
  -d '{"method":"validator_info","params":[{}]}' | python3 -m json.tool

# Restart without wiping data
docker compose -f /opt/postfiatd/docker-compose.yml restart

# Full recreate (keeps data volumes)
docker compose -f /opt/postfiatd/docker-compose.yml up -d --force-recreate
```

---

## Known gotchas (as of 2026-02-06)

1. **Image tag:** `testnet-latest` is stale at build 2.5.1. Use `testnet-full-latest` for 3.0.0.
2. **Validator list key mismatch:** The container's built-in `validators.txt` has an old VL publisher key (`ED6B26...`). The live VL at `postfiat.org/testnet_vl.json` is signed with `ED3F1E0DA736FCF99BE2880A60DBD470715C0E04DD793FB862236B070571FC09E2`. You **must** bind-mount the repo's `validators-testnet.txt` or the node will never sync.
3. **Docker compose file in repo** (`scripts/docker-compose-validator.yml`) uses a named volume for config (`postfiatd-config:/etc/postfiatd`). This needs to be replaced with bind mounts for both `postfiatd.cfg` and `validators.txt`.
4. **`sg docker` wrapper:** After `usermod -aG docker $USER`, new group membership requires a new login. For agents that can't relog, wrap docker commands with `sg docker -c "..."`.
5. **ARM hosts:** If you must use ARM (Hetzner CAX), install `qemu-user-static binfmt-support` and add `--platform=linux/amd64` to all docker run/pull commands. Expect slower performance.
