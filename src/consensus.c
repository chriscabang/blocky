/* consensus.c — consensus routing: PoW and PoS block validation. */
#include "consensus.h"
#include "validator.h"
#include "vrf.h"
#include "sha256.h"
#include "pow.h"
#include "block.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

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
 * PoS rules: hash integrity + proposer identity + VRF proof + block signature.
 */
static int verify_pos_rules(const Block *block) {
    /* 1. Hash integrity — always required. */
    if (block_verify_hash(block) != EXIT_SUCCESS) {
        log_error("verify_consensus: PoS hash integrity failed for block %u",
                  block->index);
        return EXIT_FAILURE;
    }

    /* 2. Proposer checks: every PoS block must identify its proposer. */
    if (block->proposer_id[0] == '\0') {
        log_warn("verify_consensus: PoS block %u has no proposer_id",
                 block->index);
        return EXIT_FAILURE;
    }

    ValidatorRegistry *reg = validator_registry_load();
    if (!reg) {
        log_warn("verify_consensus: cannot load validator registry for block %u",
                 block->index);
        return EXIT_FAILURE;
    }

    const Validator *proposer = validator_lookup(reg, block->proposer_id);
    if (!proposer) {
        log_warn("verify_consensus: proposer '%s' not in registry (block %u)",
                 block->proposer_id, block->index);
        validator_registry_free(reg);
        return EXIT_FAILURE;
    }

    /* 2a. Stake check. */
    if (validator_check_stake(reg, block->proposer_id) != EXIT_SUCCESS) {
        log_warn("verify_consensus: proposer '%s' has insufficient stake (block %u)",
                 block->proposer_id, block->index);
        validator_registry_free(reg);
        return EXIT_FAILURE;
    }

    /* 2b. VRF proof: proposer was elected for this slot. */
    uint64_t total = validator_total_stake(reg);
    uint8_t  prev_raw[SHA256_DIGEST_LEN];
    uint8_t  slot_msg[VRF_OUTPUT_LEN];

    /* For genesis previous_hash ("0"), treat as all-zero raw bytes. */
    memset(prev_raw, 0, SHA256_DIGEST_LEN);
    if (strlen((const char *)block->previous_hash) == SHA256_HEX_LEN) {
        sha256_from_hex((const char *)block->previous_hash,
                        prev_raw, SHA256_DIGEST_LEN);
    }
    vrf_slot_message((uint64_t)block->index, prev_raw, slot_msg);

    if (vrf_verify(block->proposer_id, slot_msg, &block->vrf_proof,
                   proposer->public_key, sizeof(proposer->public_key),
                   proposer->stake, total) != EXIT_SUCCESS) {
        log_error("verify_consensus: VRF proof invalid for block %u proposer '%s'",
                  block->index, block->proposer_id);
        validator_registry_free(reg);
        return EXIT_FAILURE;
    }

    /* 2c. Block signature: proposer endorsed this specific block. */
    if (block_verify_sig(block,
                         proposer->public_key,
                         sizeof(proposer->public_key)) != EXIT_SUCCESS) {
        log_error("verify_consensus: block signature invalid for block %u",
                  block->index);
        validator_registry_free(reg);
        return EXIT_FAILURE;
    }

    validator_registry_free(reg);
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
    if (!block || block->proposer_id[0] == '\0') {
        log_warn("verify_block_signature: NULL block or no proposer_id");
        return EXIT_FAILURE;
    }
    if (block->proposer_sig_len == 0) {
        log_warn("verify_block_signature: block %u has no signature",
                 block->index);
        return EXIT_FAILURE;
    }

    ValidatorRegistry *reg = validator_registry_load();
    if (!reg) {
        log_error("verify_block_signature: cannot load validator registry");
        return EXIT_FAILURE;
    }

    const Validator *v = validator_lookup(reg, block->proposer_id);
    if (!v) {
        log_warn("verify_block_signature: proposer '%s' not in registry",
                 block->proposer_id);
        validator_registry_free(reg);
        return EXIT_FAILURE;
    }

    int rc = block_verify_sig(block, v->public_key, sizeof(v->public_key));
    validator_registry_free(reg);
    return rc;
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
