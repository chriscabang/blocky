# QuteChain — Architecture & Design Notes

This document captures key architectural decisions and design discussions for the QuteChain (`blocky`) project. Update this file whenever a concept, decision, or trade-off is discussed that affects the overall design.

---

## Design Philosophy

QuteChain is written in C and deliberately applies **SOLID principles** and **clean code** practices throughout. These are not aspirational — they are enforced at review time. The sections below translate each principle into concrete C conventions used in this codebase.

### SOLID in C

#### S — Single Responsibility
Each `.c`/`.h` pair owns exactly one concern. A module that does two things should be two modules.

| Module | Sole responsibility |
|---|---|
| `sha256.c` | FIPS 180-4 SHA-256 computation — nothing else |
| `storage.c` | On-disk object store and ref management |
| `network.c` | TLS connection lifecycle and block serialization |
| `pow.c` | Proof-of-work mining and validation |
| `transaction.c` | Transaction signing and verification |
| `chain.c` | In-memory chain state and pool allocator |

`main.c` is the only module permitted to coordinate across modules. All others are strictly single-purpose.

#### O — Open/Closed
Public headers define stable interfaces. Callers depend on the header, never on internal struct layout or file-level statics. New behavior is added by extension, not by modifying existing interfaces.

- Adding a new consensus algorithm means adding a new module and a new `consensus` enum value — not modifying `block.h` or `chain.c`.
- `net_context_server` / `net_context_client` accept a `NetConfig` struct; adding a new TLS option is a new field in `NetConfig`, not a new function signature.

#### L — Liskov Substitution
Any function accepting `const Block *` or `const Chain *` must work correctly for any valid instance of that type, regardless of how it was constructed. There are no hidden preconditions beyond what the header documents.

- `block_verify_hash(block)` is meaningful for any non-NULL Block, whether loaded from disk or freshly created.
- `chain_validate(chain, block)` treats every Block identically — genesis blocks are distinguished only by their `previous_hash` value, not by type.

#### I — Interface Segregation
Headers expose only what callers need. Internal helpers are `static` in the `.c` file and invisible to the rest of the system.

- `NetContext` is opaque — callers hold a pointer and call functions; no field access.
- `storage.h` exposes five functions; the file path layout (`.chain/blocks/…`) is entirely internal.
- `sha256.h` exposes `sha256_ctx`, `sha256_init`, `sha256_update`, `sha256_final` — and nothing else.

#### D — Dependency Inversion
High-level modules depend on abstractions (headers), not on low-level implementation details.

- `chain.c` calls `storage_insert` / `storage_checkout` — it has no knowledge of file paths or `fwrite`.
- `pow.c` calls `block_compute_hash` — it has no knowledge of SHA-256 internals.
- `network.c` calls `log_info` / `log_error` — it has no knowledge of how log output is routed.

---

### Clean Code Conventions

**Naming — snake_case everywhere.**
All identifiers (functions, variables, struct fields, macros where readable), source filenames, and binary names follow snake_case. Compound words are always separated: `start_chain`, `mine_block`, `propose_block`, `key_gen`. This mirrors the C standard library and eliminates ambiguity between `startchain`, `StartChain`, and `start_chain`.

**No `exit()` in library code.**
Only `main.c` and utility `main()` functions may terminate the process. Every library function returns an error code (`NULL`, `-1`, `EXIT_FAILURE`) and lets the caller decide. This makes all modules safe to link into test harnesses and future daemons.

**No global mutable state.**
Each `NetContext`, `Chain`, and `OQS_SIG` instance is heap-allocated, caller-owned, and fully independent. There are no module-level globals. Concurrent future use (multiple chains, multiple TLS contexts) requires no locking changes to existing code.

**Const-correctness.**
Functions that do not modify their inputs declare them `const`. This is enforced: `block_verify_hash(const Block *)`, `chain_validate(const Chain *, const Block *)`, `net_serialize_block(const Block *, …)`. Violations are treated as bugs.

**One function, one job.**
Functions are short and named after what they do, not how they do it. `apply_common_security` in `network.c` encapsulates the TLS 1.3 + PQC group setup so that both server and client paths share identical policy without duplication.

**Errors are values, not exceptions.**
Return codes are checked at every call site. The pattern is: acquire resources, check each step, clean up on any failure path. `goto done` with a single cleanup block is preferred over duplicated `free` / `SSL_free` chains.

**Private key material is zeroed before release.**
Any function that holds a private key or secret seed calls `OQS_MEM_cleanse` before `free`. This applies to utility binaries (`send_payment`, `key_gen`) and any future signing code.

**Unit test coverage is mandatory.**
Every public `.h`/`.c` module has a corresponding `tests/test_<module>.c` using CMocka. A module is not considered complete until `make test` passes with that suite included. Coverage is tracked via `make check`.

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

