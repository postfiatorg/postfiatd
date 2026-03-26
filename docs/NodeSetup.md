# Post Fiat Node Setup Guide

## Prerequisites

Before starting, ensure you have:

- **Operating System**: Fresh Ubuntu Server (22.04 LTS or later recommended)
- **Hardware Requirements**:
  - Minimum 2 CPU cores
  - 4GB RAM
  - 100GB storage space

## Installation Steps

### 1. Install Docker

Update your system and install Docker with the Compose plugin:

```bash
sudo apt update
sudo apt install docker.io docker-compose-v2
```

### 2. Configure Firewall

Your validator exposes several ports, some of which are for local administration only. Without a firewall, admin ports (5005, 6006, 50051) are accessible from the public internet, allowing anyone to stop your node or alter its configuration.

**Set up UFW** to deny all incoming traffic except SSH and the peer protocol port:

```bash
sudo ufw --force reset
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow 22/tcp comment 'SSH'
sudo ufw allow 2559/tcp comment 'Peer protocol'
sudo ufw --force enable
```

This blocks all external access to admin ports (5005, 6006, 50051) while keeping peer-to-peer communication open. You can still reach admin ports from localhost (e.g., `curl http://localhost:5005/`).

**Set up iptables rate limiting** to protect the peer port against connection floods. Even though UFW allows port 2559, these rules are evaluated first and cap how aggressively a single IP can connect:

```bash
# Remove existing rules if present (safe to run on first setup)
sudo iptables -D INPUT -p tcp --dport 2559 -m state --state ESTABLISHED,RELATED -j ACCEPT 2>/dev/null || true
sudo iptables -D INPUT -p tcp --dport 2559 -m connlimit --connlimit-above 50 -j DROP 2>/dev/null || true
sudo iptables -D INPUT -p tcp --dport 2559 -m state --state NEW -m limit --limit 100/second --limit-burst 50 -j ACCEPT 2>/dev/null || true
sudo iptables -D INPUT -p tcp --dport 2559 -m state --state NEW -j DROP 2>/dev/null || true

# Add rate limiting rules (inserted in reverse order so they end up in correct order)
# Final order: ESTABLISHED->ACCEPT, connlimit->DROP, rate-limit->ACCEPT, NEW->DROP
sudo iptables -I INPUT -p tcp --dport 2559 -m state --state NEW -j DROP
sudo iptables -I INPUT -p tcp --dport 2559 -m state --state NEW -m limit --limit 100/second --limit-burst 50 -j ACCEPT
sudo iptables -I INPUT -p tcp --dport 2559 -m connlimit --connlimit-above 50 -j DROP
sudo iptables -I INPUT -p tcp --dport 2559 -m state --state ESTABLISHED,RELATED -j ACCEPT

# Save rules so they survive reboots
if command -v netfilter-persistent &> /dev/null; then
  sudo netfilter-persistent save 2>/dev/null || true
elif ! command -v ufw &> /dev/null; then
  sudo apt-get install -y iptables-persistent 2>/dev/null || true
  sudo netfilter-persistent save 2>/dev/null || true
fi
```

This does two things: no single IP can hold more than 50 concurrent connections (normal peers use exactly 1), and no more than 100 new connections per second are accepted globally. Already-established peer connections are unaffected.

### 3. Set Up the Node Directory

Create the directory and download the appropriate Docker Compose file for your node role.

**Validator node** (light image — minimal history, for consensus participation):

```bash
mkdir -p /opt/postfiatd/logs
cd /opt/postfiatd
wget https://raw.githubusercontent.com/postfiatorg/postfiatd/main/scripts/docker-compose-external-validator.yml -O docker-compose.yml
```

**RPC node** (medium image — serves API requests):

```bash
mkdir -p /opt/postfiatd/logs
cd /opt/postfiatd
wget https://raw.githubusercontent.com/postfiatorg/postfiatd/main/scripts/docker-compose-rpc.yml -O docker-compose.yml
```

### 4. Configure the Network

Create a `.env` file to select the target network (`devnet`, `testnet`, or `mainnet`):

```bash
echo "NETWORK=testnet" > .env
echo "HOSTNAME=$(hostname)" >> .env
```

### 5. Start the Node

Launch the Post Fiat node:

```bash
docker compose up -d
```

### 6. Verify Installation

Check that the Docker container is running properly:

