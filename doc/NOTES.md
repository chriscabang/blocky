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

## ADR-006: Block/Chain Module Split — Pool Allocator and Runtime Pointer Fix

**Date:** 2026-03
**Status:** Adopted
**Files:** `inc/block.h`, `src/block.c`, `inc/chain.h`, `src/chain.c`

### Context

`blockchain.c` was a monolith that mixed the `Block` data type, single-block operations, and chain-level operations. It also had two architectural bugs:

1. **`Block.next` written to disk.** `storage_insert()` called `fwrite(block, sizeof(Block), 1, file)` — the runtime `next` pointer was serialised as raw memory. When read back via `fread`, the field contains a garbage address. `validate()` dereferenced this garbage pointer, causing a crash.
2. **Hidden global state.** `Block *chain = NULL` was a file-scoped global, making multiple chain instances and clean teardown in tests difficult.

### Decision

Split `blockchain.c/.h` into two units:

| Unit | Responsibility |
|---|---|
| `block.h / block.c` | `Block` data type + single-block operations (`create`, `compute_hash`, `verify_hash`, `free`) |
| `chain.h / chain.c` | In-memory chain with pool allocator; public chain API (`load`, `unload`, `validate`, `add`, `propose`, `info`, `show`, `list`) |

`blockchain.h` becomes a compatibility shim that includes both headers — existing code that includes `blockchain.h` continues to compile.

### Pool Allocator Design

```c
#define CHAIN_POOL_SIZE 64

typedef struct {
  Block   *head;
  Block    pool[CHAIN_POOL_SIZE];
  uint8_t  pool_used[CHAIN_POOL_SIZE];
} Chain;
```

- `Chain` is heap-allocated once by `chain_load()`. The pool is embedded — no per-block `malloc` after init.
- `pool_alloc(c)` scans `pool_used` for a free slot, marks it, returns `&pool[i]`.
- `pool_free(c, b)` uses pointer arithmetic (`b - c->pool`) to locate and clear the slot.
- At steady state only **one slot is occupied**: the current chain tip. When `chain_add()` installs a new block, the old head slot is freed immediately after (it is already persisted to disk).
- Pool exhaustion returns `EXIT_FAILURE` from `chain_add()`. With CHAIN_POOL_SIZE = 64 this is unreachable in normal operation but tested explicitly.

### `Block.next` Fix

`Block.next` is a **runtime-only** pointer. It must never reach disk:

- `storage_insert()` writes a **stack copy** of the block with `copy.next = NULL`.
- `storage_read()` and `storage_read_into()` set `block->next = NULL` after `fread`.
- `chain_validate()` and `chain_add()` never dereference `next`; they rely on `previous_hash` for chain linkage.

### Removed API

`load()`, `validate()`, `unload()`, `add()`, `propose()`, `info()`, `show()`, `list()` are removed. Their replacements are prefixed with `chain_`:

| Old | New |
|---|---|
| `load()` | `chain_load()` |
| `validate(block)` | `chain_validate(c, block)` |
| `unload()` | `chain_unload(c)` |
| `add(block)` | `chain_add(c, block)` |
| `propose(block)` | `chain_propose(c, block)` (stub) |
| `info()` | `chain_info(c)` |
| `show(hash)` | `chain_show(c, hash)` |
| `list(n)` | `chain_list(c, n)` |

### Rationale

- **No C++ / smart pointers needed.** A C pool allocator gives the same zero-allocation-after-init benefit with no ABI complications and is compatible with the native-first (ADR-004) constraint.
- **Explicit `Chain *` parameter** eliminates hidden global state, makes tests clean (each test creates/destroys its own `Chain`), and allows multiple chain instances.
- **`block_verify_hash()`** verifies integrity without modifying the original block: copies to stack, calls `hash()` on the copy, compares. This is the correct pattern for validation.
- **`storage_read_into(hash, out)`** fills a pool slot directly — no intermediate `malloc`/`free` for the common case.

### Test Coverage

**`tests/test_block.c`** — 12 tests across 4 groups:

