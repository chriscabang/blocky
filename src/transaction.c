/* transaction.c */
// Handles transactions (signing and verification) using Dilithium signatures
// from liboqs.
#include <oqs/oqs.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "transaction.h"

void sign_transaction(Transaction *tx, const uint8_t *private_key) {
  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  if (!sig) {
    printf("Failed to initialize signature scheme.\n");
    return;
  }

  size_t message_len =
      sizeof(tx->sender) + sizeof(tx->recipient) + sizeof(tx->amount);
  uint8_t message[message_len];

  memcpy(message, tx->sender, sizeof(tx->sender));
  memcpy(message + sizeof(tx->sender), tx->recipient, sizeof(tx->recipient));
  memcpy(message + sizeof(tx->sender) + sizeof(tx->recipient), &tx->amount,
         sizeof(tx->amount));

  // Fix the signature length
  if (OQS_SIG_sign(sig, tx->signature, &tx->signature_length, message,
                   message_len, private_key) != OQS_SUCCESS) {
    printf("Failed to sign transaction.\n");
  }

  OQS_SIG_free(sig);
}

int verify_transaction(const Transaction *tx, const uint8_t *public_key) {
  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  if (!sig) {
    printf("Failed to initialize signature scheme.\n");
    return 0;
  }

  size_t message_len =
      sizeof(tx->sender) + sizeof(tx->recipient) + sizeof(tx->amount);
  uint8_t message[message_len];

  memcpy(message, tx->sender, sizeof(tx->sender));
  memcpy(message + sizeof(tx->sender), tx->recipient, sizeof(tx->recipient));
  memcpy(message + sizeof(tx->sender) + sizeof(tx->recipient), &tx->amount,
         sizeof(tx->amount));

  int valid = OQS_SIG_verify(sig, message, message_len, tx->signature,
                             tx->signature_length, public_key) == OQS_SUCCESS;

  OQS_SIG_free(sig);
  return valid;
}
