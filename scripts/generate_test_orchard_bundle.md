# Generating Test Orchard Bundles

For now, we'll use a simpler approach: pre-generated test bundles from Zcash test vectors.

## Option 1: Use Empty Bundle for Initial Tests

For the first basic test, we can use an empty bundle:
```cpp
// Empty bundle = no actions
Blob emptyBundle{0};  // Just one byte: nActionsOrchard = 0
```

## Option 2: Extract from Zcash Test Vectors

Zcash provides test vectors with real bundles. We can extract these for testing.

## Option 3: Build Bundle Generation Later

Once we verify the node processes bundles correctly, we can add bundle generation
as a separate wallet/testing feature.

## For Now: Start with Empty Bundle Tests

The C++ test should start simple and test the flow without requiring bundle generation.
