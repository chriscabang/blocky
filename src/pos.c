/* pos.c — Proof of Stake: validator registry and block selection. */

#include "pos.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

void pos_init(PoSSystem *pos) {
    if (!pos) return;
    memset(pos, 0, sizeof(*pos));
}

void pos_stake(PoSSystem *pos, uint32_t validator_id, uint64_t amount) {
    if (!pos) return;

    /* Update existing validator if found. */
    for (int i = 0; i < pos->validator_count; i++) {
        if (pos->validators[i].id == validator_id) {
            pos->validators[i].stake += amount;
            return;
        }
    }

    /* Register new validator. */
    if (pos->validator_count >= MAX_VALIDATORS) {
        log_warn("pos_stake: validator pool full (MAX_VALIDATORS=%d)", MAX_VALIDATORS);
        return;
    }

    pos->validators[pos->validator_count].id    = validator_id;
    pos->validators[pos->validator_count].stake = amount;
    pos->validator_count++;
}

int pos_select_validator(const PoSSystem *pos, PosEntry *out) {
    if (!pos || !out) {
        log_error("pos_select_validator: NULL argument");
        return EXIT_FAILURE;
    }
    if (pos->validator_count == 0) {
        log_error("pos_select_validator: no validators registered");
        return EXIT_FAILURE;
    }

    uint64_t total_stake = 0;
    for (int i = 0; i < pos->validator_count; i++)
        total_stake += pos->validators[i].stake;

    if (total_stake == 0) {
        log_error("pos_select_validator: total stake is zero");
        return EXIT_FAILURE;
    }

    /*
     * Stake-weighted selection.
     * NOTE: rand() % total_stake has modulo bias for large stake values
     * and is not cryptographically secure.  This will be replaced by a
     * VRF-based leader selection scheme per ADR-003.
     */
    uint64_t target  = (uint64_t)rand() % total_stake;
    uint64_t running = 0;

    for (int i = 0; i < pos->validator_count; i++) {
        running += pos->validators[i].stake;
        if (target < running) {
            *out = pos->validators[i];
            return EXIT_SUCCESS;
        }
    }

    /* Unreachable when total_stake > 0 — fallback for safety. */
    *out = pos->validators[pos->validator_count - 1];
    return EXIT_SUCCESS;
}

int pos_validate_block(const Block *block, const PosEntry *validator) {
    if (!block || !validator) {
        log_error("pos_validate_block: NULL argument");
        return EXIT_FAILURE;
    }

    /*
     * Placeholder: PoS validates every block whose index is divisible by 10.
     * Full VRF proof + Dilithium-3 block signature + stake check pending ADR-003.
     */
    if (block->index % 10 == 0) {
        log_info("pos_validate_block: block %u validated by validator %u"
                 " (stake=%llu)",
                 block->index, validator->id,
                 (unsigned long long)validator->stake);
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}
