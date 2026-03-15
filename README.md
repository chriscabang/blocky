# zuno

**zuno** — *Zero-trust Unalterable Notarized Object-store*

A personal study of blockchain fundamentals implemented from scratch in C.
The goal is to understand — by building — how a blockchain actually works:
content-addressed storage, proof-of-work, proof-of-stake, post-quantum
cryptography, peer-to-peer networking over TLS, and consensus in a distributed
system. Every design decision is documented with its rationale.

Named after Zuno from *Dragon Ball Super* — the omniscient being who holds the
answer to every question. A blockchain is the same: an immutable, tamper-proof
record of all truth, forgotten by no one.

| Word | Property |
|---|---|
| **Zero-trust** | No central authority — every block cryptographically verified by every peer |
| **Unalterable** | Hash-chained immutable ledger — tampering breaks the chain |
| **Notarized** | Dilithium-3 post-quantum signatures on transactions and blocks |
| **Object-store** | Git-style `.chain/blocks/` content-addressed storage, one file per hash |

---

## What This Project Covers

- **Git-like workflow** — `send` queues a signed transaction, `mine` seals a
  PoW block, `propose` broadcasts it to peers
- **Post-quantum cryptography** — Dilithium-3 (NIST level 3) transaction
  signatures; p256_kyber768 hybrid TLS key exchange
- **Dual consensus** — Proof-of-Work (SHA-256, configurable difficulty) and
  Proof-of-Stake with VRF leader election (every 10th block)
- **GHOST fork choice** — canonical chain selection by subtree weight, not
  just longest chain
- **Self-contained SHA-256** — FIPS 180-4 implementation, no OpenSSL in the
  hot path
- **TLS 1.3 minimum** on all peer connections; bidirectional close_notify
- **SOLID architecture in C** — no global state, no `exit()` in library code,
  one module per responsibility
- **336 unit tests** across 20 CMocka test suites

---

## Documentation

| Document | Contents |
|---|---|
| `docs/ARCHITECTURE.md` | Module map, dependency graph, data flow, consensus model, design philosophy |
| `docs/DESIGN.md` | Technical implementation details for each module and subsystem |
| `docs/QUICKSTART.md` | Prerequisites, build instructions, step-by-step usage guides, utilities |
| `docs/CONTRIBUTING.md` | Coding standards, testing requirements, ADR format |
| `docs/NOTES.md` | Architecture Decision Records (ADR-001 – ADR-019) — the authoritative *why* |

---

## CLI Reference

```
Usage: zuno <command> [options]

Commands:
  init                                 Initialise chain (creates genesis block)
  status                               Show chain tip and mempool count
  log [--limit N]                      List recent blocks (default 10)
  show <hash>                          Show block details (human-readable)
  cat  <hash>                          Raw field dump of a block
  verify <hash>                        Verify a block's hash integrity
  send --from <s> --to <r> --amount <a>
                                       Sign and queue a transaction (mempool)
  mine                                 Build a PoW block from mempool transactions
  propose                              Broadcast tip to peers via .chain/peers
  version                              Print version
  help [command]                       Show this help or per-command help
```

**Amounts** are decimal tokens (e.g. `10.5`). Internally stored as micro-units
(1 token = 1,000,000 µ) to avoid floating-point arithmetic in the core.

**Exit codes:** `0` success · `1` usage/argument error · `2` runtime error.

---

## Project Layout

```
zuno/
├── src/                    C source files
│   ├── main.c              CLI dispatcher
│   ├── chain.c             In-memory chain with pool allocator
│   ├── block.c             Block create / hash / verify / sign
│   ├── storage.c           Git-style .chain/ object store
│   ├── network.c           TLS 1.3 server, client, block serialization
│   ├── consensus.c         Consensus dispatcher (PoW vs PoS)
│   ├── equivocation.c      Slot-based equivocation guard
│   ├── pow.c               Proof-of-Work mining (midstate optimization)
│   ├── pos.c               Proof-of-Stake helper (PosEntry, stake selection)
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
├── docs/
│   ├── ARCHITECTURE.md     Architecture overview and design philosophy
│   ├── DESIGN.md           Technical implementation reference
│   ├── QUICKSTART.md       Setup, build, and usage guide
│   ├── CONTRIBUTING.md     Coding standards and contribution guidelines
│   └── NOTES.md            Architecture Decision Records (ADR-001 – ADR-019)
└── Makefile
```

### Chain Storage Layout

```
.chain/
├── HEAD              — symbolic ref: "ref: refs/heads/main"
├── refs/heads/main   — hash of the current tip block
├── peers             — peer list for propose (one ip:port per line)
├── tls-cert.pem      — node TLS certificate
├── tls-key.pem       — node TLS private key (chmod 600)
├── blocks/           — content-addressed block store (one file per hash)
├── keys/             — Dilithium-3 keypairs (key_gen writes here)
├── mempool/          — pending signed transactions (one file per tx hash)
├── slots/            — equivocation guard (one file per PoS proposer)
└── validators/       — validator registry (one binary file per validator)
```