## ADR-010: Consensus Module Redesign — Dispatch Table, PoW Enforcement, PoS Stubs

**Date:** 2026-03
**Status:** Adopted
**Files:** `inc/consensus.h`, `src/consensus.c`, `src/chain.c`, `src/main.c`, `tests/test_chain.c`, `tests/test_consensus.c`

### Context — Issues Found in Original consensus.h / consensus.c

A review of the consensus module found the following bugs and architectural deficiencies:

| Issue | Severity | Detail |
|---|---|---|
| `verify_consensus()` never called | Critical | No code anywhere included `consensus.h` or called `verify_consensus()`. Chain accepted any block regardless of PoW difficulty or PoS validity — consensus was completely bypassed. |
| Wrong dispatch logic | Critical | `block->consensus % SWITCH_INTERVAL` evaluated `0 % 10 = 0` (PoS) and `1 % 10 = 1` (PoW) — the opposite of the intended routing. |
| Always returns 0 (success) | Critical | `verify_consensus()` returned 0 unconditionally after printing a string. All validation was commented out. |
| `verify_signature()` declared but undefined | High | Would cause a linker error if called. Interface Segregation violated — an incomplete function was exported. |
| `SWITCH_INTERVAL` conflicts with ADR-006 | High | Automatic block-index-based consensus switching conflicts with `Block.consensus` being an explicit proposer-set field (ADR-006). The field stores the consensus type directly; SWITCH_INTERVAL is meaningless. |
| `puts()` instead of `log_error()` | Low | Bypassed the log framework; messages always went to stdout. |
| Unused types (`ProofOf`, `ConsensusType`) | Low | Never referenced outside `consensus.h`. Added noise. |

### Decision — Dispatch Table

Replace the conditional with a function-pointer array indexed by `block->consensus`:

```c
typedef int (*consensus_fn)(const Block *);

static const consensus_fn VERIFY[] = {
    [CONSENSUS_POW] = verify_pow_rules,
    [CONSENSUS_POS] = verify_pos_rules,
};
```

Adding a new consensus type requires only a new entry in the array and a new `verify_*_rules` function — no changes to `verify_consensus()` itself (Open/Closed Principle).

### Decision — Constants

```c
#define CONSENSUS_POW 0   /* Proof of Work  */
#define CONSENSUS_POS 1   /* Proof of Stake */
```

These replace `SWITCH_INTERVAL`, `ProofOf`, and `ConsensusType`, which are all removed. The constants are self-documenting and match the values stored in `Block.consensus` (from ADR-006).

### Decision — PoW Rules

`verify_pow_rules()` delegates entirely to `validate_block_pow(block, DIFFICULTY)`, which performs both the leading-zero difficulty check and `block_verify_hash()` internally. The DIFFICULTY constant (default 4) is defined in `pow.h`.

### Decision — PoS Rules (with Security Stubs)

`verify_pos_rules()` currently enforces:
1. **Hash integrity** — `block_verify_hash()` — required for all blocks.

Three security checks are required for production PoS but deferred to ADR-003 (validator key registry + VRF leader selection). Each is marked with a TODO:

```c
/* TODO (ADR-003): verify VRF proof — proposer must hold the slot token. */
/* TODO (ADR-003): verify Dilithium-3 block signature (verify_block_signature). */
/* TODO (ADR-002): verify proposer has sufficient registered stake. */
```

The stubs are present and accounted for; they are not silent omissions.

### Decision — `verify_block_signature` Stub

`verify_signature(const Block *)` (undeclared implementation) is replaced by `verify_block_signature(const Block *)`, which is implemented as a stub returning `EXIT_FAILURE`. This:
- Eliminates the undefined-symbol linker error.
- Prevents unsigned blocks from passing validation by accident.
- Documents the missing feature with an ADR-003 reference.

### Decision — Wire into `chain_validate`

```c
/* Before (manual check only): */
if (block->consensus != 0 && block->consensus != 1) {
    log_error("chain_validate: unknown consensus type ...");
    return EXIT_FAILURE;
}

/* After (full consensus enforcement): */
if (verify_consensus(block) != EXIT_SUCCESS) {
    log_error("chain_validate: consensus check failed ...");
    return EXIT_FAILURE;
}
```

`chain_validate()` now enforces PoW difficulty and PoS hash integrity for every block added to the chain. The consensus bypass is closed.

### Decision — `cmd_commit` Now Mines PoW Blocks

`cmd_commit` in `main.c` previously called `block_compute_hash(b)`, which produces a valid hash but with no leading zeros. After wiring `verify_consensus` into `chain_validate`, such a block would fail the PoW check when `chain_add()` was called.

