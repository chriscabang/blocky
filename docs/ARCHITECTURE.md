# zuno — Architecture

## Overview

zuno is a **post-quantum secure blockchain written in C**, modeled after git's
workflow and data model. Its design goals are:

- **No central authority** — every block is cryptographically verified by every peer
- **Immutable ledger** — hash-chained storage makes tampering cryptographically detectable
- **Post-quantum security** — Dilithium-3 signatures and p256_kyber768 TLS key exchange
- **Minimal dependencies** — only `liboqs` (PQC) and `OpenSSL` (TLS) are required
- **Clean C architecture** — SOLID principles, no global state, no `exit()` in library code

---

## Module Map

Each `.c`/`.h` pair owns exactly one responsibility. `main.c` is the only
module permitted to coordinate across module boundaries.

### Core Protocol

Modules that define the blockchain protocol itself:

| Module | Responsibility |
|---|---|
| `block.c` | `Block` struct — create, compute_hash, verify_hash, free |
| `chain.c` | In-memory chain tip + pool allocator — load, validate, add, propose |
| `storage.c` | Git-style `.chain/` object store and HEAD ref management |
| `consensus.c` | `verify_consensus()` dispatch → PoW or PoS rules |
| `crypto.c` | `block_hash()`, `compute_merkle_root()` |
| `sha256.c` | Self-contained FIPS 180-4 SHA-256 — no OpenSSL in the hash path |
| `transaction.c` | Transaction struct — Dilithium-3 sign and verify |
| `pow.c` | Proof-of-Work mining with midstate optimization and validation |
| `pos.c` | `PosEntry` type — stake-weighted selection helper |
| `vrf.c` | VRF leader election — slot message, prove, verify |
| `validator.c` | Validator registry — identity, stake, disk persistence |
| `equivocation.c` | Slot-based double-propose guard (`.chain/slots/`) |
| `mempool.c` | Signed transaction queue — add, load, count, purge |
| `key.c` | Dilithium-3 key store — generate, load pk/sk (`.chain/keys/`) |

### Infrastructure

Modules that support the protocol but are not part of the blockchain state machine:

| Module | Responsibility |
|---|---|
| `network.c` | TLS 1.3 server/client, block serialization, broadcast |
| `log.c` | Level-gated logger — ERROR/WARN/INFO/DEBUG |

The infrastructure boundary matters: `log.h` and `network.h` may be replaced or
mocked in test environments without affecting any protocol logic. Core modules
never include infrastructure headers — only other core headers.

---

## Dependency Graph

Arrows point from consumer to dependency (`A → B` means A includes B).

```
main.c ──────┬──► chain.h ──► block.h ──► transaction.h
             ├──► storage.h            └──► (sha256.h via crypto.h)
             ├──► consensus.h ──► block.h
             ├──► pow.h ────────► block.h
             └──► network.h ───► chain.h

chain.c ─────┬──► network.h   (for chain_propose broadcast)
             └──► consensus.h (for verify_consensus in chain_validate)

network.h ───────► chain.h    (Chain* in net_server_run)
```

**No circular dependencies.** `chain.h` does not include `network.h`. This is
enforced by design: `chain_propose` is implemented in `chain.c` (which includes
`network.h`), but the `chain.h` interface exposes no network types.

---

## Data Flow: Transaction to Block

```
zuno send --from alice --to bob --amount 10.5
        │
        ▼
Transaction {sender, recipient, amount=10500000, nonce}
        │
        ├─► sign_transaction()        [Dilithium-3 signature]
        ▼
.chain/mempool/<txhash>              [one file per signed transaction]

zuno mine
        │
        ▼
block_create(index, prev_hash)
        │
        ├─► compute_merkle_root()     [SHA-256 over all tx fields]
        │
        ├─► mine_block(b, DIFFICULTY) [PoW: nonce scan until leading zeros]
        ▼
Block {index, timestamp, prev_hash, merkle_root, nonce, consensus=0, hash}
        │
        ▼
chain_validate()
  ├─ previous_hash == chain->head->hash?
  ├─ block_verify_hash()    recompute and compare
  ├─ transaction amounts > 0?
  └─ verify_consensus()     PoW difficulty check
        │
        ▼
chain_add()
  ├─ storage_insert()       write block to .chain/blocks/<hash>
  └─ storage_checkout()     advance HEAD → new hash
```

