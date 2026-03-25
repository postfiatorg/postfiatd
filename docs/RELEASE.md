# Release Process

## Version Source

The version lives in `src/libxrpl/protocol/BuildInfo.cpp`:

```cpp
char const* const versionString = "1.0.0"
```

This is the single source of truth. CMake extracts it at build time. The CI/CD pipeline reads it to tag Docker images.

## How to Release

1. Bump the version string in `BuildInfo.cpp`
2. Commit to `main`
3. Merge to the target network branch (`devnet`, `testnet`, `mainnet`, `ai-devnet`)
4. Push triggers the build workflow, which:
   - Extracts the version from `BuildInfo.cpp`
   - Checks Docker Hub for existing versioned tags — **fails if the version already exists**
   - Builds and pushes images with both rolling and versioned tags

## Image Tags

Every network build produces two tags per image (3 sizes x 2 tags = 6 tags):

```
agtipft/postfiatd:{network}-{size}-latest        # rolling, overwritten every build
agtipft/postfiatd:{network}-{size}-{version}     # immutable, overwrite-protected
```

Example for devnet version 1.0.0:
```
agtipft/postfiatd:devnet-light-latest
agtipft/postfiatd:devnet-light-1.0.0
agtipft/postfiatd:devnet-medium-latest
agtipft/postfiatd:devnet-medium-1.0.0
agtipft/postfiatd:devnet-full-latest
agtipft/postfiatd:devnet-full-1.0.0
```

Feature branch builds only produce `{branch}-{size}-latest` tags with no versioning.

## Overwrite Protection

If you push to a network branch without bumping the version, the build fails with:

```
Error: Tag 'devnet-light-1.0.0' already exists. Bump the version in BuildInfo.cpp.
```

There is no bypass. This prevents accidental overwrites of known-good images.

If a build partially fails (e.g., `light` pushed but `full` crashed), delete the incomplete tags from Docker Hub and re-run the workflow.

## Deploying a Specific Version

**Rolling update** (pull new image, restart containers):
```bash
gh workflow run update.yml -f network=testnet -f node_type=all -f version=1.0.0
```

**Fresh deploy** (full provisioning):
```bash
gh workflow run deploy.yml -f network=testnet -f node_type=all -f version=1.0.0
```

Omit the `version` parameter to deploy the latest rolling image (default behavior).

## Rollback

Run the update workflow with a previous version:

```bash
gh workflow run update.yml -f network=testnet -f node_type=all -f version=3.0.0
```

The versioned tag is immutable and always available on Docker Hub.

## Checking Available Versions

```bash
# List recent tags
curl -s "https://hub.docker.com/v2/repositories/agtipft/postfiatd/tags/?page_size=20" \
  | jq '.results[].name'

# Check if a specific version exists
curl -s -o /dev/null -w "%{http_code}" \
  "https://hub.docker.com/v2/repositories/agtipft/postfiatd/tags/testnet-light-1.0.0"
# 200 = exists, 404 = does not exist
```
