# zuno

**zuno** — *Zero-trust Unalterable Notarized Object-store*

A lightweight, post-quantum secure blockchain written in C, designed to run on
**Raspberry Pi** and other embedded devices. The workflow is modeled after git:
stage transactions, commit a mined block, propose it to peers.

| Word | Property |
|---|---|
| **Zero-trust** | No central authority — every block cryptographically verified by every peer |
| **Unalterable** | Hash-chained immutable ledger — tampering breaks the chain |
| **Notarized** | Dilithium-3 post-quantum signatures on transactions and blocks |
| **Object-store** | Git-style `.chain/blocks/` content-addressed storage, one file per hash |

Named after Zuno, the omniscient being from *Dragon Ball Super* who holds the
answer to every question ever asked. A blockchain is the same: an immutable,
tamper-proof record of all truth — nothing forgotten, nothing alterable.

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
- [Live Demo](#live-demo)
- [Running Tests](#running-tests)
- [Project Layout](#project-layout)

---

## Features

- **Git-like workflow** — `send` queues a signed transaction, `mine` builds and seals a PoW block, `propose` broadcasts it
- **Post-quantum cryptography** — Dilithium-3 (transaction signatures), p256_kyber768 hybrid TLS key exchange
- **Dual consensus** — Proof-of-Work (SHA-256, configurable difficulty) and Proof-of-Stake (every 10th block)
- **Self-contained SHA-256** — FIPS 180-4 implementation with no OpenSSL dependency in the hot path
- **Git-style object store** — each block stored as a file named by its hash under `.chain/blocks/`
- **TLS 1.3 minimum** enforced on all peer connections; bidirectional close_notify on teardown
- **No global state, no `exit()` in library code** — clean C architecture (SOLID principles)
- **336 unit tests** across 20 test suites via CMocka

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│                   zuno CLI                      │  main.c — git-like subcommands
└──────────────────────┬──────────────────────────┘
                       │
          ┌────────────┼───────────┐
          │            │           │
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
              ┌────────┼───────┐
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
| `crypto.c` | Block hashing, Merkle root |
| `sha256.c` | Self-contained FIPS 180-4 SHA-256 (no OpenSSL) |
| `pow.c` | Proof-of-Work mining with midstate optimization |
| `pos.c` | Proof-of-Stake entry type (`PosEntry`) and stake-weighted selection |
| `network.c` | TLS 1.3 server/client, block serialization, broadcast, OQS provider |
| `transaction.c` | Transaction struct, Dilithium-3 sign and verify |
| `consensus.c` | `verify_consensus` — dispatches to PoW or PoS rules (ADR-003) |
| `validator.c` | Validator registry — identity, stake, disk persistence (ADR-017) |
| `vrf.c` | VRF leader election — slot message, prove, verify (ADR-018) |
| `key.c` | Dilithium-3 key store — generate, load pk/sk (`.chain/keys/`) |
| `mempool.c` | Signed transaction queue — add, load, count, purge (`.chain/mempool/`) |
| `equivocation.c` | Slot-based double-propose guard (`.chain/slots/`) |
| `log.c` | Level-gated logger — ERROR/WARN/INFO/DEBUG |
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
# from the parent of your zuno checkout
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
#   build/zuno           — optimized release binary
#   build/zuno-debug     — debug binary (-g -DDEBUG)
#   build/utils/           — demo utility binaries
```

Other build targets:

```sh
make test       # Build and run all 336 unit tests; print aggregated summary
make check      # Run tests then report line coverage (requires lcov)
make coverage   # Coverage report only
make clean      # Remove build/ and .chain/
make help       # List all targets
```

---

## Quick Start

```sh
# 1. Initialize the chain (creates genesis block)
./build/zuno init

# 2. Stage a transaction
./build/zuno send --from alice --to bob --amount 10.5

# 3. Mine a block containing the queued transactions
./build/zuno mine

# 4. Check the chain
./build/zuno log
./build/zuno status
```

---

## CLI Reference

```
Usage: zuno <command> [options]

Commands:
  init                             Initialise chain (creates genesis block)
  status                           Show chain tip and mempool
  log [--limit N]                  List recent blocks (default 10)
  show <hash>                      Show block details
  cat  <hash>                      Raw field dump of a block
  verify <hash>                    Verify a block's hash integrity
  send --from <s> --to <r> --amount <a>
                                   Sign and queue a transaction (mempool)
  mine                             Build a PoW block from mempool transactions
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

zuno uses a **send-then-mine** workflow. Transactions are signed with
Dilithium-3 and queued in `.chain/mempool/`. A `mine` command verifies
their signatures, bundles them into a PoW block, and appends it to the chain.

```sh
# Initialize the chain if starting fresh (also generates the genesis block)
./build/zuno init
# Initialised chain at .chain/
# Tip: block #0 (a3f8c2...)

# Generate Dilithium-3 keypairs (one-time; stored in .chain/keys/)
./build/utils/key_gen alice
./build/utils/key_gen carol

# Queue a signed transaction
./build/zuno send --from alice --to bob --amount 25.0
# Sent: alice -> bob  25.000000  (queued in mempool)

# Queue a second transaction for the same block
./build/zuno send --from carol --to dave --amount 7.5
# Sent: carol -> dave  7.500000  (queued in mempool)

# Check what is pending
./build/zuno status
# Chain tip:  block #0 (a3f8c2d1...)
# Mempool:    2 transaction(s) pending

# Mine: verify signatures, build a PoW block, append to chain
./build/zuno mine
# Mined block #1 (0000e4f2...)  [2 tx]

# Confirm
./build/zuno log
# block #1    0000e4f2...  ts=1700001234  txns=2
# block #0    a3f8c2d1...  ts=1700000000  txns=0
```

Up to **10 transactions** can be queued per block (`MAX_TRANSACTIONS`).
Queuing beyond that limit returns an error — mine the current batch first.

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

`zuno mine` mines automatically. For standalone PoW mining:

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
./build/zuno verify 0000e4f2c9a3b1d8e7f600000000000000000000000000000000000000001234
# OK    0000e4f2c9a3...
```

Returns exit code `0` for valid, `2` for tampered or not found.

#### Inspect block fields

```sh
# Formatted view
./build/zuno show <hash>
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
./build/zuno cat <hash>
```

#### Inspect the full chain

```sh
# List most recent 10 blocks
./build/zuno log

# List most recent 25 blocks
./build/zuno log --limit 25

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
./build/zuno propose
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
./build/zuno init
echo "192.168.1.20:8333" > .chain/peers

# Start TLS listener in background
./build/utils/start_chain 8333 node.crt node.key &

# Mine a block and broadcast it
./build/zuno send --from alice --to bob --amount 10
./build/zuno mine
./build/zuno propose
```

**Node B** (192.168.1.20) — starts its own chain:

```sh
./build/zuno init
echo "192.168.1.10:8333" > .chain/peers
./build/utils/start_chain 8333 node.crt node.key
# Receives the block broadcast by Node A, validates, and appends it
```

#### systemd service (Raspberry Pi)

```ini
# /etc/systemd/system/zuno.service
[Unit]
Description=zuno P2P node
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/mnt/ssd/chain
ExecStart=/usr/local/bin/start_chain 8333 tls-cert.pem tls-key.pem
Restart=on-failure
RestartSec=5
User=zuno

[Install]
WantedBy=multi-user.target
```

```sh
sudo systemctl enable --now zuno
sudo journalctl -u zuno -f    # follow logs
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
| `demo.sh` | `./build/utils/demo.sh` | Live interactive demo — miner + watcher loops, send transactions and watch them mine |
| `start_chain` | `start_chain <port> <cert> <key> [ca]` | TLS P2P server node; receives and validates blocks |
| `propose_block` | `propose_block <addr> <port> <cert> <key> [ca]` | Mine a block and broadcast it to a specific peer |
| `send_payment` | `send_payment <amount>` | Full Dilithium-3 sign-and-commit payment demo |
| `mine_block` | `mine_block [difficulty]` | PoW mining with timing and hash-rate output |
| `key_gen` | `key_gen <basename>` | Generate a Dilithium-3 keypair (`.pub` + `.key`) |
| `inspect_block` | `inspect_block <hash\|HEAD>` | Print all fields of a stored block |
| `inspect_chain` | `inspect_chain [page_size]` | Walk the chain from HEAD, print block summaries |

---

## Live Demo

`demo.sh` is a self-contained interactive test that shows a payment transaction
being submitted, mined, and verified in real time.

**Run from the project root** (not from inside `build/`):

```sh
make all
./build/utils/demo.sh
```

What it does:

1. Wipes any existing `.chain/` and initialises a fresh chain
2. Generates a Dilithium-3 keypair for `alice`
3. Starts a **miner loop** in the background — polls the mempool every second
   and mines a block whenever transactions are pending
4. Starts a **watcher loop** in the background — polls chain status every 2
   seconds and prints a line whenever the tip advances or the mempool changes

```
[16:29:00] init     initialising chain...
[16:29:01] init     generating keypair for 'alice'...
[16:29:01] init     ready.
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Miner and watcher are running.

  In another terminal, send transactions:
    ./build/zuno-debug send --from alice --to bob   --amount 10
    ./build/zuno-debug send --from alice --to carol --amount 25
    ./build/zuno-debug send --from alice --to dave  --amount 5

  Or verify a mined block:
    ./build/zuno-debug log
    ./build/zuno-debug verify <hash>

  Press Ctrl+C to stop and clean up.
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

[16:29:03] chain    tip=0 a3f8c2...  mempool=0 transactions
```

Then in a second terminal:

```sh
./build/zuno-debug send --from alice --to bob --amount 10
```

The watcher and miner respond within ~2 seconds:

```
[16:29:05] chain    tip=0 a3f8c2...  mempool=1 transactions
[16:29:06] miner    Mined block #1 (0000e6...)  [1 tx]
[16:29:07] chain    tip=1 0000e6...  mempool=0 transactions
```

Press **Ctrl+C** to stop. The demo cleans up `.chain/` automatically on exit.

---

## Running Tests

```sh
make test
```

Example output:

```
  Suite                                    Passed  Failed   Total
  ──────────────────────────────────────────────────────────────
  test_block                                   23       0      23
  test_chain                                   24       0      24
  test_consensus                               12       0      12
  test_crypto                                  20       0      20
  test_equivocation                            10       0      10
  test_integration_e2e                          6       0       6
  test_integration_miner                        9       0       9
  test_integration_payment                      8       0       8
  test_key                                     18       0      18
  test_log                                      8       0       8
  test_main                                    28       0      28
  test_mempool                                 15       0      15
  test_network                                 28       0      28
  test_pos                                     13       0      13
  test_pow                                     13       0      13
  test_sha256                                  19       0      19
  test_storage                                 29       0      29
  test_transaction                             15       0      15
  test_validator                               21       0      21
  test_vrf                                     17       0      17
  ──────────────────────────────────────────────────────────────
  TOTAL                                       336       0     336
```

---

## Project Layout

```
zuno/
├── src/                    C source files
│   ├── main.c              CLI dispatcher (init, send, mine, propose, …)
│   ├── chain.c             In-memory chain with pool allocator
│   ├── block.c             Block create / hash / verify / sign
│   ├── storage.c           Git-style .chain/ object store + Merkle re-verify on read
│   ├── network.c           TLS 1.3 server, client, block serialization, GETBODY protocol
│   ├── consensus.c         Consensus dispatcher (PoW vs PoS, equivocation guard)
│   ├── equivocation.c      Slot-based equivocation guard (.chain/slots/)
│   ├── pow.c               Proof-of-Work mining (midstate optimization)
│   ├── pos.c               Proof-of-Stake validator registry and selection
│   ├── crypto.c            Block hashing, Merkle root
│   ├── sha256.c            Self-contained FIPS 180-4 SHA-256
│   ├── transaction.c       Dilithium-3 sign and verify
│   ├── validator.c         Validator registry (stake, public key, lookup)
│   ├── vrf.c               VRF leader election (slot message, prove, verify)
│   ├── key.c               Dilithium-3 key store (.chain/keys/)
│   ├── mempool.c           Signed transaction queue (.chain/mempool/)
│   └── log.c               Level-gated logger (ERROR/WARN/INFO/DEBUG)
├── inc/                    Public headers
├── tests/                  CMocka unit tests (one file per module)
├── utils/                  Standalone utility binaries (built to build/utils/)
│   ├── demo.sh             Live interactive demo — miner + watcher loops
│   ├── start_chain.c       P2P server node
│   ├── propose_block.c     Mine and broadcast a block
│   ├── send_payment.c      Dilithium-3 payment demo
│   ├── mine_block.c        PoW mining demo with timing
│   ├── key_gen.c           Dilithium-3 keypair generation
│   ├── inspect_block.c     Block field inspector
│   └── inspect_chain.c     Chain walker
├── doc/
│   └── NOTES.md            Architecture decision records (ADR-001 – ADR-019)
├── Makefile
└── README.md
```

### Chain storage layout

```
.chain/
├── HEAD              — hash of the current tip block
├── peers             — peer list for propose (one ip:port per line)
├── tls-cert.pem      — node TLS certificate
├── tls-key.pem       — node TLS private key (chmod 600)
├── blocks/           — content-addressed block store (one file per hash)
│   ├── 00e4f2…
│   └── a3f8c2…
├── keys/             — Dilithium-3 keypairs (key_gen writes here)
│   ├── alice.pk      — public key  (chmod 644)
│   └── alice.sk      — secret key  (chmod 600)
├── mempool/          — pending signed transactions (one file per tx hash)
└── slots/            — equivocation guard (one file per PoS proposer)
    └── alice         — binary uint32_t: last committed slot for alice
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