Fix: set `b->consensus = CONSENSUS_POW` explicitly and replace `block_compute_hash` with `mine_block(b, DIFFICULTY)`:

```c
b->consensus = CONSENSUS_POW;
if (mine_block(b, DIFFICULTY) != EXIT_SUCCESS) {
    fprintf(stderr, "error: failed to mine block (nonce exhausted)\n");
    ...
}
```

This makes the CLI correct by construction: committed blocks are real PoW blocks.

### Decision — `test_chain.c` Uses `CONSENSUS_POS` in `make_next`

`make_next()` (the test helper that builds well-formed blocks for chain tests) now sets `b->consensus = CONSENSUS_POS`. PoS only requires hash integrity — no mining — so chain tests remain fast. Tests that specifically exercise PoW consensus live in `test_consensus.c`.

This separates concerns: `test_chain.c` tests chain mechanics (link rules, pool, storage); `test_consensus.c` tests consensus rules (difficulty, hash integrity, dispatch).

### Security Improvements

| Before | After |
|---|---|
| Consensus bypass: any block accepted | `verify_consensus()` enforced in `chain_validate()` for every `chain_add()` |
| PoW difficulty never checked at chain layer | PoW blocks must satisfy `DIFFICULTY` leading zeros before admission |
| `verify_signature()` undefined symbol | `verify_block_signature()` stub returns EXIT_FAILURE — unsigned blocks rejected |
| SWITCH_INTERVAL created implicit PoW↔PoS confusion | Explicit `block->consensus` field, validated against known types |
| Unknown consensus types silently accepted | Bounds check rejects unknown types (EXIT_FAILURE) |

### Pending Security Work (ADR-003)

1. **VRF leader election**: The slot token proof must be verified to prevent any validator from proposing PoS blocks out of turn.
2. **Dilithium-3 block signatures**: `verify_block_signature()` must be wired to the key registry once validator public keys are stored.
3. **Stake threshold**: A minimum stake must be required before a validator can propose — prevents Sybil attacks.
4. **Equivocation guard**: A validator should not be permitted to propose two different blocks for the same slot.

### Test Coverage (`tests/test_consensus.c`) — 8 tests across 2 groups

| Group | Tests |
|---|---|
| `consensus/verify_consensus` | NULL block, PoW mined block passes, PoW unmined block fails, PoW tampered nonce fails, PoS valid hash passes, PoS tampered hash fails, unknown type=2 fails |
| `consensus/verify_block_signature` | stub always returns EXIT_FAILURE |

### Files Changed

| File | Change |
|---|---|
| `inc/consensus.h` | Removed SWITCH_INTERVAL / ProofOf / ConsensusType; added CONSENSUS_POW / CONSENSUS_POS; replaced `verify_signature` with `verify_block_signature` |
| `src/consensus.c` | Full rewrite: dispatch table, verify_pow_rules, verify_pos_rules (with TODO stubs), verify_block_signature stub, log_error/log_warn throughout |
| `src/chain.c` | Added `#include "consensus.h"`; replaced manual consensus field check with `verify_consensus()` call |
| `src/main.c` | Added `#include "consensus.h"` and `pow.h`; `cmd_commit` now sets `CONSENSUS_POW` and calls `mine_block(b, DIFFICULTY)` |
| `tests/test_chain.c` | Added `#include "consensus.h"`; `make_next()` sets `b->consensus = CONSENSUS_POS` |
| `tests/test_consensus.c` | New: 8 tests for verify_consensus and verify_block_signature |

---

## ADR-011: Log Module Rewrite — Heisenbug-Safe, Level-Gated, Secure

**Date:** 2026-03
**Status:** Adopted
**Files:** `inc/log.h`, `src/log.c`, `tests/test_log.c`

### Context — Problems Found in Original log.h / log.c

A review of the logging module against the goals of zero latency impact and no heisenbugs found the following defects:

| Issue | Impact | Detail |
|---|---|---|
| `malloc` on every log call | Heisenbug risk, crash risk | Heap allocation changes memory layout across every log call, altering pointer values and padding. Any bug that only manifests when the heap is laid out a certain way will appear or disappear depending on log verbosity. A `malloc` failure also silently dropped the message with no NULL check. |
| Mutex held for entire I/O including `fflush` | Latency, thread scheduling change | All other threads were blocked while a single log call formatted text, wrote to disk, and flushed. `fflush` is a synchronous kernel call; holding the mutex during it serialises the mining loop. |
| `fflush` after every write | Latency | Forced a kernel write barrier after every INFO and DEBUG message. INFO is emitted frequently (every block hash, every storage insert); flushing every one adds measurable latency to the chain operations path. |
| `ENABLED` macro computed but never used | Correctness | The expression `(level <= LOG_LEVEL_INFO)` was evaluated and discarded. No runtime level filtering occurred. |
| `DEBUG` guard broken | Correctness | All four macros (`log_error`, `log_warn`, `log_info`, `log_debug`) always expanded to a `log_write()` call regardless of whether `-DDEBUG` was set. `log_debug` emitted output in release builds. |
| Source paths / function names in release | Security | Production logs must not expose internal code structure (file paths, function names, line numbers) to operators, log aggregators, or anyone with access to the log stream. |
| `(fmt, ...) + ##__VA_ARGS__` GNU extension | Portability | The `##__VA_ARGS__` token-pasting extension (removes preceding comma when variadic list is empty) triggered `-Wvariadic-macro-arguments-omitted` at every call site that passed only a format string. The pragma guards in `log.h` covered the definition but not the expansion sites. |

