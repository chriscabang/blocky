# Contributing to zuno

zuno is a personal study project. Contributions that stay true to its
architectural philosophy are welcome.

---

## Before You Start

Read `docs/ARCHITECTURE.md` for the design philosophy and module structure. Read
`docs/NOTES.md` for the Architecture Decision Records (ADR-001 – ADR-019) —
these explain *why* the code is structured the way it is. Proposals that
conflict with an existing ADR should include a counter-argument or a new ADR.

---

## Coding Standards

- **C only** for core protocol modules (`src/`, `inc/`). C++ or Python is
  acceptable for tooling or scripting, never for chain logic.
- **snake_case everywhere** — identifiers, filenames, and binary names.
- **No `exit()` in library code.** Every function returns an error code.
  `main.c` and utility `main()` functions are the only permitted exit points.
- **No global mutable state.** All objects are heap-allocated and caller-owned.
- **Const-correctness.** Functions that do not modify inputs must declare them
  `const`. Violations are bugs.
- **Private key material** must be zeroed with `OQS_MEM_cleanse` before `free`.
- Follow the SOLID principles as applied in `docs/ARCHITECTURE.md`. A module that
  does two things should be two modules.

---

## Testing Requirements

Every `.h`/`.c` unit must have a corresponding `tests/test_<unit>.c` using
CMocka. A change is not complete until `make test` passes with the new suite
included.

- One test file per unit.
- `setup`/`teardown` must clean up all side effects, including any `.chain/`
  directories created during the test.
- Group tests by function under test using `cmocka_run_group_tests_name`.
- Run `make check` (tests + coverage) before submitting.

---

## Architecture Decision Records

Significant design decisions are recorded as ADRs in `docs/NOTES.md`. Add a new
ADR whenever a decision is made that affects module responsibilities, public
APIs, on-disk formats, security contracts, or build conventions.

ADR format:

```markdown
### ADR-NNN: Short Title

**Date:** YYYY-MM · **Status:** Adopted | Superseded | Rejected
**Files:** `inc/foo.h`, `src/foo.c`

#### Context
Why does this decision need to be made?

#### Decision
What was decided?

#### Rationale
Why was this option chosen over alternatives?
```

---

## Security Notes

- Never introduce `exit()`, global state, or unchecked return values in library
  code.
- Never log private key material, VRF secrets, seed material, or session
  tokens. Treat all log output as public.
- TLS 1.3 minimum must be enforced on all new network code.
- Any new signing or hashing must use the existing `sha256.c` or `liboqs`
  primitives — do not introduce new cryptographic dependencies.