| Group | Tests |
|---|---|
| `create` | genesis sentinel, non-genesis (copies prev hash), sets timestamp, next is NULL |
| `compute` | hash is non-empty after call, deterministic (same fields → same hash) |
| `verify` | NULL block, valid block, corrupted hash field, tampered header field |
| `free` | NULL (no crash), valid block |

**`tests/test_chain.c`** — 15 tests across 4 groups:

| Group | Tests |
|---|---|
| `load` | creates genesis on empty chain, loads existing HEAD, two independent loads agree |
| `unload` | NULL is safe |
| `validate` | NULL chain, NULL block, valid next block, wrong previous_hash, tampered hash |
| `add` | NULL args, advances head, updates storage HEAD, `next` is NULL on disk (key fix), invalid block rejected, pool exhaustion |

---

## ADR-008: CLI Design — Git-Like Command Dispatcher in `main.c`

**Date:** 2026-03
**Status:** Adopted
**Files:** `src/main.c`

### Context

`main.c` was a placeholder that checked argument count and printed a version string. The chain API (`chain.h`, `block.h`, `storage.h`, `crypto.h`) was fully implemented and tested. A user-facing CLI was needed to expose it.

The design goals were:
- Mirror git's UX (subcommand dispatch, familiar flag names)
- Two-step transaction flow: `send` stages, `commit` builds the block (like `git add` + `git commit`)
- No new source files or headers — `main.c` only
- Clean separation: user-facing output to stdout, operational logs to stderr via `log_*`

### Decision — Command Set

| Command | Behaviour |
|---|---|
| `init` | `chain_load()` → creates genesis if first run; prints tip |
| `status` | Prints chain tip + count of staged transactions |
| `log [--limit N]` | `storage_scan` + one-liner per block (default 10) |
| `show <hash>` | Formatted block fields to stdout |
| `cat <hash>` | Raw field dump (all fields, including PoW/PoS label) |
| `verify <hash>` | `block_verify_hash()` → prints `OK` or `FAIL` |
| `send --from <s> --to <r> --amount <a>` | Appends one line to `.chain/STAGED` |
| `commit` | Reads STAGED → `block_create` → `compute_merkle_root` → `chain_add` → unlinks STAGED |
| `propose` | Stub; prints "not yet implemented" |
| `version` | Prints version string, no chain load |
| `help [command]` | Usage summary or per-command help |

Exit codes: `0` success · `1` usage/argument error · `2` chain/storage runtime error.

### Decision — Staging Area

Transactions are staged in `.chain/STAGED`, a tab-separated text file:

```
alice\tbob\t10.000000\n
alice\tcarol\t5.000000\n
```

- `send` appends one line and enforces the `MAX_TRANSACTIONS` (10) cap.
- `commit` parses all lines, builds the block, then `unlink`s the file.
- `status` counts newlines in the file to report pending transactions.
- `.chain/` is created by `chain_load()`, which `send` and `commit` call first, so the directory always exists before STAGED is opened.

The staging file is a runtime artefact — it is never committed to git and is removed by `make clean` (which deletes `.chain/`).

### Decision — Dispatch Table

```c
typedef struct { const char *name; int (*fn)(int, char **); } Cmd;
static const Cmd CMDS[] = { ... };
```

`main()` iterates the table with `strcmp` and calls the matching handler with the full `argc`/`argv`. Each handler does its own flag parsing. Unknown commands print an error and return 1.

### Design Trade-offs

**Two-step `send` + `commit` vs single `transfer` command.**
A single command would be simpler but loses the ability to batch multiple transactions into one block. The two-step model is consistent with ADR-001's git analogy and is how every real blockchain wallet works.

**`--from`/`--to` accept plain name strings.**
For now, sender and recipient are stored as-is in `Transaction.sender`/`.recipient` (both `char[1024]`). When Dilithium signing is wired in (ADR-003), these fields will hold public keys. The field size already accommodates a PQC public key — the label strings used now are a temporary convenience.

**`chain_info()`/`chain_list()`/`chain_show()` are not used by the CLI.**
Those functions output via `log_info` (stderr) and were designed as internal diagnostic aids. The CLI commands (`status`, `log`, `show`) re-implement output with `printf` to stdout for proper UX. The chain API functions are retained for programmatic/debug use.

