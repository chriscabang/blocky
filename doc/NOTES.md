# kiat — Architecture & Design Notes

This document captures architectural decisions, design discussions, and
implementation rationale for the kiat (`kiat`) project. It is the
authoritative reference for why the code is structured the way it is.

Update this file whenever a design decision is made, revised, or reversed.
The git log is the record of *what* changed; this file is the record of *why*.

---

## Table of Contents

- [Part I — Architectural Overview](#part-i--architectural-overview)
  - [Module Map](#module-map)
  - [Dependency Graph](#dependency-graph)
  - [Data Flow: Transaction to Block](#data-flow-transaction-to-block)
  - [On-Disk Layout](#on-disk-layout)
  - [Consensus Model](#consensus-model)
  - [Network Model](#network-model)
- [Part II — Design Philosophy](#part-ii--design-philosophy)
  - [SOLID in C](#solid-in-c)
  - [Clean Code Conventions](#clean-code-conventions)
  - [Testing Conventions](#testing-conventions)
- [Part III — Architecture Decision Records](#part-iii--architecture-decision-records)
  - [ADR-001 Git-Like Blockchain Model](#adr-001-git-like-blockchain-model)
  - [ADR-002 Canonical Chain Rule — GHOST](#adr-002-canonical-chain-rule--ghost)
  - [ADR-003 Proposer Requirements — Stake + VRF + Dilithium](#adr-003-proposer-requirements--stake--vrf--dilithium)
  - [ADR-004 Language Stack and Implementation Philosophy](#adr-004-language-stack-and-implementation-philosophy)
  - [ADR-005 Storage Module — Git-Style Object Store](#adr-005-storage-module--git-style-object-store)
  - [ADR-006 Block/Chain Split — Pool Allocator](#adr-006-blockchain-split--pool-allocator)
  - [ADR-007 Crypto Module — Self-Contained SHA-256](#adr-007-crypto-module--self-contained-sha-256)
  - [ADR-008 CLI Design — Git-Like Command Dispatcher](#adr-008-cli-design--git-like-command-dispatcher)
  - [ADR-009 Transaction Module — Dilithium Sizes, Integer Amounts, Replay Protection](#adr-009-transaction-module--dilithium-sizes-integer-amounts-replay-protection)
  - [ADR-010 Consensus Module — Dispatch Table, PoW, PoS](#adr-010-consensus-module--dispatch-table-pow-pos)
  - [ADR-011 Log Module — Heisenbug-Safe, Level-Gated, Secure](#adr-011-log-module--heisenbug-safe-level-gated-secure)
  - [ADR-012 Integration Tests — Payment and Miner Use Cases](#adr-012-integration-tests--payment-and-miner-use-cases)
  - [ADR-013 Build System — Test Summary and Coverage](#adr-013-build-system--test-summary-and-coverage)
  - [ADR-014 Network Module — TLS Security, SOLID Rewrite](#adr-014-network-module--tls-security-solid-rewrite)
  - [ADR-015 Dead Code Removal — iterator.h and common.h](#adr-015-dead-code-removal--iteratorh-and-commonh)
  - [ADR-016 Deployment — Raspberry Pi + USB SSD, Native systemd](#adr-016-deployment--raspberry-pi--usb-ssd-native-systemd)
- [Part IV — Pending Work](#part-iv--pending-work)

---

## Part I — Architectural Overview

### Module Map

Each `.c`/`.h` pair owns exactly one responsibility. `main.c` is the only
module permitted to coordinate across module boundaries.

```
src/main.c          CLI dispatcher — init, send, commit, propose, log, show, …
src/chain.c         In-memory chain tip, pool allocator, validate, add, propose
src/block.c         Block struct — create, compute_hash, verify_hash, free
src/storage.c       Git-style .chain/ object store and HEAD ref management
src/network.c       TLS 1.3 server/client, block serialize/deserialize, broadcast
src/consensus.c     verify_consensus() dispatch → PoW or PoS rules
src/pow.c           Proof-of-Work mining (midstate optimization) and validate
src/pos.c           Proof-of-Stake validator registry, stake, select, validate
src/crypto.c        block_hash(), compute_merkle_root(), sign/verify stubs
src/sha256.c        Self-contained FIPS 180-4 SHA-256 — no OpenSSL in hash path
src/transaction.c   Transaction struct, Dilithium-3 sign and verify
src/log.c           Level-gated logger — ERROR/WARN/INFO/DEBUG
```

### Dependency Graph

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

**No circular dependencies.** `chain.h` does not include `network.h`. This
is enforced by design: `chain_propose` is implemented in `chain.c` (which
includes `network.h`), but the `chain.h` interface has no network types.

### Data Flow: Transaction to Block

```
User: kiat send --from alice --to bob --amount 10.5
          │
          ▼
  Transaction {sender, recipient, amount=10500000, nonce}
          │
          ├─► sign_transaction()   [Dilithium-3 signature — ADR-009]
          │
          ▼
  .chain/STAGED  (tab-separated staging area — ADR-008)

User: kiat commit
          │
          ▼
  block_create(index, prev_hash)
          │
          ├─► compute_merkle_root()   SHA-256 over all tx fields — ADR-007
          │
          ├─► mine_block(b, DIFFICULTY)   PoW: nonce scan until hash has
          │                               DIFFICULTY leading '0' hex chars
          ▼
  Block {index, timestamp, prev_hash, merkle_root, nonce, consensus=0, hash}
          │
          ▼
  chain_validate()
    ├─ previous_hash == c->head->hash ?
    ├─ block_verify_hash() : recompute and compare
    ├─ transaction amounts > 0 ?
    └─ verify_consensus() : PoW difficulty check
          │
          ▼
  chain_add()
    ├─ storage_insert()    write block to .chain/blocks/<hash>
    └─ storage_checkout()  advance .chain/refs/heads/main → new hash
```

### On-Disk Layout

Mirrors git's `.git/` structure exactly (ADR-005):

```
.chain/
├── HEAD                        "ref: refs/heads/main"
├── refs/
│   └── heads/
│       └── main                <64-char block hash of current tip>
├── blocks/
│   ├── a3/
│   │   └── a3f8c2d1e5b6f790…  serialized Block struct (binary)
│   └── 00/
│       └── 0000e4f2c9a3b1d8…
├── STAGED                      tab-separated pending transactions
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

### Consensus Model

`Block.consensus` is an explicit field set by the proposer:

| Value | Constant | Rule enforced by `verify_consensus()` |
|---|---|---|
| `0` | `CONSENSUS_POW` | Hash must have ≥ `DIFFICULTY` leading hex zeros |
| `1` | `CONSENSUS_POS` | Hash integrity only; VRF + Dilithium pending ADR-003 |

`commit` (CLI) always sets `CONSENSUS_POW` and calls `mine_block()`.
`chain_validate()` calls `verify_consensus()` for every `chain_add()`.

Every-tenth-block PoS selection (`index % 10 == 0`) is the current placeholder
in `pos_validate_block()` — VRF leader election is pending ADR-003.

### Network Model

```
Node A (proposer)                      Node B (receiver)
─────────────────────                  ─────────────────────
chain_propose(c, c->head)
  │
  ├─ read .chain/peers
  ├─ net_context_client()              net_context_server()
  │                                        │
  └─ net_broadcast_block()  ──TLS──►  net_server_run(ctx, chain)
       serialize header                   │
       (index, ts, hashes,                ├─ net_deserialize_block()
        nonce, consensus,                 ├─ block_verify_hash()
        tx_count)                         └─ chain_add()
```

Only block **header fields** are broadcast; full transaction bodies
(Dilithium-3 signatures up to ~145 KB/block) are fetched on demand.
`net_server_run()` accepts `Chain *chain`; passing `NULL` enables log-only
monitor mode.

---

## Part II — Design Philosophy

kiat is written in C and deliberately applies **SOLID principles** and
**clean code** practices throughout. These are not aspirational — they are
enforced at review time.

### SOLID in C

#### S — Single Responsibility

Each `.c`/`.h` pair owns exactly one concern. A module that does two things
should be two modules.

| Module | Sole responsibility |
|---|---|
| `sha256.c` | FIPS 180-4 SHA-256 computation — nothing else |
| `storage.c` | On-disk object store and ref management |
| `network.c` | TLS connection lifecycle and block serialization |
| `pow.c` | Proof-of-Work mining and validation |
| `transaction.c` | Transaction signing and verification |
| `chain.c` | In-memory chain state and pool allocator |

`main.c` is the only module permitted to coordinate across modules.

#### O — Open/Closed

Public headers define stable interfaces. New behavior is added by extension,
not by modifying existing interfaces.

- Adding a new consensus algorithm means adding a new module and a new
  `CONSENSUS_*` constant — not modifying `block.h` or `chain.c`.
- `net_context_server` / `net_context_client` accept a `NetConfig` struct;
  adding a new TLS option is a new field in `NetConfig`, not a new function.
- The consensus dispatch table in `consensus.c` is indexed by `block->consensus`;
  a new entry requires only a new function pointer — no changes to
  `verify_consensus()` itself.

#### L — Liskov Substitution

Any function accepting `const Block *` or `const Chain *` works correctly for
any valid instance of that type, regardless of how it was constructed.

- `block_verify_hash(block)` is meaningful for any non-NULL Block.
- `chain_validate(chain, block)` treats every Block identically — genesis is
  distinguished only by its `previous_hash` value, not by a special type.

#### I — Interface Segregation

Headers expose only what callers need. Internal helpers are `static` and
invisible outside the translation unit.

- `NetContext` is opaque — callers hold a pointer; no field access.
- `storage.h` exposes six functions; the file path layout is entirely internal.
- `sha256.h` exposes `sha256_ctx`, `sha256_init`, `sha256_update`,
  `sha256_final`, `sha256_digest`, `sha256_to_hex` — and nothing else.

#### D — Dependency Inversion

High-level modules depend on abstractions, not on implementation details.

- `chain.c` calls `storage_insert` / `storage_checkout` — no knowledge of
  file paths or `fwrite`.
- `pow.c` calls `block_compute_hash` — no knowledge of SHA-256 internals.
- `network.c` calls `log_info` / `log_error` — no knowledge of log routing.

---

### Clean Code Conventions

**snake_case everywhere.**
All identifiers, source filenames, and binary names. `start_chain`,
`mine_block`, `key_gen`. This mirrors the C standard library and eliminates
ambiguity between `startchain`, `StartChain`, and `start_chain`.

**No `exit()` in library code.**
Only `main.c` and utility `main()` functions may terminate the process. Every
library function returns an error code (`NULL`, `-1`, `EXIT_FAILURE`) and lets
the caller decide. All modules are safe to link into test harnesses.

**No global mutable state.**
Each `NetContext`, `Chain`, and `OQS_SIG` instance is heap-allocated and
caller-owned. No module-level globals.

**Const-correctness.**
Functions that do not modify their inputs declare them `const`. Violations are
treated as bugs:

```c
int  block_verify_hash(const Block *block);
int  chain_validate(const Chain *c, const Block *block);
int  net_serialize_block(const Block *block, char *buf, size_t bufsz);
int  chain_propose(Chain *c, const Block *block);
void compute_merkle_root(const Block *block, char *out);
```

**Errors are values, not exceptions.**
Return codes are checked at every call site. `goto done` with a single cleanup
block is preferred over duplicated `free` / `SSL_free` chains:

```c
int rc = EXIT_FAILURE;
OQS_SIG *sig  = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
uint8_t *priv = malloc(sig->length_secret_key);
if (!sig || !priv) goto done;
/* … work … */
rc = EXIT_SUCCESS;
done:
    OQS_MEM_cleanse(priv, sig->length_secret_key);
    free(priv);
    OQS_SIG_free(sig);
    return rc;
```

**Private key material is zeroed before release.**
Any function that holds a private key calls `OQS_MEM_cleanse` before `free`.

**One function, one job.**
Functions are short and named after what they do. `apply_common_security` in
`network.c` encapsulates TLS 1.3 + PQC group setup once, shared by both server
and client paths without duplication.

---

### Testing Conventions

Every public `.h`/`.c` module has a corresponding `tests/test_<module>.c`
using CMocka. A module is not complete until `make test` passes with that
suite included.

**Standard test file structure:**

```c
#include <stdarg.h>
#include <stddef.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
#include <setjmp.h>
#include <cmocka.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

static int setup(void **state)   { /* init shared state */ return 0; }
static int teardown(void **state){ /* free state, rm -rf .chain */ return 0; }

static void test_name(void **state) {
    /* assert_* macros; never return an error code */
}

int main(void) {
    log_set_stream(stderr);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_name, setup, teardown),
    };
    return cmocka_run_group_tests_name("group", tests, NULL, NULL);
}
```

**Rules:**
- One test file per unit: `tests/test_<unit>.c`
- `setup`/`teardown` clean up all side effects including `.chain/` directories
- Tests link against debug objects (no `main.o`) — see Makefile `test` target
- Group tests by function under test: `cmocka_run_group_tests_name("insert", …)`
- `make test` must pass before any code is considered complete

**Current test suite (213 tests across 14 suites):**

| Suite | Tests | Covers |
|---|---|---|
| `test_block` | 12 | create, compute_hash, verify_hash, free |
| `test_chain` | 18 | load, unload, validate, add, propose |
| `test_consensus` | 8 | verify_consensus (PoW/PoS/unknown), verify_block_signature stub |
| `test_crypto` | 20 | SHA-256 NIST vector, OpenSSL cross-check, block_hash, Merkle root |
| `test_integration_miner` | 9 | PoW end-to-end, chain growth, tamper rejection |
| `test_integration_payment` | 6 | Payment flow, batch, multi-block, invalid amount, tamper |
| `test_log` | 8 | Stream routing, level gating, DEBUG guard, format |
| `test_main` | 23 | All CLI subcommands via `popen` subprocess tests |
| `test_network` | 25 | context lifecycle, serialize, deserialize, broadcast arg validation |
| `test_pos` | 13 | init, stake, select (deterministic), validate |
| `test_pow` | 13 | mine, validate, midstate correctness |
| `test_sha256` | 16 | FIPS 180-4, incremental vs one-shot, boundary lengths |
| `test_storage` | 26 | insert, read, exists, head, checkout, scan |
| `test_transaction` | 15 | constants, struct size, sign, verify, tamper detection |

---

## Part III — Architecture Decision Records

---

### ADR-001: Git-Like Blockchain Model

**Date:** 2025-03 · **Status:** Adopted

#### Context

The goal of kiat is to create a blockchain that operates like git. Both
systems are content-addressed DAGs: commits and blocks are identified by the
hash of their content plus their parent's hash.

| Git operation | `kiat` equivalent |
|---|---|
| `git init` | `kiat init` — initialize chain, create genesis block |
| `git status` | `kiat status` — show chain tip and staged transactions |
| `git add <file>` | `kiat send --from X --to Y --amount N` — stage a transaction |
| `git commit` | `kiat commit` — seal staged transactions into a new block |
| `git commit -S` | `kiat commit` + Dilithium signing — sign block (ADR-003, pending) |
| `git push` | `kiat propose` — broadcast block to peers |
| `git log` | `kiat log [--limit N]` — list blocks newest-first |
| `git show <hash>` | `kiat show <hash>` — human-readable block detail |
| `git cat-file -p <hash>` | `kiat cat <hash>` — raw field dump of a block |
| `git verify-commit <hash>` | `kiat verify <hash>` — verify block hash integrity |
| `git checkout <branch>` | automatic — GHOST selects heaviest chain tip (ADR-002) |
| Branch pointer | chain tip stored in `.chain/refs/` |
| Competing branches | chain forks — resolved by GHOST subtree weight (ADR-002) |
| Merge | consensus — GHOST canonical selection; no explicit merge command |

#### Decision

Design the workflow and data model around git semantics:

1. A node **checks out** the current heaviest chain tip (selects a parent block)
2. The node **builds** a new block on top of it (stages transactions, computes hashes)
3. The node **proposes** the block to the network
4. Peers **validate** and, if accepted, extend the chain

#### Rationale

The mental model is familiar to developers. Explicit fork management is cleaner
than implicit forks from simultaneous mining. The git analogy makes the two-step
`send` + `commit` workflow intuitive as the blockchain equivalent of `git add`
then `git commit`.

---

### ADR-002: Canonical Chain Rule — GHOST

**Date:** 2025-03 · **Status:** Adopted · **Implemented:** 2026-03 · **Files:** `inc/chain.h`, `src/chain.c`, `inc/storage.h`, `src/storage.c`

#### Context

When two valid competing chain branches exist, the network needs a deterministic
rule to decide which is canonical. Options considered:

- **Longest chain (Nakamoto)** — pick the branch with the most blocks
- **GHOST** — pick the branch whose subtree has the most total descendants
- **BFT finality (Tendermint)** — require 2/3 supermajority vote
- **First-seen / FIFO** — trivially gameable, rejected

#### Decision

Adopt **GHOST** (Greedy Heaviest Observed Subtree).

At a fork point, count all descendant blocks in each branch's subtree. The
branch with the greater subtree weight wins — not just the longest tip:

```
A → B → C → D → E        (subtree weight: 5)
              ↘ X → Y    (subtree weight: 2)
```

GHOST selects the branch ending at `E`.

Block weight is:
- `1` per block in PoW mode (uniform weight)
- `stake amount` of the proposer in PoS mode (stake-weighted)

The `consensus` field in `Block` already distinguishes these two modes.

#### Rationale

- **Fork resistant** — unlike longest-chain, GHOST counts "uncle" blocks as
  weight, making fork attacks significantly more expensive.
- **Works with both PoW and PoS** — weight metric is pluggable.
- **Production-validated** — LMD-GHOST is the fork choice rule used by
  Ethereum's Beacon Chain.
- **Best fit for the git analogy** — the "heaviest branch" wins, just as `main`
  wins in a real project because more contributors build on it.

#### Implementation

`chain_fork_choice(Chain *c)` is declared in `inc/chain.h` and implemented in
`src/chain.c`. It is called automatically by `chain_load()` so a node always
boots onto the canonical branch, even after storing competing blocks from peers.

**Algorithm:**

1. `storage_list_all()` (new in `src/storage.c`) enumerates every block file in
   `.chain/blocks/` — all stored blocks, not just the current HEAD chain.
2. Each block is loaded into a lightweight `GhostNode {hash, prev, consensus}`
   struct. Full transaction arrays are discarded immediately to bound memory.
3. The genesis block is located by its sentinel `previous_hash == "0"`.
4. `ghost_walk()` descends greedily from genesis: at each fork point it picks
   the child whose `ghost_subtree_weight()` is greatest.
5. Tie-break: lexicographically smaller hash wins (deterministic, no randomness).
6. If the canonical tip differs from `c->head`, the pool slot is updated and
   `storage_checkout()` persists the new HEAD.

**Weight metric:**

| Mode | Weight per block | Notes |
|---|---|---|
| PoW (`consensus == 0`) | 1 | Uniform — every block counts equally |
| PoS (`consensus == 1`) | 1 | Placeholder — will use proposer stake once ADR-003 validator registry is implemented |

**Tests:** 6 new tests in `tests/test_chain.c` under the `fork_choice` group —
null guard, genesis-only (no-op), linear chain (no reorg), heavy-branch-wins
(core GHOST property), tiebreak-by-hash, and storage-head-updated-after-reorg.

---

### ADR-003: Proposer Requirements — Stake + VRF + Dilithium

**Date:** 2025-03 · **Status:** Adopted (design); implementation pending

#### Context

A proposer is a node that builds and broadcasts a new block. The network needs:
1. Sybil resistance — fake nodes cannot spam proposals cheaply
2. Quantum-resistant signatures — block proposals signed with PQC keys
3. Unpredictable leader selection — no validator can game when they are selected
4. Feasibility on Raspberry Pi hardware

#### Decision

A valid proposer must satisfy all three conditions:

1. **Minimum stake locked** in the validator registry
2. **Selected for the current slot** by a VRF (Verifiable Random Function)
3. **Block signed** with a **Dilithium-3** post-quantum key

Full propose flow:

```
Validator locks stake (validator registry)
        ↓
VRF selects proposer for slot N
        ↓
Proposer: checkout heaviest GHOST tip
        ↓
Proposer: build block, sign with Dilithium-3 key
        ↓
Proposer: broadcast to peers
        ↓
Peers validate:
  ✓ VRF proof valid?
  ✓ Dilithium-3 signature valid?
  ✓ Proposer has sufficient stake?
  ✓ Block extends correct GHOST tip?
        ↓
Block accepted → GHOST subtree weight updated
```

#### Rationale

| Concern | Solution |
|---|---|
| Raspberry Pi energy | No PoW — signing is computationally cheap |
| Quantum resistance | Dilithium-3 replaces ECDSA (available in `liboqs`) |
| Sybil resistance | Stake lockup — spinning up fake validators costs real tokens |
| Selfish proposing | VRF — no validator can predict or game their selection slot |
| Git analogy | Signing a block mirrors `git commit -S` (signed commits) |

#### Implementation Notes

- **Dilithium-3** is the target signature scheme; it is already available via
  `liboqs`. Transaction signing (`sign_transaction` / `verify_transaction`) is
  fully implemented. Block-level signing (`verify_block_signature`) is a stub
  returning `EXIT_FAILURE` pending the validator registry.
- **VRF** is not directly provided by `liboqs` but can be constructed from PQC
  primitives. Algorand's VRF construction is a useful reference.
- **Validator registry** — new `validator.c` or extension of `storage.c` —
  tracks public keys and locked stake per validator ID.
- This design is architecturally equivalent to a simplified Ethereum Beacon
  Chain (LMD-GHOST + Casper-FFG stake).

---

### ADR-004: Language Stack and Implementation Philosophy

**Date:** 2026-03 · **Status:** Adopted

#### Decision

- **Core language: C.** All fundamental units (block, chain, storage, crypto,
  consensus, network) are written in C.
- **C++ and Python: as needed** for tooling or scripting, not for core chain
  logic.
- **Native-first:** minimize external dependencies. Only `liboqs` (PQC, no
  native alternative) and `OpenSSL` (TLS) are accepted. `cmocka` is test-only.

#### Rationale

- C keeps the implementation close to the metal — essential for Raspberry Pi
  performance and memory predictability.
- Native-first reduces dependency management burden and improves portability
  across Linux and macOS.
- Per-unit CMocka tests enforce clear module boundaries and catch regressions
  early.

---

### ADR-005: Storage Module — Git-Style Object Store

**Date:** 2026-03 · **Status:** Adopted · **Files:** `inc/storage.h`, `src/storage.c`

#### Context — Problems in Original storage.c

| Bug | Severity |
|---|---|
| `storage_head()` returned heap-allocated `char *` — consistently leaked | High |
| `storage_move()` called `memcmp` on potentially NULL `storage_head()` return | High |
| `storage_scan()` count was always one less than actual (off-by-one at genesis) | High |
| `fwrite`/`fread` return values not checked — silent data corruption | High |
| `storage_insert()` always updated HEAD — object store and ref management coupled | Medium |
| `HEAD` stored raw hash directly — diverged from git's symbolic ref model | Medium |
| No way to check block existence without reading the entire block | Low |

#### Decision — On-Disk Layout

The `.chain/` directory mirrors git's `.git/` structure:

```
.chain/
├── blocks/              ← object store: one binary file per block, named by hash
│   └── a1b2c3…          ← serialized Block struct (binary, fwrite/fread)
├── refs/
│   └── heads/
│       └── main         ← plain text: the tip block hash
└── HEAD                 ← plain text: "ref: refs/heads/main"
```

`storage_checkout()` updates the branch file (`refs/heads/main`), not `HEAD`
itself — identical to `git checkout`. Detached HEAD (raw hash in HEAD) is also
supported for inspecting specific blocks.

#### Decision — Public API

| Function | Behaviour |
|---|---|
| `storage_insert(block)` | Write block to object store. Idempotent. Does **not** update HEAD. |
| `storage_read(hash)` | Read and return a block by hash. Caller frees. |
| `storage_exists(hash)` | Check if block file exists. No allocation, no file open. |
| `storage_head(buf, size)` | Resolve HEAD → ref → hash into caller-provided buffer. |
| `storage_checkout(hash)` | Advance branch tip to hash. Verifies block exists first. |
| `storage_scan(offset, count)` | Walk chain backwards from HEAD, return array of hashes. |

**Insert and checkout are separate operations** — a node must be able to store
a received block for validation without immediately making it HEAD.

**`storage_exists()` uses `access(F_OK)`** — no allocation, safe to call
frequently in the deduplication path.

**Durability: `fsync` on all writes.** Block files and ref files are flushed
with `fflush` + `fsync` before `fclose`. A block file that fails to write
completely is deleted before returning failure.

**Genesis sentinel.** Genesis `previous_hash` is `"0"` (ASCII zero + null).
`GENESIS_PREVIOUS_HASH` is exported so all modules use the same constant.
`storage_scan()` detects genesis by checking `previous_hash[0] == '0' &&
previous_hash[1] == '\0'`.

#### `storage_scan()` Walk Semantics

Walks the chain backwards from HEAD (newest → oldest):

```
HEAD → block[4] → block[3] → block[2] → block[1] → block[0/genesis]

scan(offset=0, count=5) → [hash4, hash3, hash2, hash1, hash0]
scan(offset=2, count=5) → [hash2, hash1, hash0]    ← skips 2 from HEAD
```

`*count` is updated to the actual number returned.

#### Test Coverage

22 tests across 6 groups in `tests/test_storage.c`:

| Group | Tests |
|---|---|
| `insert` | null block, empty hash, valid block, duplicate (idempotent) |
| `read` | valid block (field verification), null hash, empty hash, nonexistent |
| `exists` | after insert, nonexistent, null hash |
| `head` | before checkout (empty ref → FAILURE), after checkout, invalid buffer |
| `checkout` | null, empty, nonexistent, valid, already-at-head (no-op) |
| `scan` | null count, empty chain, single block, multiple (order), capped, offset, offset past genesis |

---

### ADR-006: Block/Chain Split — Pool Allocator

**Date:** 2026-03 · **Status:** Adopted · **Files:** `inc/block.h`, `inc/chain.h`, `src/block.c`, `src/chain.c`

#### Context — Problems in Original blockchain.c

The original monolith mixed the `Block` type, single-block operations, and
chain-level operations. Two architectural bugs were found:

1. **`Block.next` written to disk.** `fwrite(block, sizeof(Block), 1, f)`
   serialized the runtime `next` pointer as raw memory. When read back, the
   field contained a garbage address. `validate()` dereferenced it — crash.

2. **Hidden global state.** `Block *chain = NULL` was file-scoped, making
   multiple chain instances and clean teardown in tests impossible.

#### Decision — Module Split

| Unit | Responsibility |
|---|---|
| `block.h / block.c` | `Block` type + single-block ops: `create`, `compute_hash`, `verify_hash`, `free` |
| `chain.h / chain.c` | In-memory chain with pool allocator: `load`, `unload`, `validate`, `add`, `propose` |

`blockchain.h` is kept as a compatibility shim that includes both headers.

#### Pool Allocator Design

```c
#define CHAIN_POOL_SIZE 64

typedef struct {
  Block   *head;
  Block    pool[CHAIN_POOL_SIZE];
  uint8_t  pool_used[CHAIN_POOL_SIZE];
} Chain;
```

- `Chain` is heap-allocated once by `chain_load()`. No per-block `malloc` after
  init.
- At steady state **one slot is occupied**: the current chain tip. When
  `chain_add()` installs a new block, the old head slot is freed immediately
  (it is already persisted to disk).
- Pool exhaustion returns `EXIT_FAILURE`. With `CHAIN_POOL_SIZE = 64` this is
  unreachable in normal operation but tested explicitly.

#### `Block.next` Fix

`Block.next` is a **runtime-only** pointer. It must never reach disk:

- `storage_insert()` writes a stack copy with `copy.next = NULL`
- `storage_read()` sets `block->next = NULL` after `fread`
- `chain_validate()` and `chain_add()` never dereference `next`; chain linkage
  uses `previous_hash`

#### Test Coverage

**`test_block.c`** — 12 tests: create, compute, verify, free.

**`test_chain.c`** — 18 tests:

| Group | Tests |
|---|---|
| `load` | creates genesis on empty chain, loads existing HEAD, two independent loads agree |
| `unload` | NULL is safe |
| `validate` | NULL chain, NULL block, valid next block, wrong previous_hash, tampered hash |
| `add` | NULL args, advances head, updates storage HEAD, `next` is NULL on disk, invalid rejected, pool exhaustion |
| `propose` | NULL chain, NULL block, no peers file (returns EXIT_SUCCESS) |

---

### ADR-007: Crypto Module — Self-Contained SHA-256

**Date:** 2026-03 · **Status:** Adopted · **Files:** `inc/sha256.h`, `src/sha256.c`, `inc/crypto.h`, `src/crypto.c`

#### Context — Bugs in Original crypto.c

| Bug | Severity | Detail |
|---|---|---|
| Hash truncated to 32 chars | Critical | `memcpy(block->hash, hash_hex, SHA256_DIGEST_LENGTH)` copied 32 of 64 hex chars |
| `consensus` omitted from hash | High | Changing PoW↔PoS produced the same block hash |
| Merkle root used only `sender` | High | `recipient` and `amount` ignored — different transactions could produce the same root |
| Deprecated OpenSSL SHA API | Medium | `SHA256_Init/Update/Final` deprecated in OpenSSL 3.x |
| NULL dereference before NULL check | Medium | `log_info(block->index)` ran before `if (!block)` |
| `sign()`/`verify()` declared but undefined | Medium | Linker error if called |

#### Decision — Self-Contained SHA-256

Replace OpenSSL's SHA-256 with a clean implementation of FIPS 180-4. OpenSSL
stays in the build for TLS only.

| Goal | Why |
|---|---|
| Small | ~130 lines, zero dependencies |
| Auditable | Every line checkable against FIPS 180-4 §5 |
| Portable | Pure C99 — works on Raspberry Pi, macOS, Linux |
| Native-first | Aligns with ADR-004; no external SHA in hot path |

**Alternative considered: Monocypher (Blake2b).** Faster, formally audited,
used in WireGuard. Not chosen because it changes the hash format (not SHA-256
compatible). Remains a valid future choice if the hash format is renegotiated.

#### sha256.h / sha256.c API

```c
void sha256_init  (sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const void *data, size_t len);
void sha256_final (sha256_ctx *ctx, uint8_t digest[SHA256_DIGEST_LEN]);
void sha256_digest(const void *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]);
void sha256_to_hex(const uint8_t *bytes, size_t len, char *out);
```

`sha256_final` wipes the context after producing the digest (no state leakage).
`sha256_to_hex` is the shared hex encoder used by `crypto.c` and `pow.c`,
eliminating the DRY violation between two identical `to_hex`/`bytes_to_hex`
static functions.

#### Fixes Applied to crypto.c

1. Hash length: `sha256_to_hex` writes all 64 hex chars; `HASH_SIZE = 65` fully
   utilised
2. `consensus` field added to block hash input
3. `compute_merkle_root`: feeds `sender + recipient + amount + nonce` via
   incremental `sha256_update`
4. NULL check moved before `log_info`
5. `sign()`/`verify()` stubs renamed to `block_sign()`/`block_verify_sig()` and
   return `EXIT_FAILURE` with ADR-003 reference

#### Test Coverage

20 tests across 5 groups in `test_crypto.c`:

| Group | Tests |
|---|---|
| `sha256/nist` | FIPS 180-4 known answer: SHA-256("") |
| `sha256/vs_openssl` | Cross-validation vs OpenSSL: 1, 5, 55, 56, 64, 200, 256 bytes |
| `sha256/incremental` | Byte-by-byte matches one-shot; deterministic; different inputs differ |
| `block_hash` | Full 64-char output; nonce change; consensus change; NULL block |
| `merkle` | Empty → "0"; NULL args safe; full 64-char root; different amounts differ |

The 55-byte and 56-byte tests specifically exercise the two padding paths in
`sha256_final` (single-block vs two-block padding).

---

### ADR-008: CLI Design — Git-Like Command Dispatcher

**Date:** 2026-03 · **Status:** Adopted · **Files:** `src/main.c`

#### Context

`main.c` was a placeholder. The chain API was fully implemented and tested. A
user-facing CLI was needed.

Design goals:
- Mirror git's UX (subcommand dispatch, familiar flag names)
- Two-step transaction flow: `send` stages, `commit` builds the block
- No new source files — `main.c` only
- User output to stdout; operational logs to stderr via `log_*`

#### Decision — Command Set

| Command | Behaviour |
|---|---|
| `init` | `chain_load()` → creates genesis if first run; prints tip |
| `status` | Prints chain tip + count of staged transactions |
| `log [--limit N]` | `storage_scan` + one-liner per block (default 10) |
| `show <hash>` | Formatted block fields to stdout |
| `cat <hash>` | Raw field dump (all fields, PoW/PoS label) |
| `verify <hash>` | `block_verify_hash()` → prints `OK` or `FAIL` |
| `send --from <s> --to <r> --amount <a>` | Appends one line to `.chain/STAGED` |
| `commit` | Reads STAGED → `block_create` → `compute_merkle_root` → `mine_block` → `chain_add` → unlinks STAGED |
| `propose` | `chain_propose(c, c->head)` → reads `.chain/peers`, broadcasts via TLS |
| `version` | Prints version string |
| `help [command]` | Usage summary or per-command help |

Exit codes: `0` success · `1` usage/argument error · `2` chain/storage runtime
error.

#### Decision — Staging Area

Transactions are staged in `.chain/STAGED`, a tab-separated text file:

```
alice\tbob\t10500000\n
alice\tcarol\t5000000\n
```

- `send` appends one line and enforces the `MAX_TRANSACTIONS` (10) cap
- `commit` parses all lines, mines the block, then `unlink`s the file
- `status` counts newlines to report pending transactions
- `.chain/` is created by `chain_load()` first, so the directory always exists
  before STAGED is opened

The staging file is a runtime artefact — removed by `make clean`.

#### Decision — Dispatch Table

```c
typedef struct { const char *name; int (*fn)(int, char **); } Cmd;
static const Cmd CMDS[] = { {"init", cmd_init}, {"send", cmd_send}, … };
```

`main()` iterates the table with `strcmp`. Each handler does its own flag
parsing. Unknown commands print an error and return `1`.

#### Design Trade-offs

**Two-step `send` + `commit` vs single `transfer` command.** A single command
loses the ability to batch multiple transactions into one block. The two-step
model matches ADR-001's git analogy and how every real blockchain wallet works.

**`--from`/`--to` accept plain name strings.** When Dilithium signing is wired
in (ADR-003), these fields will hold public keys. The field size (`MAX_PUBLIC_KEY_LENGTH = 1952`) already accommodates a Dilithium-3 public key.

**`commit` mines PoW.** `cmd_commit` sets `b->consensus = CONSENSUS_POW` and
calls `mine_block(b, DIFFICULTY)`. Committed blocks are real PoW blocks and
pass `chain_validate`'s consensus check on the first attempt.

---

### ADR-009: Transaction Module — Dilithium Sizes, Integer Amounts, Replay Protection

**Date:** 2026-03 · **Status:** Adopted · **Files:** `inc/transaction.h`, `src/transaction.c`

#### Context — Bugs in Original transaction.h

| Issue | Severity | Detail |
|---|---|---|
| `MAX_SIGNATURE_LENGTH 512` too small | Critical | Dilithium-3 signatures are 3293 bytes — silent buffer overflow |
| `MAX_PUBLIC_KEY_LENGTH 1024` too small | Critical | Dilithium-3 public keys are 1952 bytes — same overflow risk |
| `double amount` | High | Floating-point is non-deterministic; two nodes could hash the same transaction to different values |
| No replay protection | High | Signed transaction could be rebroadcast indefinitely |
| `sign_transaction` returned `void` | High | OQS failures silently swallowed |
| `verify_transaction` return inverted | High | Returned `1` on success, `0` on failure — opposite of convention |

#### Decision — Updated Constants

```c
#define MAX_PUBLIC_KEY_LENGTH  1952          /* Dilithium-3 public key  */
#define MAX_SIGNATURE_LENGTH   3293          /* Dilithium-3 signature   */
#define MICRO_PER_TOKEN        1000000ULL    /* 1 token = 1,000,000 µ   */
#define TX_MESSAGE_LEN  (MAX_PUBLIC_KEY_LENGTH * 2 + sizeof(uint64_t) * 2)
```

`amount` is stored as `uint64_t` in **micro-units** (1 token = 1,000,000 µ).
This is the same convention used by Bitcoin (satoshis) — all hashing and
comparison operations are bitwise-identical across platforms.

A `uint64_t nonce` field adds **replay protection** — a per-sender sequence
number that must increase monotonically. Included in the signed message and
in the Merkle root so it cannot be stripped by an intermediary.

#### Decision — Transaction Struct

```c
typedef struct {
    char     sender[MAX_PUBLIC_KEY_LENGTH];     /* Dilithium-3 public key or label */
    char     recipient[MAX_PUBLIC_KEY_LENGTH];  /* Dilithium-3 public key or label */
    uint64_t amount;                            /* micro-units (MICRO_PER_TOKEN) */
    uint64_t nonce;                             /* per-sender sequence number */
    uint8_t  signature[MAX_SIGNATURE_LENGTH];
    size_t   signature_length;
} Transaction;
```

#### Decision — build_message Helper

A static `build_message()` constructs the canonical signed message using
**fixed-width fields** (not `strlen`):

```
sender[MAX_PUBLIC_KEY_LENGTH] | recipient[MAX_PUBLIC_KEY_LENGTH] | amount(8B) | nonce(8B)
```

Both `sign_transaction` and `verify_transaction` call `build_message` — they
always hash the same bytes for the same struct state, regardless of field
content.

#### Decision — Compile-Time Size Guard

```c
#if defined(OQS_SIG_dilithium_3_length_signature)
_Static_assert(MAX_SIGNATURE_LENGTH >= OQS_SIG_dilithium_3_length_signature,
               "MAX_SIGNATURE_LENGTH too small for Dilithium-3");
#endif
```

Catches size mismatches at compile time, not at runtime memory corruption.

#### Rationale

| Decision | Reason |
|---|---|
| `uint64_t amount` (micro-units) | Platform-independent; standard Bitcoin/Lightning convention |
| `uint64_t nonce` | Necessary for replay protection before Dilithium signing is wired in |
| Fixed-width `build_message` | Sign and verify always agree on message bytes |
| `_Static_assert` | Catches size regressions at compile time |
| `int` return from `sign_transaction` | Consistent with every other fallible function |

**Note on disk format:** These changes increase `sizeof(Transaction)` and
`sizeof(Block)`. Any `.chain/` created before this change holds binary files
in the old format. Old directories must be deleted and re-initialized.

---

### ADR-010: Consensus Module — Dispatch Table, PoW, PoS

**Date:** 2026-03 · **Status:** Adopted · **Files:** `inc/consensus.h`, `src/consensus.c`

#### Context — Bugs in Original consensus.c

| Issue | Severity | Detail |
|---|---|---|
| `verify_consensus()` never called | Critical | Chain accepted any block regardless of PoW difficulty — consensus completely bypassed |
| Wrong dispatch logic | Critical | `block->consensus % SWITCH_INTERVAL` routed PoW and PoS backwards |
| Always returns 0 | Critical | Validation was commented out; all blocks accepted unconditionally |
| `verify_signature()` declared but undefined | High | Linker error if called |
| `SWITCH_INTERVAL` conflicts with `Block.consensus` field | High | Automatic switching by block index conflicts with the explicit proposer-set field |

#### Decision — Dispatch Table

Replace the conditional with a function-pointer array indexed by
`block->consensus`:

```c
typedef int (*consensus_fn)(const Block *);

static const consensus_fn VERIFY[] = {
    [CONSENSUS_POW] = verify_pow_rules,
    [CONSENSUS_POS] = verify_pos_rules,
};

int verify_consensus(const Block *block) {
    if (!block || block->consensus >= NELEM(VERIFY) || !VERIFY[block->consensus])
        return EXIT_FAILURE;
    return VERIFY[block->consensus](block);
}
```

Adding a new consensus type requires only a new array entry and a new
`verify_*_rules` function — no changes to `verify_consensus()` itself.

#### Decision — Constants

```c
#define CONSENSUS_POW 0   /* Proof of Work  */
#define CONSENSUS_POS 1   /* Proof of Stake */
```

Replaces `SWITCH_INTERVAL`, `ProofOf`, and `ConsensusType` (all removed).

#### Decision — PoW Rules

`verify_pow_rules()` delegates to `validate_block_pow(block, DIFFICULTY)`,
which checks both the leading-zero difficulty target and `block_verify_hash()`.

#### Decision — PoS Rules (with Security Stubs)

`verify_pos_rules()` currently enforces hash integrity only:

```c
/* TODO (ADR-003): verify VRF proof — proposer must hold the slot token. */
/* TODO (ADR-003): verify Dilithium-3 block signature. */
/* TODO (ADR-002): verify proposer has sufficient registered stake. */
```

These stubs are present and accounted for — not silent omissions.

#### Decision — Wire into chain_validate

```c
/* After: full consensus enforcement */
if (verify_consensus(block) != EXIT_SUCCESS) {
    log_error("chain_validate: consensus check failed for block %u", block->index);
    return EXIT_FAILURE;
}
```

`chain_validate()` now enforces PoW difficulty and PoS hash integrity for every
`chain_add()`. The consensus bypass is closed.

#### Security Before vs After

| Before | After |
|---|---|
| Consensus bypass: any block accepted | `verify_consensus()` enforced in `chain_validate()` |
| PoW difficulty never checked | PoW blocks must satisfy `DIFFICULTY` leading zeros |
| `verify_signature()` undefined symbol | `verify_block_signature()` stub returns EXIT_FAILURE |
| Unknown types silently accepted | Bounds check rejects unknown types |

#### Pending Security Work (ADR-003)

1. VRF leader election — slot token proof verification
2. Dilithium-3 block signatures — wire `verify_block_signature()` to key registry
3. Stake threshold — minimum stake before a validator can propose
4. Equivocation guard — prevent proposing two blocks for the same slot

#### Test Coverage

8 tests in `test_consensus.c`:

| Group | Tests |
|---|---|
| `consensus/verify_consensus` | NULL block, PoW mined pass, PoW unmined fail, PoW tampered nonce fail, PoS valid hash pass, PoS tampered hash fail, unknown type=2 fail |
| `consensus/verify_block_signature` | stub always returns EXIT_FAILURE |

---

### ADR-011: Log Module — Heisenbug-Safe, Level-Gated, Secure

**Date:** 2026-03 · **Status:** Adopted · **Files:** `inc/log.h`, `src/log.c`

#### Context — Problems in Original log.h

| Issue | Impact |
|---|---|
| `malloc` on every log call | Changes heap layout across every call — any heap-dependent bug appears or disappears based on log verbosity (heisenbug) |
| Mutex held during `fflush` (synchronous kernel call) | All threads blocked; serialises the mining loop |
| `fflush` after every write including INFO | Measurable latency on every block hash and storage insert |
| `ENABLED` macro computed but never used | No runtime level filtering occurred |
| `DEBUG` guard broken | `log_debug` emitted output in release builds |
| Source paths in release builds | Internal code structure exposed in production logs |
| `##__VA_ARGS__` GNU extension | Compiler warning at every call site |

#### Decision — Stack-Only Formatting, Minimal Critical Section

All formatting happens on the calling thread's stack before the mutex is
acquired:

```
① level gate     — one comparison, no lock, no allocation
② format ts      — stack, no lock
③ format source  — stack, no lock
④ vsnprintf msg  — stack, no lock
⑤ assemble line  — stack, no lock
⑥ mutex_lock
⑦ fprintf        — I/O, lock held
⑧ fflush         — ERROR/WARN only, lock held
⑨ mutex_unlock
```

No `malloc` anywhere in the call path. The heap layout is identical whether
logging is active or suppressed — log-dependent heisenbugs are eliminated by
construction.

#### Decision — fflush Policy

| Level | Flushed? | Reason |
|---|---|---|
| ERROR, WARN | Yes | Critical failures must reach the operator even if the process crashes shortly after |
| INFO, DEBUG | No | INFO is emitted on every block hash and storage insert; synchronous flush would serialise the chain operations path |

#### Decision — Level Table

| Constant | Value | Compiled | Suppressible |
|---|---|---|---|
| `LOG_LEVEL_ERROR` | 0 | Always | No |
| `LOG_LEVEL_WARN` | 1 | Always | No (minimum clamp) |
| `LOG_LEVEL_INFO` | 2 | Always | Yes |
| `LOG_LEVEL_DEBUG` | 3 | Debug only (`-DDEBUG`) | N/A — `((void)0)` in release |

`log_set_level` clamps the minimum to `LOG_LEVEL_WARN`. ERROR and WARN cannot
be suppressed — operators can silence INFO for throughput without losing
consensus failure visibility.

#### Decision — Release Builds Omit Source Location

In release builds `__FILE__`, `__func__`, `__LINE__` are replaced with
`NULL, NULL, 0`. Log lines contain only timestamp, level, and message.
Internal source paths and function names are never written to log streams
that may be forwarded to external systems.

#### Security Policy

**Never** pass private keys, VRF secrets, seed material, Dilithium secret
keys, or session tokens to any log macro. Log output may be written to shared
storage or forwarded to external systems — treat all log content as public.

#### Remote Monitoring

`log_set_stream(FILE *)` accepts any `FILE*`, including one opened on a FIFO:

```c
log_set_stream(fopen("/tmp/kiat.log", "w"));
/* shell: tail -f /tmp/kiat.log | ssh user@monitor */
```

A dedicated TCP/UDP logging port is **not recommended** — it exposes mining
state (nonce trajectory, hash rate) to unauthenticated network observers,
enabling timing attacks and selfish-mining intelligence gathering.

---

### ADR-012: Integration Tests — Payment and Miner Use Cases

**Date:** 2026-03 · **Status:** Adopted · **Files:** `tests/test_integration_payment.c`, `tests/test_integration_miner.c`

#### Context

Unit tests cover modules in isolation. Integration tests verify that modules
compose correctly under realistic end-to-end scenarios.

Two primary use cases:
1. **Payment user** — sends tokens, commits to chain, verifies on-chain record
2. **Miner** — grows the chain with PoW blocks; chain rejects invalid blocks

#### Decision — Test Against the Library API Directly

Integration tests call the C library API directly, not through the CLI binary:
- Exercises the same code path as `cmd_send` + `cmd_commit`
- Failures are deterministic — no subprocess, no shell, no environment dependency
- Runs at the same speed as unit tests

#### Decision — Mine at DIFFICULTY=4

Each test that needs a valid PoW block calls `mine_block(b, DIFFICULTY)`
(DIFFICULTY=4). At 4 leading hex zeros, average hash count is ~65,000.
Three consecutive mines complete in under one second on any modern CPU.

#### Tamper Detection Scope

`block_verify_hash()` recomputes the hash over header fields: `index`,
`timestamp`, `previous_hash`, `merkle_root`, `nonce`, `consensus`. It does
**not** re-verify transaction bytes against the Merkle root.

- Tampering any **header field** (including `merkle_root`) after mining: detected
- Tampering a **transaction field** after `compute_merkle_root` but before
  `chain_add`: detected (Merkle root covers all tx fields)
- Tampering a transaction field **after** `chain_add` in the stored copy: not
  detected by `block_verify_hash` alone — full Merkle re-verification is
  pending future work

#### Test Coverage

**Payment** (6 tests):

| Group | Test | Scenario |
|---|---|---|
| `payment/send` | `test_alice_sends_50_to_bob` | Full PoW payment; block has correct sender, recipient, amount |
| `payment/send` | `test_chain_tip_advances_after_payment` | In-memory and on-disk HEAD both advance |
| `payment/batch` | `test_three_payments_in_one_block` | Three transactions in one block |
| `payment/multi_block` | `test_two_payment_blocks_in_sequence` | Block 2 links to Block 1; both independently readable |
| `payment/invalid` | `test_zero_amount_transfer_rejected` | `amount=0` fails `chain_validate` |
| `payment/invalid` | `test_tampered_merkle_root_rejected` | Flipping `merkle_root[0]` causes `chain_add` to return EXIT_FAILURE |

**Miner** (9 tests):

| Group | Test | Scenario |
|---|---|---|
| `miner/proof_of_work` | `test_mine_block_returns_success` | Hash has DIFFICULTY leading '0' chars; full 64-char hex |
| `miner/proof_of_work` | `test_mined_block_passes_pow_validation` | `validate_block_pow(b, DIFFICULTY)` returns EXIT_SUCCESS |
| `miner/proof_of_work` | `test_mined_block_accepted_by_chain` | `chain_add` advances tip and on-disk HEAD |
| `miner/proof_of_work` | `test_miner_includes_transactions` | Block with one transaction accepted; retrievable from storage |
| `miner/chain_growth` | `test_mine_three_consecutive_blocks` | Chain tip at index 3; all three readable |
| `miner/chain_growth` | `test_chain_link_integrity` | `b1.previous_hash == genesis.hash`; `b2.previous_hash == b1.hash` |
| `miner/security` | `test_unmined_pow_block_rejected` | `block_compute_hash` (no mining) → no leading zeros → rejected |
| `miner/security` | `test_tampered_hash_after_mining_rejected` | Flipping `hash[8]` triggers `block_verify_hash` mismatch |
| `miner/security` | `test_wrong_previous_hash_rejected` | Mined block with wrong previous hash fails link check |

---

### ADR-013: Build System — Test Summary and Coverage

**Date:** 2026-03 · **Status:** Adopted · **Files:** `Makefile`

#### Context

`make test` ran every suite but reported only per-suite exit codes. No
aggregated individual-test count. No coverage instrumentation.

#### Decision — Aggregated Summary

After all test binaries run, `make test` prints a table from cmocka's
group-summary lines. Current baseline (213 tests):

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

Every suite runs even if earlier ones fail. Non-zero exit from any suite causes
`make test` to return non-zero.

#### Decision — Coverage Target

`make coverage` builds all source and tests with `--coverage` into a separate
`build/cov/` tree (the normal debug build is never instrumented):

```
build/cov/
  src/          ← instrumented source objects
  tests/        ← instrumented test objects
  coverage.info ← lcov capture (if lcov installed)
  html/         ← genhtml HTML report (if lcov installed)
```

With `lcov` installed (`brew install lcov` / `apt install lcov`): full HTML
report at `build/cov/html/index.html`. Without lcov: raw gcov output per file.

`make check` runs both `test` then `coverage` — full validation gate for
releases.

#### Coverage Baseline (2026-03, 213 tests)

| File | Coverage |
|---|---|
| `src/block.c` | 100% |
| `src/consensus.c` | 100% |
| `src/sha256.c` | 100% |
| `src/pow.c` | ~97% |
| `src/log.c` | ~97% |
| `src/pos.c` | ~92% |
| `src/storage.c` | ~82% |
| `src/crypto.c` | ~80% |
| `src/transaction.c` | ~79% |
| `src/chain.c` | ~65% |
| `src/network.c` | ~35% |

Notable gaps:
- **`chain.c`** — GHOST fork-choice paths and some `chain_validate` error
  branches not yet exercised
- **`network.c`** — live TLS connections cannot be tested without a real
  server; argument-validation and serialization paths are covered; the accept
  loop is not

---

### ADR-014: Network Module — TLS Security, SOLID Rewrite

**Date:** 2026-03 · **Status:** Adopted · **Files:** `inc/network.h`, `src/network.c`

#### Context — Problems in Original network.c

**Security blockers:**

| # | Issue |
|---|---|
| S1 | No server certificate loaded — TLS handshake always failed |
| S2 | `SSL_VERIFY_NONE` default — MITM trivially possible |
| S3 | No minimum TLS version — downgrade attacks possible |
| S4 | 256-byte receive buffer — Dilithium-3 signature alone is 3293 bytes; blocks silently truncated |
| S5 | `SSL_read()` return value ignored |
| S6 | No input validation on received bytes |
| S7 | `SSL_shutdown()` called once — TLS half-close; peer in undefined state |
| S8 | Deprecated OpenSSL 1.x init API |

**Architecture:**
- Global `SSL_CTX *ctx` — SRP and OCP violation
- Six `exit()` calls in library code
- Hard-coded `SERVER_IP` / `SERVER_PORT` — OCP violation
- `serialize_block` and `broadcast_block` commented out — disconnected
- All output via `printf` — bypassed `log_*` infrastructure

#### Decision

Complete rewrite with the following contracts:

**No global state.** `NetContext` is opaque. Each context is fully independent.

**`NetConfig`-driven.** Single struct for cert paths, CA bundle, bind address,
port, and PQC group. No compile-time constants for runtime values.

**Security-first:**
- TLS 1.3 minimum enforced on every context
- PQC hybrid group (`p256_kyber768`) set when `cfg->pqc_group != NULL`
- Client contexts always enable `SSL_VERIFY_PEER`
- Server contexts optionally enable mutual TLS when `ca_file` is supplied
- Bidirectional `tls_shutdown()` on every connection close
- Receive buffer `NET_BLOCK_BUF_SIZE = 4096` (sufficient for header-only broadcast)

**No `exit()` calls.** All functions return error codes.

**Header-only broadcast.** Only block header fields are serialized and sent.
Full transaction bodies are fetched on demand. This keeps broadcast payloads
small and avoids transmitting unvalidated Dilithium signatures over the wire
before the receiving peer has validated the block header.

**Single-threaded server.** `net_server_run()` handles one connection at a
time — appropriate for Raspberry Pi / low-concurrency nodes. Threading can be
added later without changing the API.

#### API

```c
NetContext *net_context_server(const NetConfig *cfg);
NetContext *net_context_client(const NetConfig *cfg);
void        net_context_free(NetContext *ctx);               /* NULL-safe */

int net_serialize_block  (const Block *block, char *buf, size_t bufsz);
int net_deserialize_block(const char *buf, size_t len, Block *out);
int net_broadcast_block  (NetContext *ctx, const Block *block,
                          const char *peer_addr, uint16_t peer_port);
int net_server_run       (NetContext *ctx, Chain *chain);
```

`net_server_run(ctx, chain)` — when `chain != NULL`, received blocks are
deserialized, validated via `block_verify_hash`, and added to the chain via
`chain_add`. When `chain == NULL`, received blocks are logged and discarded
(monitor/inspection mode).

`net_deserialize_block` parses the `key:value\n` wire format and calls
`block_verify_hash` on the result before returning — integrity is enforced at
the deserialization layer.

#### PQC Group Note

`p256_kyber768` is a hybrid P-256 + Kyber-768 group. If classical ECC is
broken by a quantum adversary, Kyber-768 still provides security. If Kyber is
broken, P-256 still provides classical security. This defence-in-depth is the
standard NIST-recommended transition approach.

The OQS OpenSSL provider must be loaded at runtime for PQC groups to be
available. Tests use `pqc_group = NULL` to avoid a hard dependency on the
provider in CI environments.

#### Test Coverage

25 tests across 5 groups in `test_network.c`:

| Group | Tests |
|---|---|
| `network/context/server` | NULL config, null cert, null key, missing cert file, valid config, free(NULL) |
| `network/context/client` | NULL config, no cert required, missing ca_file |
| `network/serialize` | NULL block, NULL buf, zero bufsz, buf too small, basic (field tags), NUL-terminated, distinct blocks |
| `network/deserialize` | NULL buf, zero len, NULL out, garbage (hash verify fails), round-trip (serialize → deserialize, fields match) |
| `network/broadcast` | NULL ctx, NULL block, NULL addr, invalid IP |

---

### ADR-015: Dead Code Removal — iterator.h and common.h

**Date:** 2026-03 · **Status:** Adopted

#### Context

Two header files in `inc/` were unused or contained broken macro references.

**`inc/iterator.h`:**
- Declared `iterate()` and `next_block()` with no corresponding `iterator.c`
- Not `#include`d anywhere
- `Iterator.next` duplicated `Block.next` from `block.h` without adding value

**`inc/common.h`:**
- `CHECKNULL(x)` and `CHECKZERO(x)` called `Warn(...)` which was undefined
- `Info` and `Warn` were undefined identifiers — file would fail to compile
- `START`/`FAIL`/`RETURN` macros were incompatible with the existing codebase
  (see analysis below)
- Not `#include`d anywhere

#### Decision

Both files deleted. Block traversal is done via `Block.next` directly. If a
proper iterator abstraction is needed, it should be implemented as a matching
`iterator.c` + `iterator.h` pair with tests.

#### Why common.h Was Not Salvageable

After a full applicability audit, `START`/`FAIL`/`RETURN` macros cannot be
safely applied to any existing function:

1. **Resource cleanup** — `storage.c`, `chain.c`, `transaction.c`, and `main.c`
   acquire heap/file/OQS objects mid-function; `FAIL` breaks out immediately,
   causing leaks
2. **Loops with internal checks** — `chain_validate`, `storage_scan` have
   failure checks inside loops; `FAIL` would break the loop, not the scope
3. **Return type mismatches** — functions return `Block*`, `void`, or custom
   exit codes (`0/1/2`); `RETURN` (which returns `int STATUS`) is wrong for all
4. **Log noise** — `START` emits `log_info("Start >")` on every call,
   unsuitable for frequently-called functions like `block_verify_hash`
5. **Zero consumers** — the file was never `#include`d anywhere

---

### ADR-016: Deployment — Raspberry Pi + USB SSD, Native systemd

**Date:** 2026-03 · **Status:** Adopted

#### Decision

kiat nodes are deployed natively on **Raspberry Pi 4 or 5** with an
**attached USB SSD** (not microSD). No Docker in production. Docker Compose
is permitted only for local development.

#### Hardware

| Component | Choice | Rationale |
|---|---|---|
| Board | Raspberry Pi 4 (4 GB) or Pi 5 (8 GB) | Sufficient RAM; USB 3.0 for SSD; widely available |
| Storage | USB 3.0 SSD (≥ 64 GB) | MicroSD has ~10k write cycles — will fail under `.chain/` write load; SSD endures millions |
| Network | Gigabit Ethernet (preferred) | Wired is more reliable for P2P sync |
| Power | Official Pi PSU (5 V / 3 A) | Undervoltage causes data corruption on SSD writes |

**Never use microSD for the `.chain/` directory on a production node.**
Symlink or bind-mount `.chain/` to the SSD path from day one.

#### Software Stack

```
Raspberry Pi OS Lite (64-bit, Debian-based)
├── kiat binary          → /usr/local/bin/kiat
├── start_chain binary     → /usr/local/bin/start_chain
├── systemd unit           → /etc/systemd/system/kiat.service
└── chain data             → /mnt/ssd/chain/
    ├── .chain/            → kiat working directory
    ├── tls-cert.pem       → node TLS certificate
    ├── tls-key.pem        → node TLS private key (chmod 600)
    └── peers              → one ip:port per line
```

WireGuard mesh: `/etc/wireguard/wg0.conf`

#### systemd Unit

```ini
[Unit]
Description=kiat P2P node
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/mnt/ssd/chain
ExecStart=/usr/local/bin/start_chain 8333 tls-cert.pem tls-key.pem
Restart=on-failure
RestartSec=5
User=kiat
ProtectSystem=full
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

#### Multi-Node Mesh (WireGuard)

WireGuard creates a private mesh between Pi nodes on different networks. Each
node's `.chain/peers` lists WireGuard IP addresses:

```
# .chain/peers
10.0.0.2:8333
10.0.0.3:8333
```

This eliminates the need to expose port 8333 to the public internet — all
inter-node traffic traverses the encrypted WireGuard tunnel.

#### TLS Certificate Strategy

| Environment | Certificate | Notes |
|---|---|---|
| Single-node / dev | Self-signed (`openssl req -x509`) | CA bundle must be distributed to all peers |
| Multi-node mesh | Self-signed CA + per-node certs | Local CA cert; sign each node cert; distribute CA to all peers |
| Public network | Let's Encrypt (if DNS name) | Zero-config peer verification via system CA store |

#### Avoiding Docker in Production

Docker adds container overhead (memory, I/O layers) and complicates systemd
integration on a single-board computer. The `kiat` binary has no runtime
dependencies beyond OpenSSL and liboqs — it runs directly under systemd.

Docker Compose is useful for spinning up a local multi-node test environment
on a development machine.

#### Rationale

| Concern | Solution |
|---|---|
| Storage durability | USB SSD: millions of write cycles vs microSD ~10k |
| Energy | Pi runs ~5 W idle; full PoW mining adds ~2–3 W |
| Security | TLS 1.3 + WireGuard on all inter-node traffic |
| Quantum resistance | p256_kyber768 TLS key exchange when OQS provider loaded |
| Operations | systemd handles restart, logging (journald), boot startup |
| Isolation | WireGuard mesh avoids exposing node ports to the public internet |

---

## Part IV — Pending Work

Items that are designed (ADR adopted) but not yet implemented:

### High Priority

| Work Item | ADR | Location |
|---|---|---|
| Validator registry (public keys + stake) | ADR-003 | New `validator.c` or extension of `storage.c` |
| VRF leader selection | ADR-003 | Build from PQC primitives; Algorand VRF is a reference |
| Dilithium-3 block signatures | ADR-003 | Wire `verify_block_signature()` in `consensus.c` to key registry |

### Medium Priority

| Work Item | ADR | Notes |
|---|---|---|
| Stake threshold enforcement | ADR-010 | Minimum stake before a validator can propose (Sybil resistance) |
| Equivocation guard | ADR-010 | Prevent proposing two blocks for the same slot |
| `net_server_run()` receive side | ADR-014 | Transaction body fetch on demand (headers only broadcast today) |
| Full Merkle re-verification on read | ADR-012 | `compute_merkle_root` + compare when reading from storage |

### Low Priority / Future

| Work Item | Notes |
|---|---|
| OQS OpenSSL provider runtime loading | Required for `NET_PQC_GROUP = p256_kyber768` in production |
| Multi-threaded `net_server_run()` | One connection at a time today; pthread-based fan-in for higher concurrency |
| Transaction body fetch protocol | Peers request full transaction bodies by block hash after accepting the header |
| `make check` in CI | GitHub Actions or similar; run `make check` on every PR |
