# QuteChain — Architecture & Design Notes

This document captures key architectural decisions and design discussions for the QuteChain (`blocky`) project. Update this file whenever a concept, decision, or trade-off is discussed that affects the overall design.

---

## ADR-001: Git-Like Blockchain Model

**Date:** 2025-03
**Status:** Adopted

### Context

The goal of QuteChain is to create a blockchain that operates like git. The workflow mirrors common git operations:

| Git operation | Blockchain equivalent |
|---|---|
| `git init` | `startchain` — initialize the chain |
| `git checkout <branch>` | Select a chain tip (parent block) to build on |
| `git commit` | Build a new block with transactions |
| `git commit -S` | Sign the block with a PQC key (Dilithium) |
| `git push` / `git propose` | `propose` — broadcast block to the network for validation |
| Branch (pointer to commit) | Chain tip / fork |
| Competing branches | Chain forks resolved by canonical chain rule |
| Merge | Consensus — one branch becomes canonical |

### Decision

Design the user-facing workflow and internal data model around git semantics:

1. A node **checks out** the current heaviest chain tip (selects a parent block).
2. The node **builds** a new block on top of it (adds transactions, computes hashes).
3. The node **proposes** the block to the network (`propose`).
4. Peers **validate** and, if accepted, extend the chain.

### Rationale

- Both git and blockchains are content-addressed DAGs — commits and blocks are identified by the hash of their content plus their parent's hash.
- The mental model is familiar to developers.
- Explicit fork management (intentional branching) is cleaner than implicit forks from simultaneous mining.
- The existing CLI tools (`startchain`, `proposeblock`, `insertblock`) already reflect this workflow.

---

## ADR-002: Canonical Chain Rule — GHOST

**Date:** 2025-03
**Status:** Adopted

### Context

When two valid competing chain branches exist (a fork), the network needs a deterministic rule to decide which branch is canonical. Options considered:

- **Longest chain (Nakamoto)** — pick the branch with the most blocks.
- **GHOST** — pick the branch whose subtree has the most total descendant blocks.
- **BFT finality (Tendermint)** — require a 2/3 supermajority vote; no forks after finality.
- **First-seen / FIFO** — trivially gameable, rejected.

### Decision

Adopt **GHOST** (Greedy Heaviest Observed Subtree) as the canonical chain rule.

**How it works:**
At a fork point, count all descendant blocks in each branch's subtree. The branch with the greater subtree weight wins — not just the longest tip.

```
A → B → C → D → E        (subtree weight: 5)
              ↘ X → Y    (subtree weight: 2)
```
GHOST selects the branch ending at `E`.

Block weight can be:
- `1` per block (PoW mode — uniform weight)
- `stake amount` of the proposer (PoS mode — stake-weighted)

The `consensus` field in `Block` already distinguishes these two modes.

### Rationale

- **Best fit for the git analogy.** The "heaviest branch" wins, just as `main` wins in a real project because more contributors build on it. GHOST formalizes this.
- **Fork resistant.** Unlike longest-chain, GHOST counts "uncle" blocks (valid blocks that arrived late) as weight, making fork attacks significantly more expensive.
- **Works with both PoW and PoS** — weight metric is configurable.
- **No energy cost on its own** — GHOST is a selection rule, not a consensus mechanism. It operates on top of whatever is used for block proposals.
- **Production-validated.** LMD-GHOST is the fork choice rule used by Ethereum's Beacon Chain.

### Implementation Notes

- GHOST fork choice logic belongs in `blockchain.c`, likely as a helper called inside `validate()` or a new `fork_choice()` function.
- The `chain.h` / `chain.c` split (currently untracked) may be the right home for this.

---

## ADR-003: Proposer Requirements — Stake-Locked PQC Signature + VRF

**Date:** 2025-03
**Status:** Adopted

### Context

A proposer is a node that builds and broadcasts a new block. The network needs a mechanism to:
1. Prevent Sybil attacks (fake nodes spamming proposals).
2. Ensure proposals are quantum-resistant.
3. Make proposer selection unpredictable to prevent manipulation.
4. Stay feasible on low-power hardware (Raspberry Pi).

Options considered:

- **Proof of Work** — rejected; energy-intensive and uncompetitive on Pi hardware.
- **Proof of Authority** — simple round-robin of known validators; no token economics needed but centralized.
- **Stake-locked PQC signature** — proposer must hold locked stake and sign with a PQC key.
- **Stake + VRF selection** — adds unpredictable, verifiable leader election on top of stake.

### Decision

A valid proposer must satisfy all three conditions:

1. **Minimum stake locked** in the validator registry.
2. **Selected for the current slot** by a VRF (Verifiable Random Function) — provably random, publicly verifiable, unpredictable in advance.
3. **Block signed** with their **Dilithium** post-quantum key (available in `liboqs`).

**Full propose flow:**