### Decision — Stack-Only Formatting, Minimal Critical Section

All formatting (timestamp, source location, user message, assembled line) happens on the calling thread's stack before the mutex is acquired. The mutex is held only for `fprintf` and the conditional `fflush`:

```
caller thread:
  ① level gate (one comparison, no lock, no allocation)
  ② format timestamp              — stack, no lock
  ③ format source location        — stack, no lock
  ④ vsnprintf user message        — stack, no lock
  ⑤ assemble full line            — stack, no lock
  ⑥ pthread_mutex_lock
  ⑦ fprintf(log_stream, ...)      — I/O, lock held
  ⑧ fflush (ERROR/WARN only)      — conditional, lock held
  ⑨ pthread_mutex_unlock
```

No `malloc` or `free` anywhere in the call path. All buffers are fixed-size stack variables. The heap layout is identical whether logging is active or suppressed — heisenbugs caused by log-dependent memory layout are eliminated by construction.

### Decision — Level Gate

The first operation in `log_write` is a single comparison against `log_level` with no lock:

```c
if (level > log_level) return;
```

Filtered messages cost one integer comparison on the calling thread and nothing else. No stack allocation, no mutex touch, no formatting work.

### Decision — fflush Policy

`fflush` is called only when `level <= LOG_LEVEL_WARN`:

- **ERROR, WARN** — flushed immediately. Critical failures and anomalies must reach the operator even if the process crashes shortly after.
- **INFO, DEBUG** — left in the OS write buffer. INFO is emitted on every block hash and storage insert; synchronous flushing after every INFO would serialise the mining loop and chain operations.

### Decision — Runtime Level Control

```c
void log_set_level(int level);
```

Sets the minimum severity threshold at runtime. **ERROR and WARN cannot be suppressed**: `log_set_level` clamps the minimum to `LOG_LEVEL_WARN`. Operators running high-throughput nodes can silence INFO to reduce I/O without losing consensus failure visibility.

| Constant | Value | Compiled | Suppressible |
|---|---|---|---|
| `LOG_LEVEL_ERROR` | 0 | Always | No (always visible) |
| `LOG_LEVEL_WARN` | 1 | Always | No (minimum clamp) |
| `LOG_LEVEL_INFO` | 2 | Always | Yes (via `log_set_level`) |
| `LOG_LEVEL_DEBUG` | 3 | Debug only | N/A — `((void)0)` in release |

### Decision — Compile-Time DEBUG Guard

In release builds, `log_debug(...)` expands to `((void)0)`. The compiler emits zero instructions: no stack frame, no register spill, no heap touch, no thread scheduling impact. This is the correct fix for debug-induced heisenbugs — the binary is identical to one that never had `log_debug` calls.

```c
#if defined(DEBUG)
#define log_debug(...) \
    log_write(LOG_LEVEL_DEBUG, "DEBUG", __FILE__, __func__, __LINE__, __VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif
```

### Decision — Release Builds Omit Source Location

In release builds, `__FILE__`, `__func__`, and `__LINE__` are replaced with `NULL, NULL, 0`:

```c
/* Release */
#define log_error(...) \
    log_write(LOG_LEVEL_ERROR, "ERROR", NULL, NULL, 0, __VA_ARGS__)
```

`log_write` checks `if (file && func)` before formatting the source location field. Release log lines contain only the timestamp, level, and message. Internal source paths and function names are never written to log streams that may be forwarded to external systems.

### Decision — `(...)` / `__VA_ARGS__` Pattern

All log macros use `(...)` instead of `(fmt, ...)` and `__VA_ARGS__` instead of `##__VA_ARGS__`. With `(...)`, the format string itself is the first element of the variadic pack — `__VA_ARGS__` is never empty. This eliminates the GNU extension entirely, removing the pragma guards from `log.h` and the `-Wvariadic-macro-arguments-omitted` warning from all call sites.

### Decision — Remote Monitoring via Named Pipe

