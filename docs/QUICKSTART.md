# zuno — Quick Start Guide

---

## Prerequisites

### macOS (Development)

```sh
brew install gcc make openssl cmocka
```

### Raspberry Pi OS / Debian

```sh
sudo apt update
sudo apt install build-essential libssl-dev libcmocka-dev
```

### liboqs (Post-Quantum Cryptography)

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
#   build/utils/         — demo and utility binaries
```

Other build targets:

```sh
make test       # Build and run all unit tests; print aggregated summary
make check      # Run tests then report line coverage (requires lcov)
make coverage   # Coverage report only (build/cov/html/index.html)
make clean      # Remove build/ and .chain/
make help       # List all targets
```

---

## Quick Start

```sh
# 1. Initialize the chain (creates genesis block)
./build/zuno init

# 2. Generate a keypair for a sender
./build/utils/key_gen alice

# 3. Stage a transaction
./build/zuno send --from alice --to bob --amount 10.5

# 4. Mine a block containing the queued transactions
./build/zuno mine

# 5. Inspect the chain
./build/zuno log
./build/zuno status
```

---

## Step-by-Step Guides

### 1. Exchanging Tokens

zuno uses a **send-then-mine** workflow. Transactions are signed with
Dilithium-3 and queued in `.chain/mempool/`. The `mine` command verifies
signatures, bundles them into a PoW block, and appends it to the chain.

```sh
# Initialize the chain
./build/zuno init
# Initialised chain at .chain/
# Tip: block #0 (a3f8c2...)

# Generate Dilithium-3 keypairs (stored in .chain/keys/)
./build/utils/key_gen alice
./build/utils/key_gen carol

# Queue signed transactions
./build/zuno send --from alice --to bob --amount 25.0
./build/zuno send --from carol --to dave --amount 7.5

# Check what is pending
./build/zuno status
# Chain tip:  block #0 (a3f8c2d1...)
# Mempool:    2 transaction(s) pending

# Mine: verify signatures, build PoW block, append to chain
./build/zuno mine
# Mined block #1 (0000e4f2...)  [2 tx]

# Confirm
./build/zuno log
# block #1    0000e4f2...  ts=1700001234  txns=2
# block #0    a3f8c2d1...  ts=1700000000  txns=0
```

Up to **10 transactions** can be queued per block (`MAX_TRANSACTIONS`). Mining
the current batch before queuing more is required beyond this limit.

#### Full Dilithium-3 Signature Demo

The `send_payment` utility demonstrates the complete cryptographic flow:

```sh
./build/utils/send_payment 50

# [keygen] Dilithium-3 keypairs ready.
# [tx]     50 tokens (50000000 micro)  nonce=1700001234
# [sign]   signature_length=3293 bytes
# [verify] Signature OK.
# [mine]   difficulty=4 ...
# [mine]   block #1  hash=0000c3a7...
# [chain]  Block committed to local chain.
```

---

### 2. Mining a Block

`zuno mine` mines automatically from the mempool. For standalone PoW mining:

```sh
# Mine an empty block at default difficulty (4 leading hex zeros)
./build/utils/mine_block

# [mine]   Block #1  difficulty=4
# [mine]   prev=a3f8c2d1e5b6f790...
# [mine]   Solved!  nonce=183241  hash=00009f3a...
# [mine]   Elapsed: 0.412 s  (~444924 H/s)

# Adjust difficulty
./build/utils/mine_block 3    # faster  — good for testing
./build/utils/mine_block 5    # slower  — ~16× harder than 4
```

Mining uses a **midstate SHA-256 optimization** that pre-computes the hash of
all invariant fields once per block, then only re-hashes the nonce on each
iteration.

---

### 3. Verifying Blocks

```sh
# Verify hash integrity of a specific block
./build/zuno verify 0000e4f2c9a3b1d8e7f600000000000000000000000000000000000000001234
# OK    0000e4f2c9a3...

# Inspect block fields (human-readable)
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

# Walk the full chain
./build/zuno log              # last 10 blocks
./build/zuno log --limit 25   # last 25 blocks