```bash
docker compose ps
```

## Node Management

### Viewing Logs

To monitor node activity:

```bash
docker compose logs
```

> **Note**: Full logs are also available in `/opt/postfiatd/logs`

### Modifying Configuration

To update the node configuration:

1. **Edit the configuration file** inside the running container:
   ```bash
   docker exec -it postfiatd nano /etc/postfiatd/postfiatd.cfg
   ```

2. **Restart the container** for changes to take effect:
   ```bash
   docker compose restart
   ```

## Running as a Validator

The container will run a postfiatd node that connects to the Post Fiat testnet by default. To operate as a validator, you'll need to create a validator token using the validator keys tool.

### Validator keys tool

- Validator keys tool is present in the postfiatd docker image. To use it run bash shell inside postfiatd running Docker container:
```bash
docker exec -it postfiatd bash
validator-keys --help
```
   A validator uses a public/private key pair. The validator is identified by the
public key. The private key should be tightly controlled. It is used to:

*   sign tokens authorizing a postfiatd server to run as the validator identified
    by this public key.
*   sign revocations indicating that the private key has been compromised and
    the validator public key should no longer be trusted.

Each new token invalidates all previous tokens for the validator public key.
The current token needs to be present in the postfiatd config file.

Servers that trust the validator will adapt automatically when the token
changes.

## Validator Keys

When first setting up a validator, use the `validator-keys` tool to generate
its key pair:

```
  $ validator-keys create_keys
```

Sample output:
```
  Validator keys stored in /root/.ripple/validator-keys.json
```

## Validator Token