`log_set_stream(FILE *stream)` accepts any `FILE*`, including one opened on a FIFO:

```c
log_set_stream(fopen("/tmp/blocky.log", "w"));
// shell: cat /tmp/blocky.log | ssh user@monitor ...
```

A dedicated **TCP/UDP logging port is not recommended**. It exposes real-time mining state (nonce trajectory, block solve timing, hash rate) to unauthenticated network observers, enabling timing attacks and selfish-mining intelligence gathering. Named pipe or log file forwarded over an authenticated channel (SSH, TLS) is the correct approach.

### Security Policy

`NEVER` pass private keys, VRF secrets, seed material, Dilithium secret keys, or session tokens to any log macro. Log output may be written to shared storage or forwarded to external systems — treat all log content as public. Source paths and function names are omitted in release builds to avoid leaking internal code structure.

### Test Coverage (`tests/test_log.c`) — 8 tests across 3 groups

| Group | Test | Verifies |
|---|---|---|
| `log/stream` | `test_log_to_terminal` | `log_set_stream(stdout)` routes output; "INFO" appears in pipe-captured output |
| `log/stream` | `test_log_to_file` | `log_set_stream(file)` writes "INFO" and the message text |
| `log/level` | `test_info_suppressed_below_threshold` | INFO produces zero bytes when threshold is WARN |
| `log/level` | `test_error_never_suppressed` | ERROR appears even when threshold is WARN |
| `log/level` | `test_warn_visible_at_warn_threshold` | WARN appears when threshold is WARN |
| `log/level` | `test_level_clamped_at_warn_minimum` | `log_set_level(ERROR)` is clamped to WARN; WARN still appears |
| `log/format` | `test_debug_build_includes_source_location` | Debug build includes filename and function name in output |
| `log/format` | `test_timestamp_present` | Timestamp prefix `[20...` appears on every log line |

### Files Changed

| File | Change |
|---|---|
| `inc/log.h` | Full rewrite: level constants, `log_set_level`, fixed DEBUG guard, `(...)/__VA_ARGS__` pattern, security documentation |
| `src/log.c` | Full rewrite: stack-only buffers, level gate, format outside mutex, fflush on ERROR/WARN only, `log_set_stream` mutex-protected |
| `tests/test_log.c` | Updated: setup/teardown per test, 4 new level tests, 2 format tests |

---

## ADR-012: Integration Tests — Payment and Miner Use Cases

**Date:** 2026-03
**Status:** Adopted
**Files:** `tests/test_integration_payment.c`, `tests/test_integration_miner.c`

### Context

Unit tests cover individual modules in isolation (`test_block.c`, `test_chain.c`, etc.). Integration tests are needed to verify that the modules compose correctly under realistic end-to-end scenarios matching real user workflows. Two primary use cases were identified:

1. **Payment user** — sends tokens from one address to another, commits to the chain, and can verify the on-chain record.
2. **Miner** — mines blocks with PoW, grows the chain, and the chain correctly rejects invalid blocks.

### Decision — Test Against the Library API Directly

Integration tests call the C library API (`chain.h`, `block.h`, `storage.h`, `pow.h`, `consensus.h`) directly, not through the CLI binary. This:
- Exercises the same code path as `cmd_send` + `cmd_commit` without depending on CLI parsing.
- Makes failures deterministic — no subprocess, no shell, no environment dependency.
- Runs at the same speed as unit tests.

### Decision — Mine at `DIFFICULTY=4` in Integration Tests

Each test that produces a valid PoW block calls `mine_block(b, DIFFICULTY)` (DIFFICULTY=4, defined in `pow.h`). At 4 leading hex zeros, average hash count is ~65,000 per block. Three consecutive mines complete in well under one second on any modern CPU.

### Decision — Tamper Detection Scope

`block_verify_hash()` (called by `verify_consensus` for all block types) recomputes the hash over the block **header fields**: `index`, `timestamp`, `previous_hash`, `merkle_root`, `nonce`, `consensus`. It does **not** re-verify raw transaction bytes against the Merkle root.

Consequences:
- Tampering any **header field** (including `merkle_root`) after mining is detected.
- Tampering a **transaction field** (e.g., `amount`) after computing the Merkle root but before `chain_add` is also detected, because `compute_merkle_root` hashes all transaction fields into `merkle_root`, and the stored hash covers `merkle_root`.
- Tampering a transaction field **after** `chain_add` (i.e., in the stored copy) is not caught by `block_verify_hash` — it would require re-running `compute_merkle_root` and comparing. This is a known limitation; full Merkle re-verification is future work.

The integration test `test_tampered_merkle_root_rejected` validates tamper detection by flipping a byte in `b->merkle_root` before `chain_add`, which correctly triggers rejection.

### Test Coverage — Payment (`tests/test_integration_payment.c`) — 6 tests across 4 groups

