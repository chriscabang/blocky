/**
 * @file vrf.c
 * @brief VRF leader selection — slot-based, stake-weighted, Dilithium-3 proven.
 */

#include "vrf.h"
#include "log.h"

#include <oqs/oqs.h>
#include <string.h>
#include <stdlib.h>

/* ── private helpers ──────────────────────────────────────────────────── */

/* Encode a uint64_t as 8 little-endian bytes. */
static void encode_le64(uint64_t v, uint8_t out[8])
{
  for (int i = 0; i < 8; i++) {
    out[i] = (uint8_t)(v & 0xFF);
    v >>= 8;
  }
}

/* Decode the first 8 bytes of @b as a big-endian uint64_t. */
static uint64_t decode_be64(const uint8_t b[8])
{
  uint64_t v = 0;
  for (int i = 0; i < 8; i++)
    v = (v << 8) | b[i];
  return v;
}

/* ── public API ───────────────────────────────────────────────────────── */

void vrf_slot_message(uint64_t slot, const uint8_t *prev_hash,
                      uint8_t out[VRF_OUTPUT_LEN])
{
  if (!prev_hash || !out) return;

  uint8_t slot_le[8];
  encode_le64(slot, slot_le);

  sha256_ctx ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, slot_le,   sizeof(slot_le));
  sha256_update(&ctx, prev_hash, SHA256_DIGEST_LEN);
  sha256_final(&ctx, out);
}

void vrf_selection_hash(const char    *validator_id,
                        const uint8_t  slot_msg[VRF_OUTPUT_LEN],
                        uint8_t        out[VRF_OUTPUT_LEN])
{
  if (!validator_id || !slot_msg || !out) return;

  sha256_ctx ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, (const uint8_t *)validator_id, strlen(validator_id));
  sha256_update(&ctx, slot_msg, VRF_OUTPUT_LEN);
  sha256_final(&ctx, out);
}

int vrf_is_elected(const uint8_t selection_hash[VRF_OUTPUT_LEN],
                   uint64_t validator_stake,
                   uint64_t total_stake)
{
  if (total_stake == 0) return 0;
  uint64_t h64 = decode_be64(selection_hash); /* use first 8 bytes as u64be */
  return (h64 % total_stake) < validator_stake ? 1 : 0;
}

int vrf_prove(const char    *validator_id,
              const uint8_t  slot_msg[VRF_OUTPUT_LEN],
              const uint8_t *private_key,
              size_t         sk_len,
              VRFProof      *proof)
{
  if (!validator_id || !slot_msg || !private_key || !proof) {
    log_error("vrf_prove: NULL argument");
    return EXIT_FAILURE;
  }

  /* VRF output = selection_hash = SHA-256(validator_id || slot_msg). */
  vrf_selection_hash(validator_id, slot_msg, proof->output);

  /* Proof = Dilithium-3 signature over slot_msg. */
  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  if (!sig) {
    log_error("vrf_prove: OQS_SIG_new failed");
    return EXIT_FAILURE;
  }

  proof->sig_len = MAX_SIGNATURE_LENGTH;
  OQS_STATUS rc  = OQS_SIG_sign(sig,
                                 proof->sig, &proof->sig_len,
                                 slot_msg,   VRF_OUTPUT_LEN,
                                 private_key);
  OQS_SIG_free(sig);

  if (rc != OQS_SUCCESS) {
    log_error("vrf_prove: OQS_SIG_sign failed (rc=%d, sk_len=%zu)", rc, sk_len);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int vrf_verify(const char     *validator_id,
               const uint8_t   slot_msg[VRF_OUTPUT_LEN],
               const VRFProof *proof,
               const uint8_t  *public_key,
               size_t          pk_len,
               uint64_t        validator_stake,
               uint64_t        total_stake)
{
  if (!validator_id || !slot_msg || !proof || !public_key) {
    log_error("vrf_verify: NULL argument");
    return EXIT_FAILURE;
  }

  /* 1. Verify Dilithium-3 signature over slot_msg. */
  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  if (!sig) {
    log_error("vrf_verify: OQS_SIG_new failed");
    return EXIT_FAILURE;
  }

  OQS_STATUS rc = OQS_SIG_verify(sig,
                                  slot_msg,   VRF_OUTPUT_LEN,
                                  proof->sig, proof->sig_len,
                                  public_key);
  OQS_SIG_free(sig);

  if (rc != OQS_SUCCESS) {
    log_warn("vrf_verify: signature invalid for '%s' (pk_len=%zu)",
             validator_id, pk_len);
    return EXIT_FAILURE;
  }

  /* 2. Recompute selection_hash; must match proof->output. */
  uint8_t expected[VRF_OUTPUT_LEN];
  vrf_selection_hash(validator_id, slot_msg, expected);
  if (memcmp(expected, proof->output, VRF_OUTPUT_LEN) != 0) {
    log_warn("vrf_verify: selection_hash mismatch for '%s'", validator_id);
    return EXIT_FAILURE;
  }

  /* 3. Election check: stake-weighted. */
  if (!vrf_is_elected(proof->output, validator_stake, total_stake)) {
    log_warn("vrf_verify: '%s' not elected (stake=%llu, total=%llu)",
             validator_id,
             (unsigned long long)validator_stake,
             (unsigned long long)total_stake);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
