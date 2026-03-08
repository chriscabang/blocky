/* pos.h — Proof of Stake: validator registry and block selection.
 *
 * This module manages the validator set and stake-weighted leader selection.
 * Full VRF-based selection and Dilithium-3 block signing are pending ADR-003.
 *
 * All functions follow the module_verb naming convention and return
 * EXIT_SUCCESS / EXIT_FAILURE.  No exit() calls.
 */

#ifndef POS_H
#define POS_H

#include "block.h"
#include <stdint.h>
#include <stdlib.h>

#define MAX_VALIDATORS 100

typedef struct {
    uint32_t id;      /* unique validator identifier */
    uint64_t stake;   /* registered stake amount in micro-units */
} PosEntry;

typedef struct {
    PosEntry validators[MAX_VALIDATORS];
    int      validator_count;
} PoSSystem;

/*
 * Initialise a PoSSystem to an empty validator set.
 * Must be called before any other pos_* function.
 */
void pos_init(PoSSystem *pos);

/*
 * Register or add stake for a validator.
 * If validator_id already exists, amount is added to its existing stake.
 * Silently ignored if the validator pool is full (MAX_VALIDATORS).
 */
void pos_stake(PoSSystem *pos, uint32_t validator_id, uint64_t amount);

/*
 * Select a validator by stake-weighted random sampling.
 * Fills *out with the chosen Validator on success.
 *
 * Returns EXIT_FAILURE (without crashing) if:
 *   - pos or out is NULL
 *   - no validators are registered
 *   - total stake is zero
 *
 * NOTE: currently uses rand() which has modulo bias and is not
 * cryptographically secure.  Will be replaced by VRF per ADR-003.
 *
 * Returns EXIT_SUCCESS on success.
 */
int pos_select_validator(const PoSSystem *pos, PosEntry *out);

/*
 * Validate a block under PoS rules.
 *
 * Placeholder: validates every block whose index is divisible by 10.
 * Full VRF proof + Dilithium-3 signature + stake check pending ADR-003.
 *
 * Returns EXIT_SUCCESS if the block passes, EXIT_FAILURE otherwise.
 */
int pos_validate_block(const Block *block, const PosEntry *validator);

#endif /* POS_H */
