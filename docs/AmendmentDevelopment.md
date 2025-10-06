# Amendment Development Guide

This guide provides step-by-step instructions for creating new amendments (also called features) in the PostFiat codebase.

## Overview

Amendments are protocol-level changes that are enabled through validator voting. They allow the network to upgrade in a coordinated, decentralized manner.

## Amendment Types

- **XRPL_FEATURE**: New functionality or feature additions
- **XRPL_FIX**: Bug fixes and corrections
- **XRPL_RETIRE**: Deprecated amendments (kept for historical ledger compatibility)

## Development Workflow

### 1. Register the Amendment

Add your amendment to `include/xrpl/protocol/detail/features.macro` at the **top of the list** (reverse chronological order):

```cpp
XRPL_FEATURE(YourFeatureName, Supported::no, VoteBehavior::DefaultNo)
// or for bug fixes:
XRPL_FIX(YourFixName, Supported::no, VoteBehavior::DefaultNo)
```

**Important**: Increment `numFeatures` in `include/xrpl/protocol/Feature.h` when adding a new amendment.

### 2. Implement Feature Logic

Use the generated variable to control code execution:

```cpp
if (view.rules.enabled(featureYourFeatureName))
{
    // New code path when amendment is enabled
}
else
{
    // Legacy code path
}
```

For fixes, the pattern is similar using `fixYourFixName`.

### 3. Mark as Supported

When development is **complete and tested**, update the amendment in `features.macro`:

```cpp
XRPL_FEATURE(YourFeatureName, Supported::yes, VoteBehavior::DefaultNo)
```

### 4. Choose Activation Strategy

There are two valid approaches for amendment activation:

#### Option A: External Governance (DefaultNo)

```cpp
XRPL_FEATURE(YourFeatureName, Supported::yes, VoteBehavior::DefaultNo)
```

With `DefaultNo`, validators must explicitly vote for the amendment. This allows the community to coordinate activation timing through external governance:

1. **Community Discussion**: The amendment is discussed in community forums, GitHub, and validator channels
2. **Validator Configuration**: Validators who wish to support the amendment add it to their `rippled.cfg`:
   ```
   [amendments]
   YourFeatureName
   ```
3. **Voting Process**: Once 67% of trusted validators vote yes for 2 weeks, the amendment activates
4. **Coordination**: This approach allows time for ecosystem preparation and coordinated rollout

#### Option B: Default Voting (DefaultYes)

```cpp
XRPL_FIX(CriticalFix, Supported::yes, VoteBehavior::DefaultYes)
```

With `DefaultYes`, validators automatically vote for the amendment by default. This is appropriate when:
- There's strong community consensus on immediate activation
- The fix addresses critical issues that need rapid deployment
- Pre-release communication has occurred through appropriate channels

**Important**: Ensure adequate community communication before using `DefaultYes`.

## Vote Behavior Options

- **VoteBehavior::DefaultNo**: Validators won't vote for it by default; requires explicit configuration for activation
- **VoteBehavior::DefaultYes**: Validators will vote for it by default; activates automatically when 67% threshold is reached
- **VoteBehavior::Obsolete**: Amendment was votable but never passed (kept for compatibility)

## Retirement Process

After an amendment has been enabled for several years (typically 2+), conditional code can be removed:

1. **Remove conditional logic** - Replace feature-gated code with the new behavior
2. **Move to retired section** in `features.macro`:

```cpp
XRPL_RETIRE(OldFeatureName)
```

**Note**: Retired amendments must remain registered indefinitely because they may exist in ledger history.

## Key Files

- `include/xrpl/protocol/detail/features.macro` - Amendment definitions
- `include/xrpl/protocol/Feature.h` - Feature declarations and `numFeatures` constant
- `src/libxrpl/protocol/Feature.cpp` - Feature registration implementation

## Best Practices

1. Always add new amendments at the **top** of `features.macro`
2. Start with `Supported::no` during development
3. Use descriptive, clear names for amendments
4. Keep amendments focused on a single change
5. Document the amendment's purpose in code comments
6. Always test with the amendment both enabled and disabled
7. Remember to update `numFeatures` when adding amendments

## Testing

Ensure your amendment works correctly:
- Test with amendment disabled (legacy behavior)
- Test with amendment enabled (new behavior)
- Verify the transition works correctly
- Check replay of old ledgers still functions

## Variable Naming Convention

- Features: `feature<Name>` (e.g., `featureAccountExclusion`)
- Fixes: `fix<Name>` (e.g., `fixAMMv1_3`)
- These variables are auto-generated from the macro definitions