```
Validator locks stake (validator registry)
        ↓
VRF selects proposer for slot N
        ↓
Proposer: checkout heaviest GHOST tip
        ↓
Proposer: build block, sign with Dilithium key
        ↓
Proposer: broadcast to peers  (propose)
        ↓
Peers validate:
  ✓ VRF proof valid?
  ✓ Dilithium signature valid?
  ✓ Proposer has sufficient stake?
  ✓ Block extends correct GHOST tip?
        ↓
Block accepted → GHOST subtree weight updated
```

### Rationale

| Concern | Solution |
|---|---|
| Raspberry Pi energy constraints | No PoW — signing a block is computationally cheap |
| Quantum resistance | Dilithium signature replaces ECDSA (already in `liboqs`) |
| Sybil resistance | Stake lockup — spinning up fake validators costs real tokens |
| Selfish proposing | VRF — no validator can predict or game when they will be selected |
| Git analogy | Signing a block mirrors `git commit -S` (signed commits) |

### Implementation Notes

- **Dilithium** is the target signature scheme; it is already available via `liboqs`. The README currently lists MSS — Dilithium should replace or supplement it as the primary signing algorithm for proposals.
- **VRF** is not directly provided by `liboqs` but can be constructed from existing PQC primitives (hash + keypair). Algorand's VRF construction is a useful reference.
- **Validator registry** is a new data structure needed in `storage.c` or a dedicated `validator.c` — tracks public keys and locked stake per validator.
- This design is architecturally equivalent to a simplified Ethereum Beacon Chain (LMD-GHOST + Casper-FFG stake), which is a useful reference implementation.

---

## ADR-004: Language Stack and Implementation Philosophy

**Date:** 2026-03
**Status:** Adopted

### Decision

- **Core implementation language: C.** All fundamental units (block, chain, storage, crypto, consensus, network) are written in C.
- **C++ and Python: as needed.** Permitted for tooling, scripting, or components where they provide a clear advantage, but not for core chain logic.
- **Native-first:** Minimize external, unmanaged libraries. Prefer standard library and OS primitives. External dependencies must be justified — currently only `liboqs` (PQC, no native alternative) and `OpenSSL` (TLS) are accepted.

### Unit Testing Convention

Every unit (a `.h`/`.c` pair) **must** have a corresponding test file in `tests/test_<unit>.c` using **CMocka**.

**Test file structure** (established by `test_storage.c` and `test_blockchain.c`):

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

// setup: runs before each test, allocates shared state via **state
static int setup(void **state) { ... return 0; }

// teardown: runs after each test, frees state and cleans up side effects
static int teardown(void **state) { ... return 0; }

