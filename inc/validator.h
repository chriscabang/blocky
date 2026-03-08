/**
 * @file validator.h
 * @brief Validator registry — Dilithium-3 public keys and locked stake.
 *
 * Each registered validator has:
 *   - a unique string ID (human label or SHA-256 hex of their public key)
 *   - a Dilithium-3 public key (1952 bytes)
 *   - a locked stake amount in micro-units
 *
 * On-disk layout (mirrors the block object store):
 *
 *   .chain/
 *   └── validators/
 *       └── <id>   ← one binary Validator struct per file, named by ID
 *
 * SRP: this module owns validator identity and stake only.
 *      VRF slot selection and block-signature wiring live in consensus.c
 *      (ADR-003, pending).
 *
 * DIP: callers depend on this header (the abstraction); the on-disk layout
 *      and in-memory structure are hidden behind the opaque ValidatorRegistry.
 */

#ifndef VALIDATOR_H
#define VALIDATOR_H

#include "transaction.h"   /* MAX_PUBLIC_KEY_LENGTH, MICRO_PER_TOKEN */

#include <stdint.h>

/** Maximum validator ID length including NUL terminator.
 *  Sized to hold a SHA-256 hex string (64 chars) or a human label. */
#define VALIDATOR_ID_SIZE 65

/** Minimum locked stake required to be eligible to propose a block.
 *  Set to 1 token; adjustable per network configuration (ADR-010). */
#define VALIDATOR_MIN_STAKE (1ULL * MICRO_PER_TOKEN)

/**
 * A single registered validator record.
 *
 * id         — unique string label (human name or SHA-256 hex of public key)
 * public_key — Dilithium-3 public key bytes (MAX_PUBLIC_KEY_LENGTH = 1952)
 * stake      — locked stake in micro-units (1 token = MICRO_PER_TOKEN)
 */
typedef struct {
  char     id[VALIDATOR_ID_SIZE];
  uint8_t  public_key[MAX_PUBLIC_KEY_LENGTH];
  uint64_t stake;
} Validator;

/**
 * Opaque in-memory validator registry.
 * Obtain with validator_registry_load(); release with validator_registry_free().
 */
typedef struct ValidatorRegistry ValidatorRegistry;

/**
 * @brief Load all validators from .chain/validators/ into memory.
 *
 * Returns an empty (count == 0) registry if the directory does not exist
 * or contains no valid entries — this is not an error condition.
 * Corrupt or short-read entries are skipped with a log_warn.
 *
 * @return Heap-allocated ValidatorRegistry. Caller must release with
 *         validator_registry_free(). Returns NULL only on allocation failure.
 */
ValidatorRegistry *validator_registry_load(void);

/**
 * @brief Release a registry and all its resources. Safe to call with NULL.
 */
void validator_registry_free(ValidatorRegistry *reg);

/**
 * @brief Register a new validator or update an existing one.
 *
 * If a validator with the same id already exists, its public_key and stake
 * are overwritten in memory and on disk (idempotent upsert).
 * Persists to .chain/validators/<id>.
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int validator_register(ValidatorRegistry *reg, const Validator *v);

/**
 * @brief Look up a validator by ID.
 *
 * @return Const pointer into the registry, valid until the registry is freed
 *         or modified. Returns NULL if not found or arguments are invalid.
 */
const Validator *validator_lookup(const ValidatorRegistry *reg, const char *id);

/**
 * @brief Check whether a validator meets the minimum stake requirement.
 *
 * @return EXIT_SUCCESS if the validator exists and stake >= VALIDATOR_MIN_STAKE,
 *         EXIT_FAILURE otherwise.
 */
int validator_check_stake(const ValidatorRegistry *reg, const char *id);

/**
 * @brief Return the number of registered validators. Safe to call with NULL.
 */
unsigned int validator_count(const ValidatorRegistry *reg);

#endif /* VALIDATOR_H */
