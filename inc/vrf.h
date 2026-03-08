/**
 * @file vrf.h
 * @brief VRF leader selection — slot-based, stake-weighted, Dilithium-3 proven.
 *
 * Construction (ADR-018):
 *   slot_message   = SHA-256(slot_le64 || prev_block_hash[32])
 *   selection_hash = SHA-256(validator_id || slot_message[32])
 *   is_elected     = (selection_hash_u64be % total_stake) < validator_stake
 *   proof.sig      = Dilithium-3_sign(slot_message, private_key)
 *   proof.output   = selection_hash
 *
 * Verification (three-step):
 *   1. Dilithium-3_verify(slot_message, proof.sig, public_key)
 *   2. Recompute selection_hash; compare with proof.output.
 *   3. Check is_elected condition.
 *
 * The VRF output (selection_hash) is deterministic for a given validator and
 * slot but unpredictable to other validators before the private key is used.
 * The Dilithium-3 proof binds the output to the slot, preventing equivocation.
 */

#ifndef VRF_H
#define VRF_H

#include "sha256.h"
#include "transaction.h"   /* MAX_SIGNATURE_LENGTH, MAX_PUBLIC_KEY_LENGTH */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/** VRF output and slot message are both SHA-256 digests (32 bytes). */
#define VRF_OUTPUT_LEN SHA256_DIGEST_LEN

/**
 * VRFProof — compact bundle produced by vrf_prove() and consumed by vrf_verify().
 *
 * Fields:
 *   output  — selection_hash = SHA-256(validator_id || slot_message); this is
 *             the VRF output used in the election test.
 *   sig     — Dilithium-3 signature over slot_message; proves the holder of
 *             the matching private key committed to this slot.
 *   sig_len — actual length of sig (≤ MAX_SIGNATURE_LENGTH = 3293 bytes).
 */
typedef struct {
  uint8_t output[VRF_OUTPUT_LEN];
  uint8_t sig[MAX_SIGNATURE_LENGTH];
  size_t  sig_len;
} VRFProof;

/**
 * vrf_slot_message — derive the public slot commitment.
 *
 * Computes SHA-256(slot_le64 || prev_hash[32]) into @out[VRF_OUTPUT_LEN].
 * No-op if @prev_hash or @out is NULL.
 *
 * @param slot       slot number (encoded as 8 little-endian bytes)
 * @param prev_hash  previous block hash (SHA256_DIGEST_LEN bytes)
 * @param out        output buffer (VRF_OUTPUT_LEN bytes)
 */
void vrf_slot_message(uint64_t slot,
                      const uint8_t *prev_hash,
                      uint8_t out[VRF_OUTPUT_LEN]);

/**
 * vrf_selection_hash — derive the validator-specific election ticket.
 *
 * Computes SHA-256(validator_id || slot_msg[32]) into @out[VRF_OUTPUT_LEN].
 * No-op if @validator_id, @slot_msg, or @out is NULL.
 *
 * @param validator_id  NUL-terminated validator ID string
 * @param slot_msg      slot message (VRF_OUTPUT_LEN bytes)
 * @param out           output buffer (VRF_OUTPUT_LEN bytes)
 */
void vrf_selection_hash(const char     *validator_id,
                        const uint8_t   slot_msg[VRF_OUTPUT_LEN],
                        uint8_t         out[VRF_OUTPUT_LEN]);

/**
 * vrf_is_elected — stake-weighted election test.
 *
 * Returns 1 if (selection_hash_u64be % total_stake) < validator_stake, 0 otherwise.
 * Always returns 0 when total_stake == 0 (guards division by zero).
 *
 * @param selection_hash   VRF_OUTPUT_LEN-byte hash; first 8 bytes used as big-endian u64
 * @param validator_stake  this validator's stake in micro-units
 * @param total_stake      sum of all validators' stakes in the registry
 */
int vrf_is_elected(const uint8_t selection_hash[VRF_OUTPUT_LEN],
                   uint64_t      validator_stake,
                   uint64_t      total_stake);

/**
 * vrf_prove — generate a VRF proof for a slot.
 *
 * Signs @slot_msg with the Dilithium-3 @private_key and fills @proof:
 *   proof->output  = SHA-256(validator_id || slot_msg)
 *   proof->sig     = Dilithium-3_sign(slot_msg, private_key)
 *   proof->sig_len = actual signature length
 *
 * @param validator_id  NUL-terminated validator ID string
 * @param slot_msg      slot message (VRF_OUTPUT_LEN bytes, from vrf_slot_message)
 * @param private_key   Dilithium-3 private key bytes
 * @param sk_len        length of @private_key (used for logging only)
 * @param proof         output VRFProof to fill
 *
 * Returns EXIT_SUCCESS on success, EXIT_FAILURE on any error.
 */
int vrf_prove(const char    *validator_id,
              const uint8_t  slot_msg[VRF_OUTPUT_LEN],
              const uint8_t *private_key,
              size_t         sk_len,
              VRFProof      *proof);

/**
 * vrf_verify — verify a VRF proof and check election eligibility.
 *
 * Steps:
 *   1. Verify proof->sig over @slot_msg using @public_key (Dilithium-3).
 *   2. Recompute selection_hash; compare with proof->output.
 *   3. Check is_elected condition.
 *
 * Returns EXIT_SUCCESS if all three checks pass, EXIT_FAILURE otherwise.
 *
 * @param validator_id    NUL-terminated validator ID string
 * @param slot_msg        slot message (VRF_OUTPUT_LEN bytes)
 * @param proof           proof bundle from vrf_prove()
 * @param public_key      Dilithium-3 public key bytes
 * @param pk_len          length of @public_key (used for logging only)
 * @param validator_stake this validator's stake in micro-units
 * @param total_stake     total stake in the registry
 */
int vrf_verify(const char    *validator_id,
               const uint8_t  slot_msg[VRF_OUTPUT_LEN],
               const VRFProof *proof,
               const uint8_t *public_key,
               size_t         pk_len,
               uint64_t       validator_stake,
               uint64_t       total_stake);

#endif /* VRF_H */