---

## On-Disk Layout

Mirrors git's `.git/` directory structure:

```
.chain/
├── HEAD                        "ref: refs/heads/main"
├── refs/
│   └── heads/
│       └── main                <64-char tip block hash>
├── blocks/
│   ├── a3/
│   │   └── a3f8c2d1e5b6f790…  serialized Block struct (binary)
│   └── 00/
│       └── 0000e4f2c9a3b1d8…
├── mempool/                    signed pending transactions (one file per tx hash)
├── keys/                       Dilithium-3 keypairs (.pk public, .sk secret)
├── slots/                      equivocation guard (one file per PoS proposer)
├── validators/                 validator registry (one binary file per validator)
├── peers                       one ip:port per line (for propose)
├── tls-cert.pem                node TLS certificate
└── tls-key.pem                 node TLS private key (chmod 600)
```

**HEAD resolution chain:**
```
HEAD → "ref: refs/heads/main" → .chain/refs/heads/main → <block hash>
```

`storage_checkout(hash)` updates the branch file, not HEAD itself — identical
to how `git checkout` works.

---

## Consensus Model

`Block.consensus` is an explicit field set by the proposer:

| Value | Constant | Rule enforced by `verify_consensus()` |
|---|---|---|
| `0` | `CONSENSUS_POW` | Hash must have ≥ `DIFFICULTY` leading hex zeros |
| `1` | `CONSENSUS_POS` | Hash integrity + proposer stake + VRF proof + Dilithium-3 signature |

`mine` (CLI) always produces `CONSENSUS_POW` blocks. `chain_validate()` calls
`verify_consensus()` for every `chain_add()` — consensus enforcement is never
bypassed.

### Canonical Chain — GHOST

When competing chain branches exist, zuno uses **GHOST** (Greedy Heaviest
Observed Subtree). At a fork point, the branch whose total subtree has the
greatest descendant count wins — not just the longest tip.

```
A → B → C → D → E        (subtree weight: 5)  ← canonical
              ↘ X → Y    (subtree weight: 2)
```

Block weight is `1` per PoW block (uniform) and `stake amount` per PoS block
(stake-weighted). Tie-break: lexicographically smaller hash wins.

`chain_fork_choice()` is called automatically by `chain_load()` so a node
always boots onto the canonical branch, even after storing competing blocks
from peers.

### PoS Proposer Requirements

A valid PoS proposer must satisfy all three conditions:

1. **Minimum stake locked** in the validator registry
2. **Selected for the current slot** by a VRF (Verifiable Random Function)
3. **Block signed** with a Dilithium-3 post-quantum key

`verify_pos_rules()` enforces these in order: hash integrity → stake check →
equivocation guard → VRF proof → Dilithium-3 block signature.

---

## Network Model

```
Node A (proposer)                      Node B (receiver)
─────────────────────                  ─────────────────────
chain_propose(c, c->head)
  │
  ├─ read .chain/peers
  └─ net_broadcast_block()  ──TLS──►  net_server_run(ctx, chain)
       serialize header                   │
       (index, ts, hashes,                ├─ net_deserialize_block()
        nonce, consensus,                 ├─ block_verify_hash()
        tx_count)                         ├─ GETBODY: fetch tx bodies
                                          └─ chain_add()
```

Only block **header fields** are broadcast in the initial message. Full
transaction bodies (Dilithium-3 signatures) are fetched on demand via the
**GETBODY protocol** — a two-phase exchange within a single TLS connection:

```
Broadcaster             Server
────────────            ──────────────────────────────────
send header    ──────►  verify header hash
                        if tx_count > 0:
               ◄──────  send "GETBODY:<hash>\n"
send tx count  ──────►
send tx array  ──────►  attach tx bodies → chain_add()
```

The server is **single-threaded** — one connection at a time. Block arrival
frequency (~1/min at DIFFICULTY=4) and `NET_LISTEN_BACKLOG=16` make this
sufficient for any realistic mesh topology. See `docs/NOTES.md` ADR-014 for the
full rationale.

---

## Design Philosophy

### Git-Like Workflow

