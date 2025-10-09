# UNL Update Process via Amendments

This document describes how to update the Unique Node List (UNL) using the amendment system.

## Overview

The UNL (Unique Node List) defines which validators the network trusts for consensus. Updating the UNL through amendments allows for:

- **Decentralized governance**: Changes require 67% validator approval
- **Coordinated rollout**: All nodes update simultaneously when amendment activates
- **Historical tracking**: Amendment activations are recorded on-ledger
- **Full replacement**: Each update provides a complete new validator list

## UNL Update Amendment Pattern

### Design Philosophy

The implementation uses a simple approach:
- Each UNL update amendment contains a **complete list** of validators (not incremental changes)
- The entire validator list is replaced when an amendment activates
- Application.cpp checks which UNL amendment is enabled and uses the corresponding list
- No complex ledger objects or state management needed

### Example: UNLUpdate1

The `UNLUpdate1` amendment demonstrates the first UNL update.

## Step-by-Step Implementation

### 1. Create the Amendment

Add the amendment to `include/xrpl/protocol/detail/features.macro`:

```cpp
XRPL_FEATURE(UNLUpdate1, Supported::yes, VoteBehavior::DefaultNo)
```

Key parameters:
- **Name**: Use a sequential naming pattern: `UNLUpdate1`, `UNLUpdate2`, etc.
- **Supported**: Set to `yes` when ready for activation
- **VoteBehavior**: Use `DefaultNo` to require explicit validator voting

### 2. Define the New Validator List

In `src/xrpld/core/UNLConfig.h`, add the complete new validator list:

```cpp
/**
 * UNL Update 1 - Complete replacement list
 * Becomes active when featureUNLUpdate1 amendment is enabled
 */
inline std::vector<std::string> const unlUpdate1List = {
    "nHU3VNRD3cBsFwcDcKaMUoikag3RE7PS9p8L4Uj9dYQFv3zsLWdQ",
    "nHUHS6rzWd2toxnaCLLcAD6nTLUBKxBsRanjywxLeyZ2q19AmZxe",
    "nHUkbNkhJcPDnSjCuZwqcAiHJUxYvirLJt8Qy38Wyvk6Tri1cq1A",
    "nHUedN7diUp6o3p6H7f6JFSoHfwC3TFjt5YEmrMcwh6p2PYggbpv",
    "nHBiHzPq3iiJ7MxZkZ3LoBBJneRtcZAoXm5Crb985neVN6ygQ3b7",
    "nHBidG3pZK11zQD6kpNDoAhDxH6WLGui6ZxSbUx7LSqLHsgzMPec",  // New validator
    // ... complete list of all validators for this UNL version
};
```

**Important**: This must be a **complete** list. It replaces the entire previous UNL.

### 3. Add Amendment Check

In `src/xrpld/core/UNLConfig.cpp`, add the check in the `getActiveUNL()` function:

```cpp
std::vector<std::string>
getActiveUNL(AmendmentTable const& amendmentTable)
{
    // Check for UNL update amendments in reverse order (most recent first)
    if (amendmentTable.isEnabled(featureUNLUpdate1))
        return unlUpdate1List;

    // Default to initial UNL
    return initialValidatorsList;
}
```

## Activation Process

### 1. Community Discussion

- Announce proposed UNL changes through official channels
- Provide rationale for adding/removing validators
- Allow time for community feedback (typically 2-4 weeks)

### 2. Code Release

- Release a new version of the software with the amendment
- Validators upgrade to the new version
- Amendment remains inactive until voted in

### 3. Validator Voting

Validators add the amendment to their `rippled.cfg`:

```ini
[amendments]
UNLUpdate1
```

### 4. Activation & Dynamic Reload

- Once 67% of trusted validators vote yes for 2 weeks, the amendment activates
- **On activation**, the `reloadUNL()` function automatically executes:
  - Determines which UNL list should be active based on enabled amendments
  - Calls `validators().load()` with the new validator list
  - Updates the amendment table with new trusted validators
- **The UNL changes take effect immediately** - no restart required!
- All nodes running the new code will automatically switch to the new UNL

### How Dynamic Reload Works

When the amendment activates in a ledger:

1. **Amendment Enabled**: Change transaction marks `UNLUpdate1` as enabled
2. **Trigger**: `applyAmendment()` detects UNL update amendment → calls `reloadUNL()`
3. **Reload**: `reloadUNL()` loads the new validator list via `validators().load()`
4. **Update**: Amendment table is notified of new trusted validators
5. **Active**: New UNL is immediately used for consensus validation

