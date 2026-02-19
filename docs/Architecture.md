# PostFiat Daemon Architecture

## Overview

`postfiatd` is the PostFiat Ledger (PFT Ledger) node daemon, forked from the XRP Ledger (`rippled`). Written in C++20, it implements a federated consensus protocol for a permissioned blockchain with features specific to PostFiat: loans, vaults, exclusions, a permissioned DEX, and validator vote tracking.

## Repository Layout

```
postfiatd/
├── include/xrpl/       # Public headers (protocol, basics, crypto, ledger, server)
├── src/
│   ├── libxrpl/        # Core library (basics, crypto, json, ledger, protocol, server)
│   ├── xrpld/          # Daemon application
│   │   ├── app/        # Application logic (consensus, ledger, misc, tx, main)
│   │   ├── consensus/  # Consensus implementation
│   │   ├── core/       # Core components (job queue, config, workers)
│   │   ├── nodestore/  # Key-value store for ledger objects
│   │   ├── overlay/    # Peer-to-peer network layer
│   │   ├── peerfinder/ # Peer discovery
│   │   ├── perflog/    # Performance logging
│   │   ├── rpc/        # JSON-RPC handlers
│   │   ├── shamap/     # Merkle tree (SHAMap) implementation
│   │   └── unity/      # Unity build files
│   └── test/           # Unit tests
├── cfg/                # Network configs (devnet, testnet, mainnet)
├── cmake/              # CMake build config
├── conan/              # Conan package manager
└── docs/               # Documentation
```

## Log Module to Source Mapping

| Log Module | Primary Source Location |
|---|---|
| `LedgerConsensus` | `src/xrpld/consensus/`, `src/xrpld/app/consensus/` |
| `ConsensusTransacting` | `src/xrpld/consensus/` |
| `NetworkOPs` | `src/xrpld/app/misc/NetworkOPs.cpp` |
| `Application` | `src/xrpld/app/main/Application.cpp` |
| `Overlay` | `src/xrpld/overlay/` |
| `Peer` | `src/xrpld/overlay/PeerImp.cpp` |
| `NodeStore` | `src/xrpld/nodestore/` |
| `SHAMap` | `src/xrpld/shamap/` |
| `SHAMapStore` | `src/xrpld/shamap/` |
| `Amendments` | `src/xrpld/app/misc/AmendmentTable.cpp` |
| `Transactor` | `src/xrpld/app/tx/` |
| `ValidatorList` | `src/xrpld/app/misc/ValidatorList.cpp` |
| `ValidatorSite` | `src/xrpld/app/misc/ValidatorSite.cpp` |
| `LedgerMaster` | `src/xrpld/app/ledger/LedgerMaster.cpp` |
| `LedgerCleaner` | `src/xrpld/app/ledger/LedgerCleaner.cpp` |
| `InboundLedger` | `src/xrpld/app/ledger/InboundLedger.cpp` |
| `OrderBookDB` | `src/xrpld/app/misc/OrderBookDB.cpp` |
| `LoadManager` | `src/xrpld/app/misc/LoadManager.cpp` |
| `PeerFinder` | `src/xrpld/peerfinder/` |
| `JobQueue` | `src/xrpld/core/JobQueue.cpp` |
| `RPCHandler` | `src/xrpld/rpc/` |
| `Exclusions` | `src/xrpld/app/misc/` |
| `Loans` | `src/xrpld/app/tx/` |
| `Vaults` | `src/xrpld/app/tx/` |
| `PermissionedDEX` | `src/xrpld/app/tx/` |

## Key Subsystems

### Consensus

The federated consensus protocol runs in `src/xrpld/consensus/`. `LedgerConsensus` drives proposal rounds, `ConsensusTransacting` applies agreed transaction sets. The algorithm is UNL-based (Unique Node List) — validators vote on transaction sets each round.

### Overlay (P2P Network)

`src/xrpld/overlay/` manages peer connections, message routing, and protocol negotiation. `PeerImp` handles individual peer sessions. `PeerFinder` in `src/xrpld/peerfinder/` discovers and ranks peers.

### Ledger Management

`LedgerMaster` (`src/xrpld/app/ledger/`) tracks the current validated ledger and manages ledger acquisition. `InboundLedger` fetches missing ledgers from peers. `SHAMap` provides the Merkle tree structure for ledger state.

### Transaction Processing

`src/xrpld/app/tx/` contains transaction processors (`Transactor` subclasses). Each transaction type (payment, offer, trust line, PFT-specific) has its own implementation. `NetworkOPs` coordinates transaction submission and validation.

### Node Store

`src/xrpld/nodestore/` provides persistent storage for SHAMap tree nodes. Supports multiple backends (RocksDB, NuDB). `SHAMapStore` manages the node store lifecycle and online delete.

### RPC

`src/xrpld/rpc/` handles JSON-RPC requests from clients. Handlers are registered in `src/xrpld/rpc/handlers/`.

## PFT-Specific Features

PostFiat extends the base XRPL with:

- **Loans**: Collateralized lending between accounts
- **Vaults**: Pooled asset management
- **Exclusions**: Account-level restrictions
- **Permissioned DEX**: Order book with access controls
- **Validator Vote Tracking**: On-ledger recording of validator votes

These are implemented as custom transaction types in `src/xrpld/app/tx/` with corresponding ledger objects.

## Coding Conventions

### Logging

All logging uses the `JLOG` macro defined in `include/xrpl/basics/Log.h`:

```cpp
JLOG(journal_.warn()) << "Something went wrong: " << detail;
JLOG(journal_.info()) << "Processing ledger " << seq;
JLOG(journal_.fatal()) << "Critical failure in " << __func__;
```

`JLOG` is a stream-guard that avoids evaluating arguments when the log level is disabled.

### Style

- C++20 standard (`std::span`, concepts, ranges, `[[nodiscard]]`)
- RAII for resource management
- `beast::Journal` for scoped logging
- `std::optional`, `Expected` for error handling
- Namespaces: `ripple::` (legacy), code under `src/xrpld/`

## Common Error Patterns

| Pattern | Typical Cause |
|---|---|
| SHAMap missing node | Node store compaction or network partition during ledger sync |
| Peer disconnections | Network instability, protocol version mismatch, or timeout |
| Consensus timeout | Insufficient validator agreement within the round window |
| NodeStore errors | Disk I/O issues, corrupted database, or backend failures |
| Amendment blocked | Majority of validators haven't enabled a required amendment |
| Ledger acquisition failure | Peers don't have the requested ledger history |

## Build and Test

```bash
# Dependencies via Conan
conan install . --output-folder build --build=missing

# Build with CMake
cmake --preset default
cmake --build build

# Run unit tests
./build/postfiatd --unittest

# Run specific test suite
./build/postfiatd --unittest=LedgerMaster
```