**`propose` is a stub.**
It prints "not yet implemented" and returns 0. Wiring it to real network broadcast (ADR-003) is deferred until the validator registry and VRF are implemented. A stub exit code of 0 (not 2) avoids breaking scripts that probe whether the command exists.

### Rationale

- Keeping everything in `main.c` (no new headers/source) respects ADR-004's native-first, minimum-complexity principle. The CLI is ~280 lines; abstraction layers would add more lines than they save.
- A static dispatch table is the simplest correct pattern for a small, fixed command set. `strcmp` over 11 entries has negligible cost.
- Tab-separated text for STAGED is human-readable, trivially parseable with `strchr`, and requires no external library.

---

## ADR-007: Crypto Module — Self-Contained SHA-256, Remove OpenSSL SHA Dependency

**Date:** 2026-03
**Status:** Adopted
**Files:** `inc/sha256.h`, `src/sha256.c`, `inc/crypto.h`, `src/crypto.c`, `tests/test_crypto.c`

### Context — Bugs Found in Original crypto.c

A review of the original `crypto.c` found the following issues:

| Bug | Severity | Detail |
|---|---|---|
| Hash truncated to 32 chars | Critical | `memcpy(block->hash, hash_hex, SHA256_DIGEST_LENGTH)` copied 32 of 64 hex chars. SHA-256 output was silently halved. |
| `consensus` field omitted from hash | High | Changing PoW↔PoS produced the same block hash. |
| `compute_merkle_root` used only `sender` | High | `recipient` and `amount` were ignored — two different transactions with the same sender produced the same Merkle root. |
| Deprecated OpenSSL SHA API | Medium | `SHA256_Init/Update/Final` deprecated in OpenSSL 3.x; EVP interface required. |
| NULL dereference before NULL check | Medium | `log_info("Hashing block %u", block->index)` ran before `if (block == NULL)`. |
| `sign()`/`verify()` declared, not defined | Medium | Calling either function would cause a linker error. |
| `puts()` in `compute_merkle_root` | Low | Bypassed the log framework. |
| No `#ifndef` guard on `HASH_SIZE` | Low | Double-define if included after `storage.h`. |

### Decision — Self-Contained SHA-256

Replace OpenSSL's SHA-256 with a clean, self-contained implementation of FIPS 180-4. OpenSSL stays in the build for TLS only.

**Rationale:**

| Goal | Why |
|---|---|
| Small | `sha256.h` + `sha256.c`: ~130 lines, zero dependencies. No OpenSSL EVP overhead. |
| Auditable | SHA-256 is a NIST-published standard (FIPS 180-4). Every line can be checked against the spec. No trust required beyond the published algorithm. |
| Portable | Pure C99. Works on Raspberry Pi, macOS, Linux without build system changes. |
| No deprecated API | Removes all `SHA256_Init/Update/Final` calls from the build. |
| Native-first | Aligns with ADR-004: minimise unmanaged external dependencies. |

**Alternative considered: Monocypher (Blake2b)**

Monocypher is a 2-file, public-domain, formally audited library (used in WireGuard) that provides Blake2b for hashing. It is faster than SHA-256 on the same hardware. It was not chosen here because:
- Blake2b changes the hash format (not SHA-256 compatible)
- SHA-256 is already established in the codebase and test suite
- The self-contained implementation is equally small and auditable

Monocypher remains a valid future choice if the hash format is renegotiated.

### sha256.h / sha256.c Design

```c
void sha256_init  (sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const void *data, size_t len);
void sha256_final (sha256_ctx *ctx, uint8_t digest[SHA256_DIGEST_LEN]);
void sha256_digest(const void *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]);
```

- `sha256_ctx` holds: `state[8]`, `bit_count` (64-bit), `buf[64]`, `buflen`
- `sha256_final` **wipes** the context after producing the digest (no state leakage)
- Padding follows FIPS 180-4 § 5.1.1 exactly; handles both 1-block and 2-block padding paths
- `sha256_digest` is the one-shot helper for the common case