# Inspect via utility
./build/utils/inspect_chain       # last 10 blocks
./build/utils/inspect_block HEAD  # current tip detail
```

`verify` returns exit code `0` for valid, `2` for tampered or not found.

---

### 4. Running a Network Node

Each node runs a TLS server that accepts incoming blocks from peers, validates
them, and appends valid blocks to its local chain.

#### Generate a Self-Signed TLS Certificate

```sh
openssl req -x509 -newkey rsa:2048 -days 365 -nodes \
  -keyout node.key -out node.crt \
  -subj "/CN=zuno-node"

chmod 600 node.key
```

#### Start a Listener Node

```sh
# Listen on port 8333
./build/utils/start_chain 8333 node.crt node.key

# With mutual TLS (verify connecting peers against a CA)
./build/utils/start_chain 8333 node.crt node.key ca.crt
```

#### Broadcast the Current Tip to Peers

```sh
# Using the CLI (reads .chain/peers for the peer list)
./build/zuno propose
# Proposed block #3 (0000a1b2...)

# Using the utility (direct peer address)
./build/utils/propose_block 192.168.1.20 8333 node.crt node.key ca.crt
```

---

### 5. Multi-Node Setup

#### Configure Peers

Each node reads `.chain/peers` — one `ip:port` per line:

```sh
cat > .chain/peers << 'EOF'
192.168.1.20:8333
192.168.1.21:8333
EOF
```

#### Two-Node Example

**Node A** (192.168.1.10):

```sh
./build/zuno init
echo "192.168.1.20:8333" > .chain/peers
./build/utils/start_chain 8333 node.crt node.key &

./build/zuno send --from alice --to bob --amount 10
./build/zuno mine
./build/zuno propose
```

**Node B** (192.168.1.20):

```sh
./build/zuno init
echo "192.168.1.10:8333" > .chain/peers
./build/utils/start_chain 8333 node.crt node.key
# Receives the block from Node A, validates, and appends it
```

#### systemd Service (Raspberry Pi)

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
ProtectSystem=full
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

```sh
sudo systemctl enable --now zuno
sudo journalctl -u zuno -f    # follow logs
```

> **Storage note:** Always run the chain on a **USB SSD**, not the microSD
> card. MicroSD cards have ~10k write cycles and will fail under `.chain/`
> write load. Symlink or bind-mount `.chain/` to the SSD from day one.

---

## Utility Binaries

All built to `build/utils/` by `make all`.

| Binary | Usage | Description |
|---|---|---|
| `demo.sh` | `./build/utils/demo.sh` | Live interactive demo — miner + watcher loops |
| `start_chain` | `start_chain <port> <cert> <key> [ca]` | TLS P2P server node |
| `propose_block` | `propose_block <addr> <port> <cert> <key> [ca]` | Mine a block and broadcast to a specific peer |
| `send_payment` | `send_payment <amount>` | Full Dilithium-3 sign-and-commit demo |
| `mine_block` | `mine_block [difficulty]` | PoW mining with timing and hash-rate output |
| `key_gen` | `key_gen <basename>` | Generate a Dilithium-3 keypair (`.pk` + `.sk`) |
| `inspect_block` | `inspect_block <hash\|HEAD>` | Print all fields of a stored block |
| `inspect_chain` | `inspect_chain [page_size]` | Walk the chain from HEAD, print summaries |

---

## Live Demo

`demo.sh` is a self-contained interactive session showing a payment transaction
being submitted, mined, and verified in real time.

```sh
make all
./build/utils/demo.sh
```

What it does:

1. Wipes any existing `.chain/` and initialises a fresh chain
2. Generates a Dilithium-3 keypair for `alice`
3. Starts a **miner loop** — polls the mempool every second and mines when
   transactions are pending
4. Starts a **watcher loop** — polls chain status every 2 seconds and prints
   when the tip advances

Then in a second terminal:

```sh
./build/zuno-debug send --from alice --to bob --amount 10
```

Output within ~2 seconds:

```
[16:29:05] chain    tip=0 a3f8c2...  mempool=1 transactions
[16:29:06] miner    Mined block #1 (0000e6...)  [1 tx]
[16:29:07] chain    tip=1 0000e6...  mempool=0 transactions
```

Press **Ctrl+C** to stop. The demo cleans up `.chain/` on exit.

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