// individual test functions
static void test_name(void **state) { ... }

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_name, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
```

**Rules:**
- One test file per unit: `tests/test_<unit>.c`
- Test functions are `static void` — they use `assert_*` macros, never `return` an error code
- `setup` and `teardown` clean up all side effects (files, memory, `.chain/` directory)
- Tests link against debug objects (no `main.o`) via the Makefile `test` target
- `make test` must pass before any code is considered complete

### Rationale

- C keeps the implementation close to the metal, which is essential for Raspberry Pi performance.
- Native-first reduces dependency management burden and improves portability across Linux/macOS.
- Per-unit CMocka tests enforce a clear boundary between units and catch regressions early.
- The clang pragma guard pattern (already in existing tests) ensures the test files compile cleanly under both GCC and Clang.

---

## ADR-005: Storage Module Design — Git-Style Object Store and Ref Management

**Date:** 2026-03
**Status:** Adopted
**Files:** `inc/storage.h`, `src/storage.c`, `tests/test_storage.c`

### Context

The storage module is responsible for persisting blocks to disk and tracking the current chain tip. The original implementation had the following problems that needed to be resolved:

- `storage_head()` returned a heap-allocated `char *` that callers consistently forgot to free — memory leak on every call.
- `storage_move()` called `memcmp` on the potentially NULL return value of `storage_head()` — undefined behavior.
- `storage_scan()` returned a count that was always one less than the actual number of blocks read (off-by-one at genesis detection).
- `fwrite` and `fread` return values were not checked — silent data corruption went undetected.
- `storage_insert()` always updated HEAD — object storage and ref management were incorrectly coupled.
- `HEAD` stored the raw block hash directly, diverging from git's symbolic ref model. The `refs/` directory was created by `init()` but never used.
- No way to check block existence without allocating and reading the entire block.

### Decision — On-Disk Layout

The `.chain/` directory mirrors git's `.git/` repository structure exactly:

```
.chain/
├── blocks/              ← object store: one binary file per block, named by hash
│   ├── a1b2c3d4...      ← serialized Block struct
│   └── ...
├── refs/
│   └── heads/
│       └── main         ← plain text file containing the tip block hash
└── HEAD                 ← plain text file containing "ref: refs/heads/main"
```

**HEAD resolution chain:**
```
HEAD  →  "ref: refs/heads/main"  →  .chain/refs/heads/main  →  <block hash>
```

This two-level indirection mirrors git exactly:
- `HEAD` is a **symbolic ref** pointing to the current branch name.
- The branch file (`refs/heads/main`) holds the actual block hash.
- `storage_checkout()` updates the **branch file**, not `HEAD` itself — identical to how `git checkout` works.
- A detached HEAD (HEAD contains a raw hash directly) is also supported, for the case where a node is inspecting a specific block without being on a named branch.

### Decision — Public API

Six public functions, intentionally minimal:

| Function | Purpose |
|---|---|
| `storage_insert(block)` | Write block to object store only. Idempotent. Does **not** update HEAD. |
| `storage_read(hash)` | Read and return a block by hash. Caller frees. |
| `storage_exists(hash)` | Check if block file exists. No allocation, no file open. |
| `storage_head(buf, size)` | Resolve HEAD → ref → hash into caller-provided buffer. |
| `storage_checkout(hash)` | Advance the current branch tip to hash. Verifies block exists first. |
| `storage_scan(offset, count)` | Walk chain backwards from HEAD, return array of hashes. |

Two internal helpers (not exported):
- `ref_read(path, buf, size)` — read one line from a ref file, strip newline.
- `ref_write(path, value)` — write a value to a ref file with `fflush` + `fsync`.

### Key Design Principles

**Insert and checkout are separate operations.**
`storage_insert()` writes a block to the object store and returns. It does not touch HEAD or any ref. The caller explicitly calls `storage_checkout()` to advance the chain tip. This matches git's `git hash-object` vs `git update-ref` separation and is critical for handling incoming blocks from the network — a node must be able to store a received block for validation without immediately making it HEAD.

**Caller-owned buffers for `storage_head()`.**
Instead of allocating and returning a `char *` (which callers forget to free), `storage_head()` fills a caller-provided buffer. This eliminates the class of memory leaks entirely.

**`storage_checkout()` verifies existence.**
Before updating any ref, `storage_checkout()` calls `storage_exists()` to confirm the target block is actually on disk. This prevents HEAD from pointing to a nonexistent block — equivalent to git refusing to checkout a commit that isn't in the object store.

**`storage_exists()` uses `access(F_OK)` — no allocation.**
Checking for block existence is a common operation (deduplication, pre-validation). Using `access()` avoids opening the file or allocating memory, making it safe to call frequently.

**Durability: `fsync` on all writes.**
Both block files and ref files are flushed with `fflush()` + `fsync()` before `fclose()`. This ensures that on a crash (power loss on Raspberry Pi), neither a partial block write nor a partial HEAD update can leave the chain in a corrupt state. A block file that fails to write completely is deleted via `remove()` before returning failure.

**Idempotent insert.**
If a block with the same hash already exists on disk, `storage_insert()` returns `EXIT_SUCCESS` immediately without re-writing. This makes re-processing safe (e.g., network retransmits).

**Genesis sentinel.**
The genesis block's `previous_hash` is set to the string `"0"` (a single ASCII zero character followed by null). The `GENESIS_PREVIOUS_HASH` constant is exported from `storage.h` so all modules use the same sentinel consistently. `storage_scan()` detects genesis by checking `previous_hash[0] == '0' && previous_hash[1] == '\0'`.

### `storage_scan()` Walk Semantics

Walks the chain **backwards** from HEAD (newest → oldest), following `previous_hash` links. Returns an array of heap-allocated hash strings. Caller frees each entry and then the outer array.

```
HEAD → block[4] → block[3] → block[2] → block[1] → block[0 / genesis]
scan(offset=0, count=5) → [hash4, hash3, hash2, hash1, hash0]
scan(offset=2, count=5) → [hash2, hash1, hash0]          ← skips 2 from HEAD
```

`*count` is updated to the actual number returned. Returns `NULL` if the chain is empty or offset exceeds the chain length.

### Test Coverage (`tests/test_storage.c`)

22 tests across 6 named groups, each with isolated `setup`/`teardown`:

| Group | Tests |
|---|---|
| `insert` | null block, empty hash, valid block, duplicate (idempotent) |
| `read` | valid block (field verification), null hash, empty hash, nonexistent hash |
| `exists` | after insert, nonexistent, null hash |
| `head` | before checkout (empty ref → FAILURE), after checkout, invalid buffer |
| `checkout` | null hash, empty hash, nonexistent hash, valid hash, already at head (no-op) |
| `scan` | null count, empty chain, single block, multiple blocks (order), count capped, offset, offset past genesis |

Three setup functions provide isolation:
- `setup_empty` — removes `.chain/`, no state
- `setup_genesis` — inserts + checks out a genesis block
- `setup_chain` — builds a 5-block linked chain, HEAD at tip

---