After first creating the [validator keys](#validator-keys) or if the previous
token has been compromised, use the `validator-keys` tool to create a new
validator token with the domain of your validator:

```
  $ validator-keys set_domain [VALIDATOR_DOMAIN]
```

Sample output:

```
The domain name has been set to: postfiat.org

The domain attestation for validator nHU3VNRD3cBsFwcDcKaMUoikag3RE7PS9p8L4Uj9dYQFv3zsLWdQ is:

attestation="52AD96B548AC71213D487307870440B74EC3A8A7B0DC887A1B936D932E4101782E08C9CCF620F0605021B38A4A42889941F4EFFB18BFB8DA3EF67181FE88C301"

You should include it in your pft-ledger.toml file in the
section for this validator.

You also need to update the postfiatd.cfg file to add a new
validator token and restart postfiatd:

# validator public key: nHU3VNRD3cBsFwcDcKaMUoikag3RE7PS9p8L4Uj9dYQFv3zsLWdQ

[validator_token]
eyJ2YWxpZGF0aW9uX3NlY3J|dF9rZXkiOiI5ZWQ0NWY4NjYyNDFjYzE4YTI3NDdiNT
QzODdjMDYyNTkwNzk3MmY0ZTcxOTAyMzFmYWE5Mzc0NTdmYT|kYWY2IiwibWFuaWZl
c3QiOiJKQUFBQUFGeEllMUZ0d21pbXZHdEgyaUNjTUpxQzlnVkZLaWxHZncxL3ZDeE
hYWExwbGMyR25NaEFrRTFhZ3FYeEJ3RHdEYklENk9NU1l1TTBGREFscEFnTms4U0tG
bjdNTzJmZGtjd1JRSWhBT25ndTlzQUtxWFlvdUorbDJWMFcrc0FPa1ZCK1pSUzZQU2
hsSkFmVXNYZkFpQnNWSkdlc2FhZE9KYy9hQVpva1MxdnltR21WcmxIUEtXWDNZeXd1
NmluOEhBU1FLUHVnQkQ2N2tNYVJGR3ZtcEFUSGxHS0pkdkRGbFdQWXk1QXFEZWRGdj
VUSmEydzBpMjFlcTNNWXl3TFZKWm5GT3I3QzBrdzJBaVR6U0NqSXpkaXRROD0ifQ==
```

**Note**: The public key and token values shown above are examples. Your actual output will contain different values unique to your validator.

### Configuring Your Validator

For a new validator, add the `[validator_token]` value to the postfiatd config file.
For a pre-existing validator, replace the old `[validator_token]` value with the
newly generated one. A valid config file may only contain one `[validator_token]`
value.

### Domain Verification

To verify your validator's domain ownership, you need to publish the attestation on your domain:

1. **Create the domain verification file** at `https://your-domain.com/.well-known/pft-ledger.toml`

2. **Add your validator information** in the following format:

```toml
[[VALIDATORS]]
public_key = "nHU3VNRD3cBsFwcDcKaMUoikag3RE7PS9p8L4Uj9dYQFv3zsLWdQ"
attestation = "52AD96B548AC71213D487307870440B74EC3A8A7B0DC887A1B936D932E4101782E08C9CCF620F0605021B38A4A42889941F4EFFB18BFB8DA3EF67181FE88C301"
```

Replace the `public_key` and `attestation` values with your actual values from the `set_domain` command output.

3. **Ensure the file is publicly accessible** at `https://your-domain.com/.well-known/pft-ledger.toml`

This attestation proves that you control both the validator keys and the domain, establishing a verifiable link between your validator identity and your domain.

> **CRITICAL: Backup Master Keys Before Docker Restart!**
>
> The `validator-keys.json` file contains your **master validator keys** and is stored inside the Docker container at `/root/.ripple/validator-keys.json`.
>
> **This file is deleted when the Docker container is restarted or recreated.**
>
> Before running `docker compose down` or `docker compose restart`:
> 1. **Copy the file from the container to a secure location**:
>    ```bash
>    docker cp postfiatd:/root/.ripple/validator-keys.json ./validator-keys-backup.json
>    ```
> 2. **Store the backup in a secure, offline location** (encrypted USB drive, secure vault, etc.)
> 3. **Never keep this file on the validator machine** after backing it up
>
> Without this backup, you will lose the ability to:
> - Generate new validator tokens for your validator public key
> - Update your validator domain
> - Revoke your keys if compromised
>
> If you lose these master keys, you will need to create a completely new validator identity.

After the config is updated, restart postfiatd.

There is a hard limit of 4,294,967,293 tokens that can be generated for a given
validator key pair.

This operation also updates the validator-keys.json file with this data.
Save the validator-keys.json to a secure location to be used when creating a 
new signing key or updating the domain.

## Key Revocation

If a validator private key is compromised, the key must be revoked permanently.
To revoke the validator key, use the `validator-keys` tool to generate a
revocation, which indicates to other servers that the key is no longer valid:

```
  $ validator-keys revoke_keys
```

Sample output:

```
  WARNING: This will revoke your validator keys!

  Update postfiatd.cfg file with these values and restart postfiatd:

  # validator public key: nHUtNnLVx7odrz5dnfb2xpIgbEeJPbzJWfdicSkGyVw1eE5GpjQr

  [validator_key_revocation]
  JP////9xIe0hvssbqmgzFH4/NDp1z|3ShkmCtFXuC5A0IUocppHopnASQN2MuMD1Puoyjvnr
  jQ2KJSO/2tsjRhjO6q0QQHppslQsKNSXWxjGQNIEa6nPisBOKlDDcJVZAMP4QcIyNCadzgM=
```

Add the `[validator_key_revocation]` value to this validator's config and
restart postfiatd. Rename the old key file and generate new [validator keys](#validator-keys) and
a corresponding [validator token](#validator-token).

## Signing

The `validator-keys` tool can be used to sign arbitrary data with the validator
key.

```
  $ validator-keys sign "your data to sign"
```

Sample output:

```
  B91B73536235BBA028D344B81DBCBECF19C1E0034AC21FB51C2351A138C9871162F3193D7C41A49FB7AABBC32BC2B116B1D5701807BE462D8800B5AEA4F0550D
```

1. **Key Management**:
   - Keys generated in `validator-keys.json` should be stored safely and separately from the validator token
   - Only the validator token should be used in the validator configuration file
   - **Never expose your private keys** - keep them secure and backed up
  
2. **Validator status**
   - Once a validator is running successfully it should appear in the validator list in the explorer: https://explorer.testnet.postfiat.org/network/validators
   - Server status of the node can be checked locally by running server_info rpc:
     ```
     curl -s -X POST http://localhost:5005/ -H "Content-Type: application/json" -d '{"method": "server_info","params": [{}]}'
     ```

### Security Best Practices

- Store validator keys in a secure, offline location
- Use the validator token (not private keys) in your configuration
- Regularly backup your configuration and keys
- Monitor your validator's performance and connectivity

## Troubleshooting

If you encounter issues:

1. Check container status: `docker compose ps`
2. Review logs: `docker compose logs`