| Group | Test | Scenario |
|---|---|---|
| `payment/send` | `test_alice_sends_50_to_bob` | Full PoW payment flow; stored block has correct sender, recipient, amount |
| `payment/send` | `test_chain_tip_advances_after_payment` | In-memory and on-disk HEAD both advance to the payment block |
| `payment/batch` | `test_three_payments_in_one_block` | Three transactions in one block; all retrieved in order |
| `payment/multi_block` | `test_two_payment_blocks_in_sequence` | Block 2 correctly links to Block 1; both independently readable |
| `payment/invalid` | `test_zero_amount_transfer_rejected` | `amount=0` fails `chain_validate` before consensus check |
| `payment/invalid` | `test_tampered_merkle_root_rejected` | Flipping `merkle_root[0]` after mining causes `chain_add` to return `EXIT_FAILURE` |

### Test Coverage — Miner (`tests/test_integration_miner.c`) — 9 tests across 3 groups

| Group | Test | Scenario |
|---|---|---|
| `miner/proof_of_work` | `test_mine_block_returns_success` | `mine_block` returns `EXIT_SUCCESS`; hash has DIFFICULTY leading `'0'` chars; full 64-char hex string |
| `miner/proof_of_work` | `test_mined_block_passes_pow_validation` | `validate_block_pow(b, DIFFICULTY)` returns `EXIT_SUCCESS` |
| `miner/proof_of_work` | `test_mined_block_accepted_by_chain` | `chain_add` advances in-memory tip and on-disk HEAD |
| `miner/proof_of_work` | `test_miner_includes_transactions` | Block with one transaction accepted; transaction retrievable from storage |
| `miner/chain_growth` | `test_mine_three_consecutive_blocks` | Chain tip at index 3; all three blocks independently readable |
| `miner/chain_growth` | `test_chain_link_integrity` | `b1.previous_hash == genesis.hash`; `b2.previous_hash == b1.hash` |
| `miner/security` | `test_unmined_pow_block_rejected` | `block_compute_hash` (no mining) produces no leading zeros; rejected by `chain_add` |
| `miner/security` | `test_tampered_hash_after_mining_rejected` | Flipping `hash[8]` after mining triggers `block_verify_hash` mismatch |
| `miner/security` | `test_wrong_previous_hash_rejected` | Mined block pointing to wrong previous hash fails the link check in `chain_validate` |

### Files Added

| File | Content |
|---|---|
| `tests/test_integration_payment.c` | 6 tests: payment send, batch, multi-block, invalid |
| `tests/test_integration_miner.c` | 9 tests: PoW correctness, chain growth, security rejection |

---

## ADR-013: Build System — Aggregated Test Summary and Coverage Target

**Date:** 2026-03
**Status:** Adopted
**Files:** `Makefile`

### Context

`make test` ran every test binary and reported only a per-suite pass/fail exit code. There was no aggregated count of individual tests passed or failed across all suites, making it hard to gauge overall test health at a glance. There was also no coverage instrumentation in the build system.

### Decision — Aggregated Summary in `make test`

After all test binaries have run, `make test` prints a table sourced from cmocka's group-summary lines (`[  PASSED  ] N test(s).` and `[  FAILED  ] N test(s).`):

```
  Suite                                    Passed  Failed   Total
  ──────────────────────────────────────────────────────────────
  test_block                                   12       0      12
  test_chain                                   15       0      15
  test_consensus                                8       0       8
  test_crypto                                  20       0      20
  test_integration_miner                        9       0       9
  test_integration_payment                      6       0       6
  test_log                                      8       0       8
  test_main                                    24       0      24
  test_pos                                     12       0      12
  test_pow                                     13       0      13
  test_storage                                 26       0      26
  test_transaction                             15       0      15
  ──────────────────────────────────────────────────────────────
  TOTAL                                       168       0     168

  All 168 tests passed.
```

Key behaviours preserved from the original:
- Every suite runs even if earlier ones fail (failures accumulate).
- Non-zero exit from any suite still causes `make test` to return non-zero.
- Output from each suite is shown before the summary (suite output feeds forward as it completes, not held until the end).

### Decision — Separate `make coverage` Target

A new `coverage` target builds all source and test files with `--coverage` (gcov-compatible profiling) into a separate `build/cov/` tree. This keeps the normal debug build unaffected — `build/debug/` objects are never instrumented.

Build layout:

```
build/cov/
  src/          ← instrumented source objects (.o + .gcno)
  tests/        ← instrumented test objects (.o + .gcno)
  test_*        ← instrumented test binaries
  coverage.info ← lcov capture (if lcov installed)
  html/         ← genhtml output (if lcov installed)
```

After running all test binaries, the coverage report is produced:

