# QuteChain

A lightweight, post-quantum secure blockchain written in C, designed to run on
**Raspberry Pi** and other embedded devices. The workflow is modeled after git:
stage transactions, commit a mined block, propose it to peers.

---

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Prerequisites](#prerequisites)
- [Building](#building)
- [Quick Start](#quick-start)
- [CLI Reference](#cli-reference)
- [Step-by-Step Guides](#step-by-step-guides)
  - [Exchanging Money](#1-exchanging-money)
  - [Mining a Block](#2-mining-a-block)
  - [Verifying Blocks](#3-verifying-blocks)
  - [Running a Network Node](#4-running-a-network-node)
  - [Multi-Node Setup](#5-multi-node-setup)
- [Utility Binaries](#utility-binaries)
- [Running Tests](#running-tests)
- [Project Layout](#project-layout)

---

## Features

- **Git-like workflow** — `send` stages transactions, `commit` mines and seals a block, `propose` broadcasts it
- **Post-quantum cryptography** — Dilithium-3 (transaction signatures), p256_kyber768 hybrid TLS key exchange
- **Dual consensus** — Proof-of-Work (SHA-256, configurable difficulty) and Proof-of-Stake (every 10th block)
- **Self-contained SHA-256** — FIPS 180-4 implementation with no OpenSSL dependency in the hot path
- **Git-style object store** — each block stored as a file named by its hash under `.chain/blocks/`
- **TLS 1.3 minimum** enforced on all peer connections; bidirectional close_notify on teardown
- **No global state, no `exit()` in library code** — clean C architecture (SOLID principles)
- **213 unit tests** across 14 test suites via CMocka

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│                   blocky CLI                    │  main.c — git-like subcommands
└──────────────────────┬──────────────────────────┘
                       │
          ┌────────────┼────────────┐
          │            │            │
    ┌─────▼─────┐ ┌────▼────┐ ┌────▼──────┐
    │  chain.c  │ │ pow.c   │ │network.c  │
    │  pool     │ │ mining  │ │ TLS P2P   │
    │  validate │ │ verify  │ │ broadcast │
    │  propose  │ └────┬────┘ └────┬──────┘
    └─────┬─────┘      │           │
          │        ┌───▼───┐  ┌────▼──────┐
    ┌─────▼─────┐  │block.c│  │ pos.c     │
    │ storage.c │  │create │  │ stake     │
    │ .chain/   │  │hash   │  │ select    │
    │ HEAD ref  │  │verify │  │ validate  │
    └───────────┘  └───────┘  └───────────┘
                       │
              ┌────────┼────────┐
         ┌────▼───┐ ┌──▼───┐ ┌─▼──────────┐
         │sha256.c│ │crypto│ │transaction │
         │FIPS    │ │merkle│ │Dilithium-3 │
         │180-4   │ │root  │ │sign/verify │
         └────────┘ └──────┘ └────────────┘
```

### Module responsibilities

| Module | Responsibility |
|---|---|
| `chain.c` | In-memory chain tip, pool allocator, validate, add, propose |
| `storage.c` | Git-style object store — `.chain/blocks/<hash>`, HEAD ref |
| `block.c` | Block struct, `block_create`, `block_compute_hash`, `block_verify_hash` |
| `crypto.c` | Block hashing, Merkle root, Dilithium sign/verify stubs |
| `sha256.c` | Self-contained FIPS 180-4 SHA-256 (no OpenSSL) |
| `pow.c` | Proof-of-Work mining with midstate optimization |
| `pos.c` | Proof-of-Stake validator registry, stake-weighted selection |
| `network.c` | TLS 1.3 server/client, block serialization, broadcast |
| `transaction.c` | Transaction struct, Dilithium-3 sign and verify |
| `consensus.c` | `verify_consensus` — dispatches to PoW or PoS based on block field |
| `main.c` | CLI dispatcher — all user-facing subcommands |

---

## Prerequisites

### Development

```sh
brew install gcc make openssl cmocka
```

### Raspberry Pi OS / Debian

```sh
sudo apt update
sudo apt install build-essential libssl-dev libcmocka-dev
```

### liboqs (post-quantum cryptography)

liboqs is built in-tree alongside the project:

```sh
# from the parent of your blocky checkout
mkdir -p libs && cd libs
git clone --depth 1 https://github.com/open-quantum-safe/liboqs.git
cd liboqs
cmake -B build -DBUILD_SHARED_LIBS=OFF -DOQS_BUILD_ONLY_LIB=ON
cmake --build build --parallel
```

The Makefile expects liboqs at `../libs/liboqs/build/` relative to the
project root.

---

## Building

```sh
# Build release binary, debug binary, and all utility binaries
make all

# Outputs:
#   build/blocky           — optimized release binary
#   build/blocky-debug     — debug binary (-g -DDEBUG)
#   build/utils/           — demo utility binaries
```

Other build targets:

```sh
make test       # Build and run all 213 unit tests; print aggregated summary
make check      # Run tests then report line coverage (requires lcov)
make coverage   # Coverage report only
make clean      # Remove build/ and .chain/
make help       # List all targets
```

---

## Quick Start

```sh
# 1. Initialize the chain (creates genesis block)
./build/blocky init

# 2. Stage a transaction
./build/blocky send --from alice --to bob --amount 10.5

# 3. Mine a block containing the staged transactions
./build/blocky commit

# 4. Check the chain
./build/blocky log
./build/blocky status
```

---

## CLI Reference

```
Usage: blocky <command> [options]

Commands:
  init                             Initialise chain (creates genesis block)
  status                           Show chain tip and staged transactions
  log [--limit N]                  List recent blocks (default 10)
  show <hash>                      Show block details
  cat  <hash>                      Raw field dump of a block
  verify <hash>                    Verify a block's hash integrity
  send --from <s> --to <r> --amount <a>
                                   Stage a transaction
  commit                           Build a block from staged transactions
  propose                          Broadcast tip to peers via .chain/peers
  version                          Print version
  help [command]                   Show this help or per-command help
```

**Amounts** are decimal tokens (e.g. `10.5`). Internally stored as
micro-units (1 token = 1,000,000 micro-units) to avoid floating-point
arithmetic in the core.

---

## Step-by-Step Guides

### 1. Exchanging Money

QuteChain uses a **stage-then-commit** workflow. Transactions are staged in
`.chain/STAGED` (a plain text file) and sealed into a block on `commit`.

```sh
# Initialize the chain if starting fresh
./build/blocky init
# Initialised chain at .chain/
# Tip: block #0 (a3f8c2...)

# Stage one transaction
./build/blocky send --from alice --to bob --amount 25.0
# Staged: alice -> bob  25.000000

# Stage a second transaction in the same block
./build/blocky send --from carol --to dave --amount 7.5
# Staged: carol -> dave  7.500000

# Check what is pending
./build/blocky status
# Chain tip:  block #0 (a3f8c2d1...)
# Staged:     2 transaction(s) pending

# Commit: mines a PoW block and appends it to the chain
./build/blocky commit
# Committed block #1 (0000e4f2...)

# Confirm
./build/blocky log
# block #1    0000e4f2...  ts=1700001234  txns=2
# block #0    a3f8c2d1...  ts=1700000000  txns=0
```

Up to **10 transactions** can be staged per block (`MAX_TRANSACTIONS`).
Staging beyond that limit returns an error — commit the current batch first.

#### With Dilithium-3 signatures (programmatic)

The `send_payment` utility shows the full cryptographic flow:

```sh
# Requires a chain initialized in the current directory
./build/utils/send_payment 50

# [keygen] Dilithium-3 keypairs ready.
# [tx]     50 tokens (50000000 micro)  nonce=1700001234
# [sign]   signature_length=3293 bytes
# [verify] Signature OK.
# [mine]   difficulty=4 ...
# [mine]   block #1  hash=0000c3a7...
# [chain]  Block committed to local chain.
```

To generate persistent key files for use in production:

```sh
./build/utils/key_gen alice
# [keygen] Public key  -> alice.pub (1952 bytes)
# [keygen] Private key -> alice.key (4000 bytes)
# [keygen] IMPORTANT: chmod 600 alice.key

chmod 600 alice.key
```

---

### 2. Mining a Block

`blocky commit` mines automatically. For standalone PoW mining:

```sh
# Mine an empty block at default difficulty (4 leading hex zeros)
./build/utils/mine_block

# [mine]   Block #1  difficulty=4
# [mine]   prev=a3f8c2d1e5b6f790...
# [mine]   Solved!  nonce=183241  hash=00009f3a...
# [mine]   Elapsed: 0.412 s  (~444924 H/s)
# [chain]  Block committed to local chain.
```

Adjust difficulty (number of leading hex zeros required):

```sh
./build/utils/mine_block 3    # faster  — good for testing
./build/utils/mine_block 5    # slower  — ~16× harder than 4
```

The PoW implementation uses a **midstate SHA-256 optimization** that
pre-computes the hash of all invariant fields once per block, then only
re-hashes the nonce on each iteration.

---

### 3. Verifying Blocks

#### Verify hash integrity of a specific block

```sh
./build/blocky verify 0000e4f2c9a3b1d8e7f600000000000000000000000000000000000000001234
# OK    0000e4f2c9a3...
```

Returns exit code `0` for valid, `2` for tampered or not found.

#### Inspect block fields

```sh
# Formatted view
./build/blocky show <hash>
# block #1
#   hash:      0000e4f2...
#   prev:      a3f8c2d1...
#   merkle:    7b3c9a12...
#   timestamp: 1700001234
#   nonce:     183241
#   consensus: 0 (PoW)
#   txns:      2
#     [0] alice -> bob  25.000000
#     [1] carol -> dave  7.500000

# Raw field dump (machine-readable)
./build/blocky cat <hash>
```

#### Inspect the full chain

```sh
# List most recent 10 blocks
./build/blocky log

# List most recent 25 blocks
./build/blocky log --limit 25

# Inspect via utility (with page size)
./build/utils/inspect_chain       # last 10 blocks
./build/utils/inspect_chain 5     # last 5 blocks

# Inspect a specific block by hash or HEAD
./build/utils/inspect_block <hash>
./build/utils/inspect_block HEAD
```

---

### 4. Running a Network Node

Each node runs a TLS server that accepts incoming blocks from peers,
validates them, and appends valid blocks to its local chain.

#### Generate a self-signed TLS certificate

```sh
# Generate a 2048-bit RSA key and self-signed certificate valid for 1 year
openssl req -x509 -newkey rsa:2048 -days 365 -nodes \
  -keyout node.key -out node.crt \
  -subj "/CN=qutechain-node"

chmod 600 node.key
```

#### Start a listener node

```sh
# Listens on port 8333, uses node.crt/node.key for TLS
./build/utils/start_chain 8333 node.crt node.key

# [node]   TLS server starting on port 8333
# [node]   Chain tip: block #0
# [node]   Press Ctrl-C to stop.
```

With optional mutual TLS (verify connecting peers against a CA):

```sh
./build/utils/start_chain 8333 node.crt node.key ca.crt
```

#### Broadcast the current tip to peers

```sh
# Using the CLI (reads .chain/peers for peer list)
./build/blocky propose
# Proposed block #3 (0000a1b2...)

# Using the utility (direct peer address)
./build/utils/propose_block 192.168.1.20 8333 node.crt node.key ca.crt
# [mine]   Mining block #4 (difficulty=4)...
# [mine]   hash=00007f3a...
# [net]    Sending to 192.168.1.20:8333 ...
# [net]    Block broadcast successful.
# [chain]  Block committed to local chain.
```

---

### 5. Multi-Node Setup

#### Configure peers

Each node reads `.chain/peers` to know who to broadcast to. One
`ip:port` entry per line:

```sh
mkdir -p .chain
cat > .chain/peers << 'EOF'
192.168.1.20:8333
192.168.1.21:8333
EOF
```

#### Two-node example (both on the same LAN)

**Node A** (192.168.1.10):

```sh
# Start fresh
./build/blocky init
echo "192.168.1.20:8333" > .chain/peers

# Start TLS listener in background
./build/utils/start_chain 8333 node.crt node.key &

# Mine a block and broadcast it
./build/blocky send --from alice --to bob --amount 10
./build/blocky commit
./build/blocky propose
```

**Node B** (192.168.1.20) — starts its own chain:

```sh
./build/blocky init
echo "192.168.1.10:8333" > .chain/peers
./build/utils/start_chain 8333 node.crt node.key
# Receives the block broadcast by Node A, validates, and appends it
```

#### systemd service (Raspberry Pi)

```ini
# /etc/systemd/system/blocky.service
[Unit]
Description=QuteChain P2P node
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/mnt/ssd/chain
ExecStart=/usr/local/bin/start_chain 8333 tls-cert.pem tls-key.pem
Restart=on-failure
RestartSec=5
User=blocky

[Install]
WantedBy=multi-user.target
```

```sh
sudo systemctl enable --now blocky
sudo journalctl -u blocky -f    # follow logs
```

> **Storage note:** Always run the chain on a **USB SSD**, never on the
> microSD card. MicroSD cards have limited write cycles (~10k) and will fail
> under `.chain/` write load. Symlink or bind-mount `.chain/` to the SSD
> from day one.

---

## Utility Binaries

All built to `build/utils/` by `make all`.

| Binary | Usage | Description |
|---|---|---|
| `start_chain` | `start_chain <port> <cert> <key> [ca]` | TLS P2P server node; receives and validates blocks |
| `propose_block` | `propose_block <addr> <port> <cert> <key> [ca]` | Mine a block and broadcast it to a specific peer |
| `send_payment` | `send_payment <amount>` | Full Dilithium-3 sign-and-commit payment demo |
| `mine_block` | `mine_block [difficulty]` | PoW mining with timing and hash-rate output |
| `key_gen` | `key_gen <basename>` | Generate a Dilithium-3 keypair (`.pub` + `.key`) |
| `inspect_block` | `inspect_block <hash\|HEAD>` | Print all fields of a stored block |
| `inspect_chain` | `inspect_chain [page_size]` | Walk the chain from HEAD, print block summaries |

---

## Running Tests

```sh
make test
```

Example output:

```
  Suite                                    Passed  Failed   Total
  ──────────────────────────────────────────────────────────────
  test_block                                   12       0      12
  test_chain                                   18       0      18
  test_consensus                                8       0       8
  test_crypto                                  20       0      20
  test_integration_miner                        9       0       9
  test_integration_payment                      6       0       6
  test_log                                      8       0       8
  test_main                                    23       0      23
  test_network                                 25       0      25
  test_pos                                     13       0      13
  test_pow                                     13       0      13
  test_sha256                                  16       0      16
  test_storage                                 26       0      26
  test_transaction                             15       0      15
  ──────────────────────────────────────────────────────────────
  TOTAL                                       213       0     213
```

---

## Project Layout

```
blocky/
├── src/                    C source files
│   ├── main.c              CLI dispatcher (init, send, commit, propose, …)
│   ├── chain.c             In-memory chain with pool allocator
│   ├── block.c             Block create / hash / verify
│   ├── storage.c           Git-style .chain/ object store
│   ├── network.c           TLS 1.3 server, client, block serialization
│   ├── pow.c               Proof-of-Work mining (midstate optimization)
│   ├── pos.c               Proof-of-Stake validator registry and selection
│   ├── consensus.c         Consensus dispatcher (PoW vs PoS)
│   ├── crypto.c            Block hashing, Merkle root
│   ├── sha256.c            Self-contained FIPS 180-4 SHA-256
│   ├── transaction.c       Dilithium-3 sign and verify
│   └── log.c               Level-gated logger (ERROR/WARN/INFO/DEBUG)
├── inc/                    Public headers
├── tests/                  CMocka unit tests (one file per module)
├── utils/                  Standalone demo binaries
│   ├── start_chain.c       P2P server node
│   ├── propose_block.c     Mine and broadcast a block
│   ├── send_payment.c      Dilithium-3 payment demo
│   ├── mine_block.c        PoW mining demo with timing
│   ├── key_gen.c           Dilithium-3 keypair generation
│   ├── inspect_block.c     Block field inspector
│   └── inspect_chain.c     Chain walker
├── doc/
│   └── NOTES.md            Architecture decision records (ADR-001 – ADR-016)
├── Makefile
└── README.md
```

### Chain storage layout

```
.chain/
├── HEAD              — hash of the current tip block
├── STAGED            — staged transactions (tab-separated, one per line)
├── peers             — peer list for propose (one ip:port per line)
├── tls-cert.pem      — node TLS certificate
├── tls-key.pem       — node TLS private key (chmod 600)
└── blocks/
    ├── 00/           — first two hex chars of hash
    │   └── 00e4f2…   — full block serialized to binary
    └── a3/
        └── a3f8c2…
```

---

## Security Notes

- **TLS 1.3 minimum** — older protocol versions are rejected at the socket level.
- **PQC key exchange** — set `pqc_group = NET_PQC_GROUP` (`p256_kyber768`) in
  `NetConfig` once all peers have the OQS OpenSSL provider loaded.
- **Dilithium-3** — transaction signatures use 3293-byte signatures and
  1952-byte public keys (NIST security level 3).
- **Private key hygiene** — `key_gen` zeroes private key memory with
  `OQS_MEM_cleanse` before exit. Do the same in any application code.
- **No exit() in library code** — all modules return error codes; the caller
  decides what to do on failure.

---

## Contributing

Architecture decisions are recorded as ADRs in `doc/NOTES.md`. Read them
before proposing changes to the core modules.

Every `.h`/`.c` unit must have a corresponding `tests/test_<unit>.c` using
CMocka. `make test` must pass before any change is considered complete.