zuno's workflow and data model are modeled after git:

| Git | zuno |
|---|---|
| `git init` | `zuno init` |
| `git add` | `zuno send --from X --to Y --amount N` |
| `git commit` | `zuno mine` |
| `git push` | `zuno propose` |
| `git log` | `zuno log` |
| `git show <hash>` | `zuno show <hash>` |
| `git cat-file -p <hash>` | `zuno cat <hash>` |
| `git verify-commit <hash>` | `zuno verify <hash>` |
| Branch pointer | `.chain/refs/heads/main` |
| Competing branches | Resolved by GHOST fork choice |

### SOLID in C

#### S — Single Responsibility

Each `.c`/`.h` pair owns exactly one concern. A module that does two things
should be two modules.

- `sha256.c` — FIPS 180-4 SHA-256 computation, nothing else
- `storage.c` — On-disk object store and ref management
- `network.c` — TLS connection lifecycle and block serialization
- `pow.c` — Proof-of-Work mining and validation
- `transaction.c` — Transaction signing and verification
- `chain.c` — In-memory chain state and pool allocator

#### O — Open/Closed

Public headers define stable interfaces. New behavior is added by extension,
not by modifying existing interfaces.

- New consensus algorithm: add a new module and a new `CONSENSUS_*` entry in
  the dispatch table — no changes to `verify_consensus()` itself
- New TLS option: add a field to `NetConfig` — no new function signatures
- New validator field: extend `Validator` struct — `validator_register` and
  `validator_lookup` are unchanged

#### L — Liskov Substitution

Any function accepting `const Block *` or `const Chain *` works correctly for
any valid instance, regardless of how it was constructed. `block_verify_hash()`
is meaningful for any non-NULL Block. Genesis is distinguished only by its
`previous_hash` value, not by a special type.

#### I — Interface Segregation

Headers expose only what callers need. Internal helpers are `static`.

- `NetContext` is opaque — callers hold a pointer, no field access
- `storage.h` exposes six functions; the file path layout is internal
- `sha256.h` exposes exactly five functions

#### D — Dependency Inversion

High-level modules depend on abstractions, not implementation details.

- `chain.c` calls `storage_insert` / `storage_checkout` — no knowledge of file paths
- `pow.c` calls `block_compute_hash` — no knowledge of SHA-256 internals
- `network.c` calls `log_info` / `log_error` — no knowledge of log routing

### Clean Code Rules

- **No `exit()` in library code.** Only `main.c` and utility `main()` functions
  may terminate the process. Every library function returns an error code.
- **No global mutable state.** Each `NetContext`, `Chain`, and `OQS_SIG` is
  heap-allocated and caller-owned.
- **Const-correctness.** Functions that do not modify their inputs declare them
  `const`. Violations are bugs.
- **Errors are values.** Return codes are checked at every call site.
  `goto done` with a single cleanup block is preferred over duplicated
  `free` / `SSL_free` chains.
- **Private key material is zeroed before release.** Any function holding a
  private key calls `OQS_MEM_cleanse` before `free`.
- **snake_case everywhere.** All identifiers, source filenames, and binary names.

### Testing Conventions

Every public `.h`/`.c` module has a corresponding `tests/test_<module>.c`
using CMocka. A module is not complete until `make test` passes.

- One test file per unit: `tests/test_<unit>.c`
- `setup`/`teardown` clean up all side effects including `.chain/` directories
- Tests link against debug objects (no `main.o`)
- Group tests by function under test: `cmocka_run_group_tests_name("group", …)`
- `make test` must pass before any code is considered complete

---

## Project Layout

```
zuno/
├── src/                    C source files (one module per file)
├── inc/                    Public headers
├── tests/                  CMocka unit + integration tests
├── utils/                  Standalone utility binaries
├── docs/
│   ├── ARCHITECTURE.md     This file — architecture and philosophy
│   ├── DESIGN.md           Technical implementation reference
│   ├── QUICKSTART.md       Setup, build, and usage guide
│   ├── CONTRIBUTING.md     Contribution guidelines
│   └── NOTES.md            Architecture Decision Records (ADR-001 – ADR-019)
└── README.md               Project overview and CLI reference
```

For full architecture decision rationale, see `docs/NOTES.md`.
