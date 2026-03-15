# zuno — Technical Design Reference

This document describes the technical implementation of each module, covering
key design decisions, data structures, APIs, and implementation constraints.
For the architectural overview and philosophy, see `docs/ARCHITECTURE.md`. For full
decision rationale and change history, see `docs/NOTES.md`.

---

## Block (`block.c`)

The `Block` struct is the fundamental unit of the chain. It contains header
fields, transaction data, and PoS-specific fields.

**Key fields:**

| Field | Type | Description |
|---|---|---|
| `index` | `uint32_t` | Block height (genesis = 0) |
| `timestamp` | `uint64_t` | Unix timestamp at creation |
| `previous_hash` | `char[65]` | SHA-256 hex of parent block |
| `merkle_root` | `uint8_t[32]` | SHA-256 over all transaction fields |
| `nonce` | `uint64_t` | PoW nonce (incremented by miner) |
| `consensus` | `uint8_t` | `CONSENSUS_POW=0` or `CONSENSUS_POS=1` |
| `hash` | `char[65]` | SHA-256 hex of all header fields |
| `transactions` | `Transaction[MAX_TRANSACTIONS]` | Embedded transaction array |
| `proposer_id` | `char[65]` | PoS proposer identity |
| `vrf_proof` | `VRFProof` | VRF election proof for PoS blocks |
| `proposer_sig` | `uint8_t[MAX_SIGNATURE_LENGTH]` | Dilithium-3 block signature |

**`block_compute_hash()`** feeds `index`, `timestamp`, `previous_hash`,
`merkle_root`, `nonce`, and `consensus` into SHA-256 via the self-contained
`sha256.c` implementation. Including `consensus` in the hash means PoW and PoS
blocks with identical content produce different hashes.

**`block_verify_hash()`** recomputes the hash and compares it against the
stored value. This is the primary tamper-detection mechanism at the header
level.

**`block_sign()` / `block_verify_sig()`** produce and verify a Dilithium-3
signature over the block hash, used for PoS block endorsement.

**`Block.next`** is a runtime-only linked list pointer. It must never reach
disk. `storage_insert()` writes a copy with `next = NULL`. `storage_read()`
sets `next = NULL` after `fread()`.

---

## Chain (`chain.c`)

The chain module manages in-memory chain state using a fixed-size pool
allocator — no per-block `malloc` after initialization.

**Pool design:**

```c
#define CHAIN_POOL_SIZE 64

typedef struct {
    Block   *head;
    Block    pool[CHAIN_POOL_SIZE];
    uint8_t  pool_used[CHAIN_POOL_SIZE];
} Chain;
```

At steady state, one slot is occupied: the current chain tip. When
`chain_add()` installs a new block, the old head slot is freed immediately
since it is already persisted to disk.

**`chain_load()`** reads the current HEAD from storage, loads the tip block
into a pool slot, and calls `chain_fork_choice()` to ensure the node boots on
the canonical branch. Creates a genesis block if the chain is empty.

**`chain_validate()`** checks in order:
1. `previous_hash` matches `chain->head->hash`
2. `block_verify_hash()` passes
3. `transaction.amount > 0` for all transactions
4. `verify_consensus()` passes (PoW difficulty or PoS rules)

**`chain_fork_choice()`** implements GHOST: enumerates all blocks in
`.chain/blocks/`, builds a lightweight `{hash, prev, consensus}` graph, finds
genesis, and greedily walks to the heaviest subtree. If the selected tip
differs from HEAD, `storage_checkout()` persists the reorg.

---

## Storage (`storage.c`)

Git-style content-addressed object store. The six public functions map directly
to git's plumbing operations:

| Function | git equivalent |
|---|---|
| `storage_insert(block)` | `git hash-object -w` |
| `storage_read(hash)` | `git cat-file blob <hash>` |
| `storage_exists(hash)` | `git cat-file -e <hash>` |
| `storage_head(buf, size)` | `git rev-parse HEAD` |
| `storage_checkout(hash)` | `git checkout <hash>` |
| `storage_scan(offset, count)` | `git log --format=%H` |

