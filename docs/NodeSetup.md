# Post Fiat Node Setup Guide

## Prerequisites

Before starting, ensure you have:

- **Operating System**: Fresh Ubuntu Server (22.04 LTS or later recommended)
- **Hardware Requirements**:
  - Minimum 2 CPU cores
  - 4GB RAM
  - 100GB storage space

## Installation Steps

### 1. Install Docker Compose

Update your system and install Docker Compose:

```bash
sudo apt update
sudo apt install docker-compose
```

### 2. Set Up the Node Directory

Create the necessary directory structure and download the configuration:

```bash
mkdir -p /opt/postfiatd
cd /opt/postfiatd
wget https://raw.githubusercontent.com/postfiatorg/postfiatd/testnet_candidate/scripts/docker-compose.yml
```

### 3. Start the Node

Launch the Post Fiat node using Docker Compose:

```bash
docker-compose up -d
```

### 4. Verify Installation

Check that the Docker container is running properly:

```bash
docker-compose ps
```

## Node Management

### Viewing Logs

To monitor node activity:

```bash
docker-compose logs
```

> **Note**: Full logs are also available in `/opt/postfiatd/logs`

### Modifying Configuration

To update the node configuration:

1. **Stop the container**:
   ```bash
   docker-compose down
   ```

2. **Edit the configuration file**:
   ```bash
   nano /var/lib/docker/volumes/postfiatd_postfiatd-config/_data/postfiatd.cfg
   ```

3. **Restart the container**:
   ```bash
   docker-compose up -d
   ```

## Running as a Validator

The container will run a postfiatd node that connects to the Post Fiat testnet by default. To operate as a validator, you'll need to create a validator token using the XRP process.

### Validator Setup Requirements

1. **Install the Ripple Validator Keys Tool**:
   - Repository: [https://github.com/ripple/validator-keys-tool](https://github.com/ripple/validator-keys-tool)
   - Follow the comprehensive guide: [Validator Keys Tool Guide](https://github.com/ripple/validator-keys-tool/blob/master/doc/validator-keys-tool-guide.md)

2. **Key Management**:
   - Keys generated in `validator-keys.json` should be stored safely and separately from the validator token
   - Only the validator token should be used in the validator configuration file
   - **Never expose your private keys** - keep them secure and backed up

### Security Best Practices

- Store validator keys in a secure, offline location
- Use the validator token (not private keys) in your configuration
- Regularly backup your configuration and keys
- Monitor your validator's performance and connectivity

## Troubleshooting

If you encounter issues:

1. Check container status: `docker-compose ps`
2. Review logs: `docker-compose logs`