**No server restart needed** - the switch happens atomically when the amendment activates.

## Best Practices

### Frequency

- Schedule UNL updates regularly (e.g., quarterly or bi-annually)
- Avoid emergency updates unless absolutely necessary
- Batch multiple validator changes into a single update when possible

### Governance

- Document the criteria for adding validators
- Document the criteria for removing validators
- Maintain transparency in the selection process
- Provide advance notice (minimum 30 days recommended)

### Technical Considerations

- Test UNL updates on testnet first
- Ensure new validators are properly configured before addition
- Verify validators being removed are actually offline/problematic
- Monitor network health closely after activation

## Future UNL Updates

For subsequent updates, follow the same pattern:

### UNLUpdate2 Example

**1. Add amendment in features.macro:**
```cpp
XRPL_FEATURE(UNLUpdate2, Supported::yes, VoteBehavior::DefaultNo)
```

**2. Define the complete new list in UNLConfig.h:**
```cpp
/**
 * UNL Update 2 - Complete replacement list
 */
inline std::vector<std::string> const unlUpdate2List = {
    "nHU3VNRD3cBsFwcDcKaMUoikag3RE7PS9p8L4Uj9dYQFv3zsLWdQ",
    "nHBidG3pZK11zQD6kpNDoAhDxH6WLGui6ZxSbUx7LSqLHsgzMPec",
    "nHDg3E9ZgH6vZfMdYEVJ5yVNVbHLbwJ8Z2PrqbXXCVk5Dq9gKz4E",
    // ... complete list for UNL v2
};
```

**3. Update the amendment check in UNLConfig.cpp (add to the top):**
```cpp
std::vector<std::string>
getActiveUNL(AmendmentTable const& amendmentTable)
{
    // Check for UNL update amendments in reverse order (most recent first)
    if (amendmentTable.isEnabled(featureUNLUpdate2))
        return unlUpdate2List;

    if (amendmentTable.isEnabled(featureUNLUpdate1))
        return unlUpdate1List;

    // Default to initial UNL
    return initialValidatorsList;
}
```

**4. Add reload trigger in Change.cpp's applyAmendment():**
```cpp
// Check if this is a UNL update amendment and reload the validator list
if (amendment == featureUNLUpdate2 || amendment == featureUNLUpdate1)
    reloadUNL();
```

**Important**: Always check amendments in reverse chronological order (newest first).

## Implementation Status

**Status**: ✅ Fully implemented and ready to use

The UNL update system is complete:
- Amendment-based activation ✅
- Full list replacement ✅
- Automatic switchover when amendment enables ✅
- Logging and tracking ✅

## Related Files

- `include/xrpl/protocol/detail/features.macro` - Amendment definitions
- **`src/xrpld/core/UNLConfig.h`** - **Single source of truth for all UNL definitions**
- **`src/xrpld/core/UNLConfig.cpp`** - **UNL selection logic based on amendments**
- `src/xrpld/app/main/Application.cpp` - Uses UNLConfig for startup
- `src/xrpld/app/tx/detail/Change.cpp` - Uses UNLConfig for dynamic reload
- `src/xrpld/app/misc/ValidatorList.h` - UNL management
- `docs/AmendmentDevelopment.md` - General amendment process

## Complete Example Summary

Here's everything you need to add a new UNL update:

**File 1: `include/xrpl/protocol/detail/features.macro`**
```cpp
XRPL_FEATURE(UNLUpdate1, Supported::yes, VoteBehavior::DefaultNo)
```

**File 2: `src/xrpld/core/UNLConfig.h`** (define the validator list)
```cpp
inline std::vector<std::string> const unlUpdate1List = {
    "validator1",
    "validator2",
    // ... complete list
};
```

**File 3: `src/xrpld/core/UNLConfig.cpp`** (add amendment check)
```cpp
std::vector<std::string>
getActiveUNL(AmendmentTable const& amendmentTable)
{
    if (amendmentTable.isEnabled(featureUNLUpdate1))
        return unlUpdate1List;

    return initialValidatorsList;
}
```

**File 4: `src/xrpld/app/tx/detail/Change.cpp`** (add reload trigger)
```cpp
if (amendment == featureUNLUpdate1)
    reloadUNL();
```

**That's it!** The UNL list is defined in one place (UNLConfig.h) and used everywhere.

## References

- [Amendment Development Guide](./AmendmentDevelopment.md)
- Validator List Management