**Insert and checkout are separate operations.** A node stores a received block
for validation before making it HEAD. `storage_insert()` is idempotent and does
not update HEAD.

**Durability.** Every block file is written with `fflush` + `fsync` before
`fclose`. A partially written file is deleted before returning failure.

**Merkle re-verification on read.** Both `storage_read()` variants recompute
`compute_merkle_root()` after `fread()` and reject any block whose stored
`merkle_root` does not match. This catches both disk corruption and post-commit
transaction tampering.

**Genesis sentinel.** Genesis `previous_hash` is `"0"` (ASCII zero, null
terminated). `GENESIS_PREVIOUS_HASH` is exported for consistent use across modules.

---

## Cryptography

### SHA-256 (`sha256.c`)

Self-contained FIPS 180-4 implementation (~130 lines, no external dependencies
in the hash path). OpenSSL is retained only for TLS.

```c
void sha256_init  (sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const void *data, size_t len);
void sha256_final (sha256_ctx *ctx, uint8_t digest[SHA256_DIGEST_LEN]);
void sha256_digest(const void *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]);
void sha256_to_hex(const uint8_t *bytes, size_t len, char *out);
```

`sha256_final` wipes the context after producing the digest (no state leakage).
`sha256_to_hex` is the shared hex encoder — used by `crypto.c` and `pow.c`.

### Crypto utilities (`crypto.c`)

`block_hash()` feeds `index`, `timestamp`, `previous_hash`, `merkle_root`,
`nonce`, and `consensus` through `sha256_digest()` and stores the 64-char hex
result in `block->hash`.

`compute_merkle_root()` feeds `sender + recipient + amount + nonce` (raw bytes,
fixed-width, no `strlen`) through incremental `sha256_update` for each
transaction. The deterministic byte layout ensures two nodes always produce
identical roots for identical transaction sets.

---

## Transaction (`transaction.c`)

**Key constants:**

```c
#define MAX_PUBLIC_KEY_LENGTH  1952          /* Dilithium-3 public key  */
#define MAX_SIGNATURE_LENGTH   3293          /* Dilithium-3 signature   */
#define MICRO_PER_TOKEN        1000000ULL    /* 1 token = 1,000,000 µ   */
```

`amount` is stored as `uint64_t` in **micro-units** — the same convention as
Bitcoin satoshis. All hashing and comparison operations are bitwise-identical
across platforms. Floating-point is never used in the core.

`uint64_t nonce` provides replay protection — a per-sender sequence number
included in the signed message and in the Merkle root.

**`build_message()`** is a static helper that constructs the canonical signed
message using fixed-width fields:

```
sender[1952] | recipient[1952] | amount(8B LE) | nonce(8B LE)
```

Both `sign_transaction()` and `verify_transaction()` call `build_message()` —
they always sign the same bytes for the same struct state.

A compile-time `_Static_assert` verifies `MAX_SIGNATURE_LENGTH >= OQS_SIG_dilithium_3_length_signature`.

---

## Proof of Work (`pow.c`)

Mining scans the nonce field until the block's SHA-256 hex hash has at least
`DIFFICULTY` leading `'0'` characters.

**Midstate optimization:** all fields except `nonce` are constant during a
mining run. `pow.c` pre-computes the SHA-256 midstate over the invariant fields
once, then only re-hashes the nonce on each iteration. This roughly doubles
hash throughput.

`validate_block_pow(block, difficulty)` checks both the leading-zero target and
`block_verify_hash()`. Called by `verify_pow_rules()` in `consensus.c`.

---

## Proof of Stake and VRF

### Consensus Dispatch (`consensus.c`)

A function-pointer table indexed by `block->consensus`:

```c
typedef int (*consensus_fn)(const Block *);

static const consensus_fn VERIFY[] = {
    [CONSENSUS_POW] = verify_pow_rules,
    [CONSENSUS_POS] = verify_pos_rules,
};
```