- **With `lcov` installed** (`brew install lcov`): runs `lcov --capture`, strips system headers and test files, prints `lcov --summary`, and generates an HTML report at `build/cov/html/index.html`.
- **Without `lcov`**: runs `gcov -o build/cov/src/ src/*.c` and prints per-file line coverage from the gcov output. Prompts the user to install lcov for a full report.

### Coverage Baseline (2026-03)

Coverage measured against all 168 tests:

| File | Line Coverage |
|---|---|
| `src/block.c` | 100% of 26 lines |
| `src/consensus.c` | 100% of 25 lines |
| `src/sha256.c` | 100% of 68 lines |
| `src/pow.c` | 96.5% of 57 lines |
| `src/log.c` | 97.1% of 34 lines |
| `src/pos.c` | 91.9% of 37 lines |
| `src/storage.c` | 82.2% of 197 lines |
| `src/crypto.c` | 80.0% of 55 lines |
| `src/transaction.c` | 78.8% of 33 lines |
| `src/chain.c` | 60.9% of 138 lines |
| `src/network.c` | 0% of 58 lines |

Notable gaps:
- **`chain.c` (60.9%)** — `chain_propose()` stub, GHOST fork-choice paths, and some error branches in `chain_validate` are not yet exercised.
- **`network.c` (0%)** — Network module has no tests. The module is a stub pending P2P implementation (ADR-001).

### Files Changed

| File | Change |
|---|---|
| `Makefile` | Added `COV_*` variables and build rules for `build/cov/`; rewrote `test` target shell to capture output and print summary; added `coverage` phony target |

---

---

## ADR-014: Network Module — TLS Security, Clean Architecture, SOLID Rewrite

**Date:** 2026-03
**Status:** Adopted

### Context

The original `network.c` was a prototype with significant security gaps and architectural violations that made it unsuitable for production use. A full review against SOLID principles, clean code methodology, and security-first requirements was conducted before any further network work.

### Problems Found

**Security (blockers):**

| # | Issue |
|---|-------|
| S1 | No server certificate or private key was loaded — TLS handshake always failed |
| S2 | No peer certificate verification (`SSL_VERIFY_NONE` default) — MITM trivially possible |
| S3 | No minimum TLS version enforced — downgrade attacks possible |
| S4 | 256-byte receive buffer — Dilithium-3 signature alone is 3 293 bytes; real blocks silently truncated |
| S5 | `SSL_read()` return value ignored — failed reads treated as empty input |
| S6 | No input validation on received bytes before acting on them |
| S7 | `SSL_shutdown()` called once only — TLS half-close; peer left in undefined state |
| S8 | OpenSSL 1.x deprecated init API (`SSL_load_error_strings`, `OpenSSL_add_ssl_algorithms`) |

**Architecture / SOLID:**

- Global `SSL_CTX *ctx` (SRP + OCP violation); shadowed by a local variable in the same TU.
- `init_tls()` created the global but `start_network_server()` created its own — dead function.
- `start_network_server()` mixed socket setup, TLS setup, accept loop, and I/O in one function.
- `create_client_context()` implemented but not exported in the header — dead private function.
- Six `exit()` calls in a library module — callers lost all error handling control.
- Hard-coded `SERVER_IP` and `SERVER_PORT` `#define`s — OCP violation.
- `serialize_block` and `broadcast_block` commented out — module was disconnected from the chain.
- All output via `printf` / `fprintf(stderr)` — bypassed `log_*` infrastructure.

### Decision

Complete rewrite of `network.h` / `network.c` with the following design:

**No global state.** An opaque `NetContext` struct owns the `SSL_CTX` and configuration. Each context is fully independent. Callers create, use, and free their own context.

**Configuration-driven.** A `NetConfig` struct is the single point of configuration for cert paths, CA bundle, bind address, port, and PQC group. No compile-time constants for runtime values.

**Security-first contract:**
- TLS 1.3 minimum enforced on every context (client and server).
- PQC hybrid group (`p256_kyber768`) is set when `cfg->pqc_group` is non-NULL. NULL skips PQC enforcement (test environments without OQS provider).
- Client contexts always enable `SSL_VERIFY_PEER`.
- Server contexts optionally enable mutual TLS via `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT` when `ca_file` is supplied.
- Bidirectional `tls_shutdown()` on every connection close.
- `SSL_read()` return value checked; failed reads logged and discarded.
- Receive buffer `NET_BLOCK_BUF_SIZE = 4096` — sufficient for block header broadcast (see Serialization).

**No `exit()` calls.** All functions return error codes. Callers decide recovery.

**Block serialization policy.** Only block header fields (index, timestamp, hashes, nonce, consensus, tx_count) are broadcast. Full transaction bodies (including Dilithium-3 signatures, up to ~145 KB per block) are fetched on demand. This keeps broadcast payloads small and avoids transmitting unvalidated key material over the wire.