### Fixes Applied to crypto.c

1. **Hash length**: `to_hex(digest, 32, (char *)block->hash)` writes all 64 hex chars + null. `HASH_SIZE = 65` is now fully utilised.
2. **`consensus` in hash**: Added `sha256_update(&ctx, &block->consensus, sizeof(block->consensus))`.
3. **`compute_merkle_root`**: Feeds `sender + recipient + amount` for each transaction using incremental `sha256_update`. No intermediate buffer — avoids the previous overflow risk.
4. **NULL check**: Moved before `log_info` in `hash()`.
5. **`sign()`/`verify()` stubs**: Implemented as `return EXIT_FAILURE` with clear ADR-003 reference.

### For Signing (Future Work)

`sign()` and `verify()` remain stubs. They will be wired to **liboqs Dilithium** as planned in ADR-003. The function signatures are already in `crypto.h` and compatible with a Dilithium integration.

### Test Coverage (`tests/test_crypto.c`) — 20 tests across 5 groups

| Group | Tests |
|---|---|
| `sha256/nist` | FIPS 180-4 known answer: SHA-256("") |
| `sha256/vs_openssl` | Cross-validation vs OpenSSL reference: 1 byte, 5 bytes, 55, 56, 64, 200, 256 bytes |
| `sha256/incremental` | Byte-by-byte update matches one-shot; deterministic; different inputs differ; output length |
| `block_hash` | Full 64-char output (regression for truncation bug); nonce change changes hash; consensus change changes hash; NULL block |
| `merkle` | Empty transactions → "0"; NULL args safe; transactions produce full 64-char root; different amounts produce different roots |

The `vs_openssl` group uses OpenSSL as a reference oracle (suppressing deprecation warnings with pragma guards). The 55-byte and 56-byte tests specifically exercise the two padding paths in `sha256_final`.

---

## ADR-009: Transaction Module — Dilithium-Correct Sizes, Integer Amounts, Replay Protection

**Date:** 2026-03
**Status:** Adopted
**Files:** `inc/transaction.h`, `src/transaction.c`, `src/chain.c`, `src/crypto.c`, `src/main.c`, `tests/test_crypto.c`

### Context — Issues Found in Original transaction.h / transaction.c

A review of the transaction module found the following bugs and deficiencies:

| Issue | Severity | Detail |
|---|---|---|
| `MAX_SIGNATURE_LENGTH 512` too small | Critical | Dilithium-3 signatures are 3293 bytes. Storing one would silently overflow the field. |
| `MAX_PUBLIC_KEY_LENGTH 1024` too small | Critical | Dilithium-3 public keys are 1952 bytes. Same overflow risk. |
| `double amount` for financial values | High | Floating-point arithmetic is non-deterministic across platforms. Two nodes could hash the same transaction to different values. |
| No replay protection | High | A signed transaction could be rebroadcast indefinitely — there was no per-sender sequence number. |
| `sign_transaction` returned `void` | High | OQS failures were silently swallowed. Callers had no way to detect a signing failure. |
| `verify_transaction` return inverted | High | Returned `1` on success and `0` on failure — opposite of `EXIT_SUCCESS`/`EXIT_FAILURE` convention used everywhere else. |
| VLA for a compile-time-constant size | Medium | `uint8_t message[sizeof(sender)+...]` produced a VLA despite all operands being constants. Illegal in C99 `_Static_assert` context and non-portable. |
| `printf` instead of `log_error` | Low | Error messages bypassed the logging framework and always went to stdout. |
| No compile-time size guard | Low | Mismatches between `MAX_SIGNATURE_LENGTH` and the actual OQS constant would only be caught at runtime (memory corruption). |

### Decision — Updated Constants and Type

```c
#define MAX_PUBLIC_KEY_LENGTH  1952          /* Dilithium-3 public key  */
#define MAX_SIGNATURE_LENGTH   3293          /* Dilithium-3 signature   */
#define MICRO_PER_TOKEN        1000000ULL    /* 1 token = 1,000,000 µ   */
#define TX_MESSAGE_LEN  (MAX_PUBLIC_KEY_LENGTH * 2 + sizeof(uint64_t) * 2)
```