Adding a new consensus type requires only a new array entry and a new
`verify_*_rules` function. `verify_consensus()` itself is never modified.

### PoS Validation Pipeline (`verify_pos_rules`)

```c
/* 1. Hash integrity                          (block_verify_hash)        */
/* 2a. Proposer stake check                   (validator_check_stake)    */
/* 2b. Equivocation guard                     (equivocation_check)       */
/* 2c. VRF proof verification                 (vrf_verify)               */
/* 2d. Dilithium-3 block signature            (block_verify_sig)         */
```

### VRF Construction (`vrf.c`)

```
slot_message   = SHA-256(slot_le64 || prev_block_hash[32])
selection_hash = SHA-256(validator_id || slot_message[32])
is_elected     = (selection_hash_u64be % total_stake) < validator_stake
proof.sig      = Dilithium-3_sign(slot_message, private_key)
proof.output   = selection_hash
```

**Verification (three steps):**
1. `OQS_SIG_verify(slot_message, proof.sig, public_key)` — signature integrity
2. `SHA-256(validator_id || slot_message) == proof.output` — output binding
3. `(output_u64be % total_stake) < validator_stake` — election eligibility

**API:**

| Function | Description |
|---|---|
| `vrf_slot_message(slot, prev_hash, out)` | Compute public slot commitment |
| `vrf_selection_hash(id, slot_msg, out)` | Compute validator-specific election ticket |
| `vrf_is_elected(hash, stake, total)` | Election test (guards division by zero) |
| `vrf_prove(id, slot_msg, sk, sk_len, proof)` | Sign and populate `VRFProof` |
| `vrf_verify(id, slot_msg, proof, pk, pk_len, stake, total)` | Three-step verify |

### Validator Registry (`validator.c`)

Stores validator identity and stake in `.chain/validators/<id>` (one binary
`Validator` struct per file).

**`Validator` record:**

| Field | Type | Description |
|---|---|---|
| `id` | `char[65]` | Unique label or SHA-256 hex of public key |
| `public_key` | `uint8_t[1952]` | Dilithium-3 public key |
| `stake` | `uint64_t` | Locked stake in micro-units |

**`VALIDATOR_MIN_STAKE`** = 1 token (1,000,000 micro-units).

`ValidatorRegistry` is opaque. Callers only see the five public functions:
`validator_registry_load`, `validator_registry_free`, `validator_register`,
`validator_lookup`, `validator_check_stake`.

### Equivocation Guard (`equivocation.c`)

Prevents a PoS proposer from submitting two blocks for the same slot.

```
.chain/slots/<proposer_id>   — binary uint32_t: last committed slot number
```

`equivocation_check()` is called by `verify_pos_rules()` (read-only).
`equivocation_record()` is called by `chain_add()` after a successful commit
(write). A recording failure is non-fatal — the block is already on disk.

---

## Mempool and Wallet

### Mempool (`mempool.c`)

Pending signed transactions are stored as individual files in
`.chain/mempool/<txhash>`, where `txhash = SHA-256(sender || recipient || amount || nonce)`.

Content-addressed storage makes `mempool_add()` idempotent and `mempool_purge()`
O(1) per transaction (no scanning required).

**API:**

| Function | Description |
|---|---|
| `mempool_add(tx)` | Persist signed `Transaction` to disk |
| `mempool_load_all(out, max, count_out)` | Load up to `max` pending transactions |
| `mempool_purge(txns, count)` | Remove named transactions after mining |
| `mempool_count()` | Return number of pending entries |

### Wallet (`wallet.c` / `key.c`)

`wallet.c` is a **key loader only** — it reads `.chain/keys/<id>.pk` and
`.chain/keys/<id>.sk`. Key *generation* is handled exclusively by the
`build/utils/key_gen` utility, which writes key files and refuses to overwrite
existing ones.

