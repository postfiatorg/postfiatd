# Validator Key and Domain Update Guide

This guide explains how to update your validator's signing key and/or domain configuration. These operations are necessary when you need to rotate keys for security purposes or update your validator's domain information.

## Prerequisites

Before proceeding, ensure you have:

- A running Post Fiat validator node
- Access to the Docker container running your validator
- Your existing `validator-keys.json` file backed up in a secure location
- Docker and Docker Compose installed

## Restoring Master Keys to Docker Container

> **Important**: The `validator-keys.json` file is deleted whenever the Docker container is restarted or recreated. Before you can generate a new token or update your validator configuration, you must first copy your backed-up master keys file back into the running Docker container.

### Copy the Master Keys File

Before performing any validator key operations, restore your `validator-keys.json` file to the container:

```bash
# Copy the backed-up validator-keys.json from your secure location to the container
docker cp /path/to/your/backup/validator-keys.json postfiatd:/root/.ripple/validator-keys.json
```

Replace `/path/to/your/backup/validator-keys.json` with the actual path to your backed-up file.

### Verify the File Was Copied Successfully

Confirm the file is now present in the container:

```bash
docker exec -it postfiatd ls -la /root/.ripple/validator-keys.json
```

You should see output showing the file exists with appropriate permissions.

### Security Reminder

After completing your validator key operations:

1. **Backup the updated file** (it now contains the new sequence number):
   ```bash
   docker cp postfiatd:/root/.ripple/validator-keys.json ./validator-keys-backup-$(date +%Y%m%d).json
   ```

2. **Store the backup in a secure, offline location** (encrypted USB drive, secure vault, etc.)

3. **Remove the local copy** from the validator machine after backing up to secure storage

## Understanding Validator Keys and Tokens

A Post Fiat validator uses a two-tier key system:

- **Master Key Pair**: The permanent validator identity (stored in `validator-keys.json`)
- **Ephemeral Signing Key**: A temporary key that can be rotated without changing the validator identity
- **Validator Token**: A signed manifest that authorizes a specific signing key for your validator

When you update the signing key or domain, you generate a new validator token that includes:
- Your master public key (validator identity)
- A new ephemeral signing key
- The domain (optional)
- A sequence number (incremented with each new token)
- Signatures from both the master key and ephemeral signing key

## Generating a New Token

All validator updates (whether updating the signing key, domain, or both) involve generating a new validator token. This section explains the token generation process that applies to all scenarios.

### Prerequisites

Before generating a new token, you must have:

**Existing validator master keys**: Your `validator-keys.json` file must already exist and contain your validator's master key pair and the current sequence number. This file should be located at `/root/.ripple/validator-keys.json` (or `~/.ripple/validator-keys.json`).

### When to Generate a New Token

You should generate a new validator token when:

- Rotating your signing key periodically as a security best practice (e.g., every 6-12 months)
- You suspect the current signing key may have been compromised
- Moving your validator to new hardware
- Updating or setting your validator's domain
- As part of routine security maintenance

### Access the Validator Keys Tool

For all token generation operations, first enter the running Docker container:

```bash
docker exec -it postfiatd bash
```

## Updating the Signing Key Only

Use this method when you want to rotate your signing key while keeping your existing domain unchanged.

### Generate a New Token

Generate a new validator token (this will keep your existing domain if you have one):

```bash
validator-keys create_token
```

## Updating the Domain (and Signing Key)

Use this method when you want to set or change your validator's domain. **Note that `set_domain` automatically generates a new signing key along with the domain update**, so you get both updates in one operation.

### Set the New Domain

The `set_domain` command generates a new token with both a new signing key and your specified domain:

```bash
validator-keys set_domain your-domain.com
```

**Important**: The domain should be entered without the protocol (no `https://` or `http://`). For example:
- Correct: `validator.example.com` or `example.com`
- Incorrect: `https://example.com`


**Sample output:**

