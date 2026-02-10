# Deployment Workflows

This document covers the three GitHub Actions workflows used to manage PostFiat network infrastructure: **Deploy**, **Update**, and **Destroy**. All three are manual workflows triggered from the GitHub Actions UI.

## Prerequisites

Before running any workflow, ensure:

1. You have push access to the repository (required to trigger `workflow_dispatch`)
2. The target network's Docker images have been built (see [Build Workflows](#build-workflows))
3. GitHub repository variables and secrets are configured (see [Configuration](#configuration))

## Workflow Overview

| Workflow | File | Purpose | Risk Level |
|----------|------|---------|------------|
| Deploy   | `deploy.yml` | Provision nodes from scratch | Medium — overwrites existing config |
| Update   | `update.yml` | Pull latest image and restart containers | Low — preserves data and config |
| Destroy  | `destroy.yml` | Wipe all node data and containers | **High — irreversible data loss** |

## How to Run a Workflow

1. Go to the repository on GitHub
2. Click **Actions** in the top navigation
3. Select the workflow from the left sidebar (**Deploy Nodes**, **Update Nodes**, or **Destroy Node Data**)
4. Click **Run workflow**
5. Select the inputs (network, node type, etc.)
6. Click the green **Run workflow** button
7. Monitor the run by clicking into it — each host runs as a separate matrix job

## Deploy Nodes (`deploy.yml`)

Performs a full provisioning of PostFiat nodes. This is the most complex workflow and should be used when setting up nodes for the first time or when a full reprovisioning is needed.

### Inputs

| Input | Options | Description |
|-------|---------|-------------|
| `network` | `devnet`, `testnet` | Target network |
| `node_type` | `validators`, `rpc`, `archive`, `all` | Which node type(s) to deploy |
| `reset_vhs` | `true` (default), `false` | Reset Validator History Service after validator deployment |

### What It Does

**For validators:**
- Configures UFW firewall (SSH + peer protocol port 2559 only)
- Writes `docker-compose.yml` with the `light` image variant
- Writes Promtail config for log shipping to Loki
- Starts containers
- Injects the validator token into `postfiatd.cfg` based on the host IP
- Restarts postfiatd and runs a health check

**For RPC nodes:**
- Configures UFW firewall (SSH, HTTP/HTTPS, peer protocol, VHS admin access)
- Configures iptables rate limiting for DDoS protection on port 443
- Installs and configures Caddy as a TLS-terminating reverse proxy
- Writes `docker-compose.yml` with the `medium` image variant
- Starts containers
- Reconfigures WebSocket to use `ws://` internally (Caddy handles TLS)
- Sets WebSocket connection limit to 100
- Sets peer protocol `ip_limit` to 5
- Adds VHS host to the admin whitelist
- Restarts postfiatd and runs health checks for both postfiatd and Caddy

**For archive nodes:**
- Same as RPC nodes, but uses the `full` image variant
- WebSocket connection limit is 25 instead of 100
- Caddy domains use `archive.{network}.postfiat.org` and `ws-archive.{network}.postfiat.org`

**VHS reset (optional):**
- Runs after all deployments complete
- Pulls and executes the reset script from the `validator-history-service` repo
- Only triggers when deploying validators and `reset_vhs` is true

### Execution Order

When `node_type` is `all`, jobs run in this order:

```
prepare → deploy-validator → deploy-rpc    (waits for validators)
                            → deploy-archive (waits for validators)
                                            → reset-vhs (waits for all)
```

Validators deploy sequentially (`max-parallel: 1`) to maintain network quorum during rolling restarts. RPC and archive deployments run in parallel, both after validators finish.

Each deploy wipes the `postfiatd-config` volume to guarantee a clean config from the Docker image. The validator token is then injected once. This prevents config drift across repeated deploys.

### When to Use Deploy

- **First-time setup** of nodes on new hosts
- **After a destroy** to reprovision nodes
- **Infrastructure changes** that require a full reconfiguration (firewall rules, Caddy config, Docker Compose structure)
- **Adding new validator tokens** or changing token-to-host mappings
- **Network topology changes** like adding a new VHS host to the admin whitelist

### When NOT to Use Deploy

- You just need to roll out a new postfiatd version — use [Update](#update-nodes-updateyml) instead
- The node is running fine and you only want to change a runtime config value — SSH in and edit manually, or update the workflow and redeploy

## Update Nodes (`update.yml`)

Lightweight workflow that pulls the latest Docker image and restarts containers. This is the go-to workflow for rolling out new postfiatd builds.

### Inputs

| Input | Options | Description |
|-------|---------|-------------|
| `network` | `devnet`, `testnet` | Target network |
| `node_type` | `validators`, `rpc`, `all` | Which node type(s) to update |

### What It Does

On each target host:
1. `cd /opt/postfiatd`
2. `docker compose pull` — pulls the latest image tag
3. `docker compose up -d` — recreates containers with the new image
4. Waits 30 seconds, then runs a health check

Validators update sequentially (`max-parallel: 1`) to maintain network quorum.

### What It Preserves

- All Docker volumes (config, database, Promtail positions)
- The `docker-compose.yml`, `.env`, and `promtail-config.yml` on the host
- Caddy configuration and service state (not touched at all)
- Firewall rules (not touched at all)
- Any manual config changes made inside the container's config volume

### When to Use Update

- **After a build workflow completes** and you want nodes to pick up the new image
- **Routine version bumps** — this is the standard deployment path
- **Quick recovery** if a container stopped unexpectedly

### When NOT to Use Update

- Nodes have never been deployed — use [Deploy](#deploy-nodes-deployyml) first
- You need to change firewall rules, Caddy config, or Docker Compose structure — use Deploy
- You need to change validator tokens — use Deploy

## Destroy Node Data (`destroy.yml`)

Wipes all PostFiat node data, containers, volumes, and configuration from target hosts. **This is irreversible.**

### Inputs

| Input | Options | Description |
|-------|---------|-------------|
| `network` | `devnet`, `testnet` | Target network |
| `node_type` | `validators`, `rpc`, `all` | Which node type(s) to destroy |

### What It Does

On each target host:
1. `docker compose down --volumes --remove-orphans` — stops and removes containers + volumes
2. Removes any remaining Docker volumes with the `postfiatd` prefix
3. `docker system prune -af` — removes all unused images, containers, networks
4. `rm -rf /opt/postfiatd` — deletes all deployment files

**Additionally for RPC nodes:**
5. Stops the Caddy systemd service
6. Removes `/etc/caddy/Caddyfile`

### What Gets Deleted

- All postfiatd data (ledger database, config, logs)
- All Docker images cached on the host
- The entire `/opt/postfiatd` directory
- Caddy configuration (RPC nodes only)

### What Survives

- The host OS and SSH access
- Installed packages (Docker, Caddy binary, etc.)
- UFW/iptables rules
- System-level logs

### When to Use Destroy

- **Network reset** — wiping a devnet/testnet to start fresh
- **Before a clean redeploy** when you want to guarantee no stale state
- **Decommissioning** nodes from a network

### When NOT to Use Destroy

- You just want to update the software — use [Update](#update-nodes-updateyml)
- You want to reconfigure nodes — use [Deploy](#deploy-nodes-deployyml), which tears down containers itself before redeploying
- **Never run on production/mainnet** unless you fully understand the consequences

## Typical Workflows

### Rolling out a new postfiatd build

1. Push code changes to the `devnet` or `testnet` branch
2. The corresponding build workflow (`devnet-build.yml` / `testnet-build.yml`) triggers automatically and pushes new Docker images
3. Run **Update Nodes** with the matching network and node type
4. Monitor the workflow run — each host shows its own job status

### Full network reset (devnet/testnet)

1. Run **Destroy Node Data** → network: `devnet`, node_type: `all`
2. Wait for it to complete
3. Run **Deploy Nodes** → network: `devnet`, node_type: `all`, reset_vhs: `true`
4. Validators come up first, then RPC/archive nodes, then VHS resets

### Adding a new validator

1. Generate a new validator token
2. Add the token as a GitHub secret (`{NETWORK}_VALIDATOR_TOKEN_{N}`)
3. Add the host IP to the `{NETWORK}_VALIDATOR_HOSTS` variable
4. Add the IP-to-token mapping in the `deploy.yml` validator token injection block
5. Run **Deploy Nodes** → select the network, node_type: `validators`

### Reprovisioning a single node type

Run **Deploy Nodes** with the specific `node_type`. Only that type gets redeployed — other node types are unaffected.

## Build Workflows

Deploy and Update rely on Docker images built by these workflows:

| Workflow | Branch Trigger | Image Tags |
|----------|---------------|------------|
| `devnet-build.yml` | Push to `devnet` | `agtipft/postfiatd:devnet-{light,medium,full}-latest` |
| `testnet-build.yml` | Push to `testnet` | `agtipft/postfiatd:testnet-{light,medium,full}-latest` |
| `mainnet-build.yml` | Push to `main` | `agtipft/postfiatd:mainnet-{light,medium,full}-latest` |

All build workflows also support manual `workflow_dispatch` triggers.

**Image size variants and their usage:**

| Variant | Used By | Description |
|---------|---------|-------------|
| `light` | Validators | Minimal node for consensus participation |
| `medium` | RPC nodes | Mid-size node for serving API requests |
| `full` | Archive nodes | Full history node for complete ledger data |

## Configuration

### GitHub Repository Variables

These are JSON arrays of host IPs. Set them in **Settings → Secrets and variables → Actions → Variables**.

| Variable | Example | Used By |
|----------|---------|---------|
| `DEVNET_VALIDATOR_HOSTS` | `["66.135.27.77","108.61.85.238"]` | deploy, update, destroy |
| `DEVNET_RPC_HOSTS` | `["149.28.X.X"]` | deploy, update, destroy |
| `DEVNET_ARCHIVE_HOSTS` | `["45.76.X.X"]` | deploy |
| `DEVNET_VHS_HOST` | `10.0.0.5` | deploy |
| `TESTNET_VALIDATOR_HOSTS` | `["96.30.199.55","144.202.24.188"]` | deploy, update, destroy |
| `TESTNET_RPC_HOSTS` | `["..."]` | deploy, update, destroy |
| `TESTNET_ARCHIVE_HOSTS` | `["..."]` | deploy |
| `TESTNET_VHS_HOST` | `...` | deploy |

### GitHub Repository Secrets

Set in **Settings → Secrets and variables → Actions → Secrets**.

| Secret | Purpose |
|--------|---------|
| `VULTR_SSH_KEY` | Private SSH key for root access to all hosts |
| `DEVNET_VALIDATOR_TOKEN_1` through `_4` | Validator identity tokens for devnet hosts |
| `TESTNET_VALIDATOR_TOKEN_1` through `_5` | Validator identity tokens for testnet hosts |
| `DOCKERHUB_USERNAME` | Docker Hub login for image push/pull |
| `DOCKERHUB_TOKEN` | Docker Hub access token |

### Network Ports

| Port | Protocol | Node Types | Purpose |
|------|----------|------------|---------|
| 22 | TCP | All | SSH access |
| 80 | TCP | RPC, Archive | Caddy HTTP (health check, redirect) |
| 443 | TCP | RPC, Archive | Caddy HTTPS (JSON-RPC and WebSocket APIs) |
| 2559 | TCP | All | Peer-to-peer protocol |
| 5005 | TCP | All (internal) | Admin RPC — only exposed to VHS host on RPC/Archive |
| 6005 | TCP | All (internal) | WebSocket — bound to localhost on RPC/Archive, proxied through Caddy |
| 6006 | TCP | All | Metrics |
| 50051 | TCP | All | gRPC |

## Troubleshooting

### Workflow fails with SSH connection error
- Verify the `VULTR_SSH_KEY` secret is correct and not expired
- Check that the host IP is reachable and SSH is running on port 22
- Confirm the host allows root login

### Health check shows "inconclusive"
- This is normal during initial sync — the node needs time to connect to peers and sync the ledger
- SSH into the host and check manually: `docker exec postfiatd postfiatd server_info`
- Check logs: `docker logs postfiatd --tail 100`

### Validator token not injected
- The deploy workflow maps tokens to hosts by IP address — verify the host IP matches a case in the injection block
- Check that the corresponding secret exists and is not empty

### Caddy fails to start (RPC/Archive)
- DNS for `rpc.{network}.postfiat.org` / `ws.{network}.postfiat.org` must point to the host IP before Caddy can obtain TLS certificates
- Check Caddy logs: `journalctl -u caddy --no-pager -n 50`

### Node stuck syncing after update
- Check peer connectivity: `docker exec postfiatd postfiatd peers`
- Verify the node can reach other validators/peers on port 2559
- If the ledger is corrupted, run Destroy then Deploy to start fresh
