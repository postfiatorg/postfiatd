# Docker Build Guide

This guide explains how to build PostFiat Docker images with different network and node size configurations.

## Available Configurations

The Dockerfile supports the following configuration files:

- `postfiatd-mainnet-full.cfg` - Mainnet full node
- `postfiatd-mainnet-light.cfg` - Mainnet light node
- `postfiatd-testnet-full.cfg` - Testnet full node
- `postfiatd-testnet-light.cfg` - Testnet light node
- `postfiatd-devnet-full.cfg` - Devnet full node (default)
- `postfiatd-devnet-light.cfg` - Devnet light node

## Build Arguments

The Dockerfile accepts two build arguments:

- `NETWORK` - Network type: `mainnet`, `testnet`, or `devnet` (default: `devnet`)
- `NODE_SIZE` - Node size: `full` or `light` (default: `full`)

## Build Examples

### Build with Default Configuration (devnet-full)

```bash
docker build -t postfiatd .
```

### Build Mainnet Full Node

```bash
docker build \
  --build-arg NETWORK=mainnet \
  --build-arg NODE_SIZE=full \
  -t postfiatd:mainnet-full \
  .
```

### Build Mainnet Light Node

```bash
docker build \
  --build-arg NETWORK=mainnet \
  --build-arg NODE_SIZE=light \
  -t postfiatd:mainnet-light \
  .
```

### Build Testnet Full Node

```bash
docker build \
  --build-arg NETWORK=testnet \
  --build-arg NODE_SIZE=full \
  -t postfiatd:testnet-full \
  .
```

### Build Testnet Light Node

```bash
docker build \
  --build-arg NETWORK=testnet \
  --build-arg NODE_SIZE=light \
  -t postfiatd:testnet-light \
  .
```

### Build Devnet Full Node

```bash
docker build \
  --build-arg NETWORK=devnet \
  --build-arg NODE_SIZE=full \
  -t postfiatd:devnet-full \
  .
```

### Build Devnet Light Node

```bash
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
        NODE_SIZE: full
    image: postfiatd:mainnet-full
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
        NODE_SIZE: full
    image: postfiatd:testnet-full
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
- `network_id = main` for mainnet
- `network_id = test` for testnet
- `network_id = devnet` for devnet