The `send` → `mine` flow:
1. `zuno send` signs the transaction at submission time using the sender's
   Dilithium-3 secret key and writes the signed struct to the mempool
2. `zuno mine` loads mempool entries, verifies each signature against the
   sender's public key, and includes only verified transactions in the PoW block
3. After `chain_add()` succeeds, mined transactions are purged from the mempool

---

## Network (`network.c`)

All configuration passes through `NetConfig`. No compile-time constants for
runtime values:

```c
typedef struct {
    const char *cert_file;   /* PEM path for server cert / client cert */
    const char *key_file;    /* PEM path for private key               */
    const char *ca_file;     /* CA bundle for peer verification        */
    const char *bind_addr;   /* server bind address (NULL = 0.0.0.0)  */
    uint16_t    port;        /* server listen port                     */
    const char *pqc_group;   /* e.g. "p256_kyber768" or NULL           */
} NetConfig;
```

**Security contracts:**
- TLS 1.3 minimum enforced on every context
- Client contexts always set `SSL_VERIFY_PEER`
- Server contexts enable mutual TLS when `ca_file` is provided
- Bidirectional `tls_shutdown()` on every connection close
- No `exit()` calls — all functions return error codes

**PQC hybrid key exchange:** `p256_kyber768` provides defence-in-depth — if
classical ECC is broken by a quantum adversary, Kyber-768 still protects the
session. The OQS OpenSSL provider must be loaded at runtime; if absent, the
implementation falls back to classical TLS transparently.

**`net_serialize_block()`** produces a `key:value\n` wire format containing
only header fields. `net_deserialize_block()` calls `block_verify_hash()` on
the parsed result before returning — integrity is enforced at the
deserialization layer.

**API:**

```c
NetContext *net_context_server(const NetConfig *cfg);
NetContext *net_context_client(const NetConfig *cfg);
void        net_context_free  (NetContext *ctx);           /* NULL-safe */

int net_serialize_block  (const Block *block, char *buf, size_t bufsz);
int net_deserialize_block(const char *buf, size_t len, Block *out);
int net_broadcast_block  (NetContext *ctx, const Block *block,
                          const char *peer_addr, uint16_t peer_port);
int net_server_run       (NetContext *ctx, Chain *chain);

NetProviders *net_providers_load(void);
void          net_providers_free(NetProviders *p);
```

---

## Logging (`log.c`)

All formatting happens on the calling thread's stack before the mutex is
acquired. No `malloc` anywhere in the call path — log-dependent heisenbugs
are eliminated by construction.

| Level | Flushed? | Compiled in release? |
|---|---|---|
| `LOG_LEVEL_ERROR` | Yes | Always |
| `LOG_LEVEL_WARN` | Yes | Always (minimum clamp) |
| `LOG_LEVEL_INFO` | No | Always |
| `LOG_LEVEL_DEBUG` | No | Debug only (`-DDEBUG`) |

`log_set_level()` clamps to `LOG_LEVEL_WARN` minimum — ERROR and WARN cannot
be suppressed.

In release builds, `__FILE__`, `__func__`, and `__LINE__` are replaced with
`NULL, NULL, 0`. Internal source paths never appear in production log output.

**Security rule:** Never pass private keys, VRF secrets, seed material, or
session tokens to any log macro. Treat all log content as public.

---

## CLI (`main.c`)

Command dispatch uses a static function-pointer table:

```c
typedef struct { const char *name; int (*fn)(int, char **); } Cmd;
static const Cmd CMDS[] = { {"init", cmd_init}, {"send", cmd_send}, … };
```

Each handler does its own flag parsing. `main()` iterates the table with
`strcmp`. Unknown commands print usage and return `1`.

**Exit codes:** `0` success · `1` usage/argument error · `2` chain/storage
runtime error.

**`propose` resilience:** `chain_propose()` returns `EXIT_SUCCESS` even if all
peers are unreachable. A partial broadcast is acceptable — the node always
keeps its local chain valid regardless of network conditions.
