/* consensus.c — consensus routing: PoW and PoS block validation. */
#include "consensus.h"
#include "pow.h"
#include "block.h"
#include "log.h"

#include <stdlib.h>

/* ── internal rules ───────────────────────────────────────────────────── */

/*
 * PoW rules: the stored hash must satisfy DIFFICULTY leading hex zeros AND
 * match the block's fields.  Delegates to validate_block_pow(), which
 * performs both the difficulty check and block_verify_hash() internally.
 */
static int verify_pow_rules(const Block *block) {
    if (validate_block_pow(block, DIFFICULTY) != EXIT_SUCCESS) {
        log_error("verify_consensus: PoW rules failed for block %u",
                  block->index);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/*
 * PoS rules: the stored hash must match the block's fields (hash integrity).
 *
 * Three security checks are required but deferred to ADR-003:
 *   - VRF proof: the proposer must hold the slot token for this round.
 *   - Dilithium-3 block signature (verify_block_signature).
 *   - Stake check: the proposer must have sufficient stake on record.
 *
 * Hash integrity is enforced now; the stubs below mark where the remaining
 * checks must be inserted before PoS is production-ready.
 */
static int verify_pos_rules(const Block *block) {
    /* 1. Hash integrity — always required. */
    if (block_verify_hash(block) != EXIT_SUCCESS) {
        log_error("verify_consensus: PoS hash integrity failed for block %u",
                  block->index);
        return EXIT_FAILURE;
    }

    /* TODO (ADR-003): verify VRF proof — proposer must hold the slot token. */
    /* TODO (ADR-003): verify Dilithium-3 block signature via
     *                 verify_block_signature(block). */
    /* TODO (ADR-002): verify proposer has sufficient registered stake. */

    return EXIT_SUCCESS;
}

/* ── dispatch table ───────────────────────────────────────────────────── */

typedef int (*consensus_fn)(const Block *);

/*
 * Indexed by the consensus field value (CONSENSUS_POW=0, CONSENSUS_POS=1).
 * Adding a new consensus type: extend the array and add a verify_*_rules
 * function — no changes required to verify_consensus() itself (Open/Closed).
 */
static const consensus_fn VERIFY[] = {
    [CONSENSUS_POW] = verify_pow_rules,
    [CONSENSUS_POS] = verify_pos_rules,
};

#define N_CONSENSUS_TYPES ((int)(sizeof(VERIFY) / sizeof(VERIFY[0])))

/* ── public API ───────────────────────────────────────────────────────── */

int verify_block_signature(const Block *block) {
    (void)block;
    /*
     * Stub: full Dilithium-3 proposer signature verification is pending
     * ADR-003 (validator key registry + VRF leader selection).
     * Returns EXIT_FAILURE so unsigned blocks cannot pass until the key
     * registry is operational.
     */
    log_warn("verify_block_signature: not yet implemented (ADR-003)");
    return EXIT_FAILURE;
}

int verify_consensus(const Block *block) {
    if (!block) {
        log_error("verify_consensus: NULL block");
        return EXIT_FAILURE;
    }

    if (block->consensus >= (uint8_t)N_CONSENSUS_TYPES) {
        log_error("verify_consensus: unknown consensus type %u in block %u",
                  (unsigned)block->consensus, block->index);
        return EXIT_FAILURE;
    }

    return VERIFY[block->consensus](block);
}
