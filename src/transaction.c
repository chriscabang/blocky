/* transaction.c — Transaction signing and verification via Dilithium-3 (liboqs). */

#include <oqs/oqs.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "transaction.h"

/*
 * Compile-time guard: catch mismatches between our constant and the
 * actual Dilithium-3 signature size reported by the installed liboqs.
 */
#if defined(OQS_SIG_dilithium_3_length_signature)
_Static_assert(MAX_SIGNATURE_LENGTH >= OQS_SIG_dilithium_3_length_signature,
               "MAX_SIGNATURE_LENGTH too small for Dilithium-3");
#endif

/*
 * Build the canonical message buffer from a transaction's fields.
 * Layout: sender[MAX_PUBLIC_KEY_LENGTH] | recipient[MAX_PUBLIC_KEY_LENGTH]
 *         | amount(uint64_t) | nonce(uint64_t)
 *
 * Using the full fixed-width fields (not strlen) keeps the message
 * length constant regardless of label content — sign and verify
 * always hash the same bytes for the same struct state.
 */
static void build_message(const Transaction *tx, uint8_t msg[TX_MESSAGE_LEN])
{
  memcpy(msg,
         tx->sender,    MAX_PUBLIC_KEY_LENGTH);
  memcpy(msg + MAX_PUBLIC_KEY_LENGTH,
         tx->recipient, MAX_PUBLIC_KEY_LENGTH);
  memcpy(msg + MAX_PUBLIC_KEY_LENGTH * 2,
         &tx->amount,   sizeof(tx->amount));
  memcpy(msg + MAX_PUBLIC_KEY_LENGTH * 2 + sizeof(tx->amount),
         &tx->nonce,    sizeof(tx->nonce));
}

int sign_transaction(Transaction *tx, const uint8_t *private_key)
{
  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  if (!sig) {
    log_error("sign_transaction: OQS_SIG_new failed");
    return EXIT_FAILURE;
  }

  uint8_t message[TX_MESSAGE_LEN];
  build_message(tx, message);

  int rc = EXIT_SUCCESS;
  if (OQS_SIG_sign(sig, tx->signature, &tx->signature_length,
                   message, TX_MESSAGE_LEN, private_key) != OQS_SUCCESS) {
    log_error("sign_transaction: OQS_SIG_sign failed");
    rc = EXIT_FAILURE;
  }

  OQS_SIG_free(sig);
  return rc;
}

int verify_transaction(const Transaction *tx, const uint8_t *public_key)
{
  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  if (!sig) {
    log_error("verify_transaction: OQS_SIG_new failed");
    return EXIT_FAILURE;
  }

  uint8_t message[TX_MESSAGE_LEN];
  build_message(tx, message);

  int rc = (OQS_SIG_verify(sig, message, TX_MESSAGE_LEN,
                            tx->signature, tx->signature_length,
                            public_key) == OQS_SUCCESS)
             ? EXIT_SUCCESS
             : EXIT_FAILURE;

  OQS_SIG_free(sig);
  return rc;
}