`amount` is stored as a `uint64_t` in **micro-units** (one millionth of a token). This is the same convention used by Bitcoin (satoshis) and the Lightning Network. It makes all hashing and comparison operations on amounts bitwise-identical across platforms.

A `uint64_t nonce` field is added for **replay protection**. The nonce is a per-sender sequence number that must increase monotonically. A transaction with a reused nonce is invalid. The nonce is included in the signed message and in the Merkle root so it cannot be stripped by an intermediary.

### Decision — Updated Transaction Struct

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

### Decision — sign_transaction Returns int

`sign_transaction` now returns `int` (`EXIT_SUCCESS` / `EXIT_FAILURE`). This aligns with every other function in the codebase that can fail. Callers can propagate or log the error.

`verify_transaction` return convention is corrected: `EXIT_SUCCESS` on valid signature, `EXIT_FAILURE` otherwise.

### Decision — build_message Helper

A static `build_message()` helper constructs the canonical message for sign/verify:

```
sender[MAX_PUBLIC_KEY_LENGTH] | recipient[MAX_PUBLIC_KEY_LENGTH] | amount(8B) | nonce(8B)
```

Using **fixed-width fields** (not `strlen`) guarantees that the message length is constant regardless of label content. Both `sign_transaction` and `verify_transaction` call `build_message` — they always hash the same bytes for the same struct state.

### Decision — _Static_assert Guard

```c
#if defined(OQS_SIG_dilithium_3_length_signature)
_Static_assert(MAX_SIGNATURE_LENGTH >= OQS_SIG_dilithium_3_length_signature,
               "MAX_SIGNATURE_LENGTH too small for Dilithium-3");
#endif
```

This catches a size mismatch at compile time rather than at runtime (memory corruption). The guard is conditional so the file compiles when `liboqs` is absent.

### Decision — Nonce in Merkle Root

`compute_merkle_root` (in `crypto.c`) feeds `tx->nonce` into the SHA-256 hash for each transaction. Without this, a valid transaction could have its nonce stripped and remain undetected.

### On-Disk Format Break

These changes increase `sizeof(Transaction)` and therefore `sizeof(Block)`. Any `.chain/` directory created before this change holds binary files in the old format and is incompatible. Existing `.chain/` directories must be deleted and re-initialised.

This is an intentional, one-time break. The Makefile requires `make clean` before `make test` any time a header that affects `sizeof(Block)` changes, because the Makefile does not yet generate automatic header dependency rules (there is no `-MMD -MP` flag). Tracking this as a known limitation: any change to `transaction.h` or `block.h` requires `make clean` to avoid stale objects.

### Rationale

| Decision | Reason |
|---|---|
| `uint64_t amount` (micro-units) | Platform-independent bitwise representation; standard convention (Bitcoin, Lightning) |
| `uint64_t nonce` | Necessary for replay protection before Dilithium signing is wired in |
| Fixed-width `build_message` | Sign and verify always agree on the message bytes regardless of field content |
| `_Static_assert` | Catches size regressions at compile time, not at runtime |
| `int` return from `sign_transaction` | Consistent with every other fallible function in the codebase |
| `log_error` instead of `printf` | All error output should go through the logging framework for stream control |

### Files Changed

| File | Change |
|---|---|
| `inc/transaction.h` | New constants, `uint64_t amount/nonce`, `TX_MESSAGE_LEN`, `int sign_transaction` |
| `src/transaction.c` | `build_message()`, fixed-size buffer, corrected returns, `log_error`, `_Static_assert` |
| `src/chain.c` | `amount == 0` guard (was `<= 0.0` on a double) |
| `src/crypto.c` | Added `sha256_update(&ctx, &tx->nonce, sizeof(tx->nonce))` in `compute_merkle_root` |
| `src/main.c` | `atof` → `uint64_t` micro-unit conversion; STAGED format uses integer amounts; display with `MICRO_PER_TOKEN` divisor |
| `tests/test_crypto.c` | Test amounts converted to ULL micro-units (e.g. `42.0` → `42000000ULL`) |

---