**Single-threaded server.** `net_server_run()` handles one connection at a time. This is a deliberate trade-off appropriate for Raspberry Pi / low-concurrency nodes. Threading can be added later without changing the API.

**`log_*` throughout.** All output uses the project-standard `log_info` / `log_warn` / `log_error` macros.

### API

```c
NetContext *net_context_server(const NetConfig *cfg);
NetContext *net_context_client(const NetConfig *cfg);
void        net_context_free(NetContext *ctx);           /* NULL-safe */

int net_serialize_block(const Block *block, char *buf, size_t bufsz);
int net_broadcast_block(NetContext *ctx, const Block *block,
                        const char *peer_addr, uint16_t peer_port);
int net_server_run(NetContext *ctx);
```

### PQC Group Note

`p256_kyber768` is a hybrid classical P-256 + Kyber-768 group. If classical ECC is broken by a quantum adversary, Kyber-768 still provides post-quantum security. If Kyber is broken, P-256 still provides classical security. This defence-in-depth is the standard transition approach endorsed by NIST.

The OQS OpenSSL provider must be loaded at runtime for PQC groups to be available. Tests use `pqc_group = NULL` to avoid a hard dependency on the provider in CI environments.

### Pending

- `net_server_run()` currently logs received bytes; it needs to call a block deserializer and `chain_add()` once that interface is defined.
- Multi-peer broadcasting (fan-out to all known peers) belongs in `chain_propose()` using `net_broadcast_block()` per peer.

### Files Changed

| File | Change |
|---|---|
| `inc/network.h` | Full rewrite — `NetConfig`, opaque `NetContext`, secure API |
| `src/network.c` | Full rewrite — no global state, TLS 1.3 min, `SSL_VERIFY_PEER`, bidirectional shutdown, `log_*` |
| `tests/test_network.c` | New — 20 unit tests across 4 groups |

---

## ADR-015: Dead Code Removal — iterator.h and common.h Cleanup

**Date:** 2026-03
**Status:** Adopted

### Context

Two header files were present in `inc/` that were either completely unused or contained broken macro references.

### Problems Found

**`inc/iterator.h`:**
- Declared `iterate()` and `next_block()` but had no corresponding `iterator.c`.
- Was not `#include`d anywhere in the codebase.
- `Iterator.next` field duplicated `Block.next` from `block.h` without adding value.
- Pure dead code — no implementation, no consumers.

**`inc/common.h`:**
- `CHECKNULL(x)` and `CHECKZERO(x)` only called `Warn(...)` but did not call `FAIL` — they were silent no-ops that gave a false sense of guard coverage.
- `Info` and `Warn` were undefined identifiers — the file referenced macros that did not exist anywhere in the codebase (likely intended aliases for `log_info` / `log_warn` that were never defined). The file would fail to compile if included.
- Was not `#include`d anywhere in the codebase.

### Decision

**`inc/iterator.h`:** Deleted. Block traversal is done via `Block.next` directly. If a proper iterator abstraction is needed in the future, it should be implemented as `iterator.c` + `iterator.h` together, with tests.

**`inc/common.h`:** Initially rewritten (fixed `Info`/`Warn` refs, corrected `CHECKNULL`/`CHECKZERO` to call `FAIL`), then **deleted** — see follow-up analysis below.

### Follow-up: common.h Deleted

After the rewrite, a full applicability audit across all source files showed that the `START`/`FAIL`/`RETURN` macros cannot be safely applied to any existing function because:

1. **Resource cleanup** — `storage.c`, `chain.c`, `transaction.c`, and `main.c` all acquire heap, file, or OQS objects mid-function. `FAIL` breaks out immediately, skipping cleanup and causing leaks.
2. **Loops with internal checks** — `chain_validate`, `cmd_log`, `storage_scan` have failure checks inside `for`/`while` loops; `FAIL` would break the loop, not the `START` scope.
3. **Return type mismatches** — several functions return `Block*`, `void`, or custom exit codes (`0`/`1`/`2`); `RETURN` (which returns `int STATUS`) is wrong for all of them.
4. **Log noise** — `START` emits `log_info("Start >")` on every call, making it unsuitable for library functions called frequently (e.g. `block_verify_hash`, `storage_read`).
5. **Zero consumers** — `common.h` was never `#include`d anywhere.

Conclusion: the macros have a contract too narrow for the existing codebase and add file footprint with no benefit. Deleted entirely.

### Files Changed

| File | Change |
|---|---|
| `inc/iterator.h` | Deleted — no implementation, no consumers |
| `inc/common.h` | Deleted — zero consumers; macro contract incompatible with existing code |

