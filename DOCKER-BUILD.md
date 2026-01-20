# Docker Build Guide

This guide explains how to build PostFiat Docker images with different network and node size configurations.

## Available Configurations

The Dockerfile supports the following configuration files:

| Config | Network | Node Size | History |
|--------|---------|-----------|---------|
| `postfiatd-mainnet-full.cfg` | Mainnet | Full | Complete history |
| `postfiatd-mainnet-medium.cfg` | Mainnet | Medium | ~50,000 ledgers |
| `postfiatd-mainnet-light.cfg` | Mainnet | Light | ~512 ledgers |
| `postfiatd-testnet-full.cfg` | Testnet | Full | Complete history |
| `postfiatd-testnet-medium.cfg` | Testnet | Medium | ~50,000 ledgers |
| `postfiatd-testnet-light.cfg` | Testnet | Light | ~512 ledgers |
| `postfiatd-devnet-full.cfg` | Devnet | Full | Complete history |
| `postfiatd-devnet-medium.cfg` | Devnet | Medium | ~50,000 ledgers |
| `postfiatd-devnet-light.cfg` | Devnet | Light | ~512 ledgers |

## Build Arguments

The Dockerfile accepts two build arguments:

- `NETWORK` - Network type: `mainnet`, `testnet`, or `devnet` (default: `devnet`)
- `NODE_SIZE` - Node size: `full`, `medium`, or `light` (default: `full`)

## Node Size Recommendations

| Node Size | Use Case | Disk Space |
|-----------|----------|------------|
| `full` | Archive nodes, block explorers | High |
| `medium` | RPC nodes serving API clients | Moderate |
| `light` | Validators, minimal relay nodes | Low |

## Build Examples

### Build with Default Configuration (devnet-full)

```bash
docker build -t postfiatd .
```

### Build Mainnet Nodes

```bash
# Full archive node
docker build \
  --build-arg NETWORK=mainnet \
  --build-arg NODE_SIZE=full \
  -t postfiatd:mainnet-full \
  .

# Medium RPC node
docker build \
  --build-arg NETWORK=mainnet \
  --build-arg NODE_SIZE=medium \
  -t postfiatd:mainnet-medium \
  .

# Light node
docker build \
  --build-arg NETWORK=mainnet \
  --build-arg NODE_SIZE=light \
  -t postfiatd:mainnet-light \
  .
```

### Build Testnet Nodes

```bash
# Full archive node
docker build \
  --build-arg NETWORK=testnet \
  --build-arg NODE_SIZE=full \
  -t postfiatd:testnet-full \
  .

# Medium RPC node
docker build \
  --build-arg NETWORK=testnet \
  --build-arg NODE_SIZE=medium \
  -t postfiatd:testnet-medium \
  .

# Light node
docker build \
  --build-arg NETWORK=testnet \
  --build-arg NODE_SIZE=light \
  -t postfiatd:testnet-light \
  .
```

### Build Devnet Nodes

```bash
# Full archive node
docker build \
  --build-arg NETWORK=devnet \
  --build-arg NODE_SIZE=full \
  -t postfiatd:devnet-full \
  .

# Medium RPC node
docker build \
  --build-arg NETWORK=devnet \
  --build-arg NODE_SIZE=medium \
  -t postfiatd:devnet-medium \
  .

# Light node
docker build \
  --build-arg NETWORK=devnet \
  --build-arg NODE_SIZE=light \
  -t postfiatd:devnet-light \
  .
```

## Running the Container

After building the image, you can run it with:

```bash
docker run -d \
  -p 5005:5005 \
  -p 6006:6006 \
  -p 50051:50051 \
  -v /path/to/data:/var/lib/postfiatd/db \
  -v /path/to/logs:/var/log/postfiatd \
  postfiatd:your-tag
```

## Docker Compose Example

You can also use docker-compose to manage different configurations:

```yaml
version: '3.8'

services:
  postfiatd-mainnet:
    build:
      context: .
      args:
        NETWORK: mainnet
        NODE_SIZE: medium
    image: postfiatd:mainnet-medium
    ports:
      - "5005:5005"
      - "6006:6006"
      - "50051:50051"
    volumes:
      - mainnet-data:/var/lib/postfiatd/db
      - mainnet-logs:/var/log/postfiatd

  postfiatd-testnet:
    build:
      context: .
      args:
        NETWORK: testnet
        NODE_SIZE: medium
    image: postfiatd:testnet-medium
    ports:
      - "5015:5005"
      - "6016:6006"
      - "50061:50051"
    volumes:
      - testnet-data:/var/lib/postfiatd/db
      - testnet-logs:/var/log/postfiatd

volumes:
  mainnet-data:
  mainnet-logs:
  testnet-data:
  testnet-logs:
```

## Verifying the Configuration

After running the container, you can verify which configuration is being used:

```bash
# Check the configuration file inside the container
docker exec <container-id> cat /etc/postfiatd/postfiatd.cfg | grep network_id
```

This should show the network ID corresponding to your selected network:
- `network_id = 2026` for mainnet
- `network_id = 2025` for testnet
- `network_id = 2024` for devnet