```
The validator key pair is generated.

Update postfiatd.cfg file with these values:

# validator public key: nHUtNnLVx7odrz5dnfb2xpIgbEeJPbzJWfdicSkGyVw1eE5GpjQr

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

### Update the Configuration

After generating a new token (regardless of the method used), you need to update the configuration file.

**Option A: Direct edit (if you have access to the volume)**

```bash
nano /var/lib/docker/volumes/postfiatd_postfiatd-config/_data/postfiatd.cfg
```

**Option B: Edit from within the container**

```bash
# From within the container
nano /root/.config/ripple/postfiatd.cfg
```

Find the existing `[validator_token]` section and replace it with the new token. The configuration file should contain only **one** `[validator_token]` section.

**Before:**
```
[validator_token]
eyJ... [OLD TOKEN] ...ifQ==
```

**After:**
```
[validator_token]
eyJ... [NEW TOKEN] ...ifQ==
```

> **CRITICAL: Backup Master Keys Before Docker Restart!**
>
> The `validator-keys.json` file contains your **master validator keys** and is stored inside the Docker container at `/root/.ripple/validator-keys.json`.
>
> **This file is deleted when the Docker container is restarted or recreated.**
>
> Before running `docker-compose down` or `docker-compose restart`:
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

### Restart the Validator

Exit the container and restart it to apply the changes:

```bash
exit
docker-compose restart
```

## Important Considerations

### Token Sequence Numbers

- Each new token has a higher sequence number than the previous one
- Nodes on the network will automatically accept the new token if the sequence number is higher
- Old tokens are automatically invalidated when a new token is published
- There is a hard limit of 4,294,967,293 tokens per validator key pair

### Propagation Time

- New tokens propagate through the network automatically
- Other validators will detect and accept the new manifest within minutes
- No coordination with other operators is required
- The validator identity (master public key) remains unchanged

### Key File Management

- **Never lose your `validator-keys.json` file** - it contains your validator identity
- Keep secure backups in multiple locations (encrypted USB drives, secure cloud storage, etc.)
- The key file should be protected with appropriate file permissions
- Consider keeping the key file offline and only accessing it when needed for key operations

### Security Best Practices

1. **Rotation Schedule**: Establish a regular key rotation schedule (e.g., quarterly or semi-annually)
2. **Secure Environment**: Perform key operations in a secure, isolated environment
3. **Backup Before Changes**: Always backup your current configuration before making changes
4. **Monitor After Updates**: Watch logs and network status after updating to ensure proper operation
5. **Document Changes**: Keep a record of when keys were rotated and why

## Troubleshooting

### Token Not Accepted

If your new token is not being accepted:

1. Verify the sequence number is higher than the previous token
2. Check that the token is properly formatted in the config file (no extra spaces or line breaks within the token)
3. Ensure the `[validator_token]` section header is present
4. Confirm there is only one `[validator_token]` section in the config

### Validator Not Showing in Network

If your validator doesn't appear in the network after updating:

1. Check container logs: `docker-compose logs -f`
2. Verify the validator token is correctly formatted
3. Ensure the config file was saved properly
4. Confirm the container restarted successfully: `docker-compose ps`
5. Check network connectivity and peer connections

### Domain Not Appearing

If the domain is not showing for your validator:

1. Verify you used the correct format (no protocol, e.g., `example.com` not `https://example.com`)
2. Confirm the token was generated with the domain parameter
3. Allow time for the manifest to propagate through the network (usually a few minutes)
4. Check that your domain is accessible and properly configured

## Related Documentation

- [NodeSetup.md](NodeSetup.md) - Initial validator setup guide
- [Post Fiat Explorer](https://explorer.testnet.postfiat.org/network/validators) - Monitor validator status

## Getting Help

If you encounter issues not covered in this guide:

1. Check container status: `docker-compose ps`
2. Review logs: `docker-compose logs -f`
3. Verify configuration: Check for syntax errors in `postfiatd.cfg`
4. Consult the community or open an issue on the project repository
