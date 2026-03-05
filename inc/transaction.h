/* transaction.h */
#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stdint.h>
#include <stddef.h>

/*
 * Sizes calibrated to Dilithium-3 (OQS_SIG_alg_dilithium_3):
 *   public key : 1952 bytes  (OQS_SIG_dilithium_3_length_public_key)
 *   signature  : 3293 bytes  (OQS_SIG_dilithium_3_length_signature)
 *
 * These fields currently hold plain name/label strings (e.g. "alice").
 * They are intentionally sized for PQC keys so no struct or on-disk
 * format change is needed when Dilithium integration lands (ADR-003).
 */
#define MAX_PUBLIC_KEY_LENGTH 1952
#define MAX_SIGNATURE_LENGTH  3293

/*
 * Amount is stored as a fixed-point integer in micro-units.
 * 1 token == MICRO_PER_TOKEN micro-units.
 * Using uint64_t avoids the rounding errors inherent in IEEE 754 doubles
 * for currency values (e.g. 0.1 + 0.2 != 0.3 in floating point).
 */
#define MICRO_PER_TOKEN 1000000ULL

/*
 * Byte length of the message buffer signed / verified per transaction:
 * sender (MAX_PUBLIC_KEY_LENGTH) + recipient (MAX_PUBLIC_KEY_LENGTH)
 * + amount (uint64_t) + nonce (uint64_t).
 */
#define TX_MESSAGE_LEN  (MAX_PUBLIC_KEY_LENGTH * 2 + sizeof(uint64_t) * 2)

typedef struct {
  char     sender[MAX_PUBLIC_KEY_LENGTH];    /* sender public key (or label) */
  char     recipient[MAX_PUBLIC_KEY_LENGTH]; /* recipient public key (or label) */
  uint64_t amount;                           /* transfer amount in micro-units */
  uint64_t nonce;                            /* per-sender sequence number (replay protection) */
  uint8_t  signature[MAX_SIGNATURE_LENGTH];
  size_t   signature_length;
} Transaction;

/*
 * Sign a transaction with a Dilithium-3 private key.
 * Covers: sender + recipient + amount + nonce.
 * Returns EXIT_SUCCESS on success, EXIT_FAILURE on any error.
 *
 * NOTE: the private key is passed as a raw pointer. Key material must
 * be zeroed by the caller after this call returns.
 */
int sign_transaction(Transaction *tx, const uint8_t *private_key);

/*
 * Verify a transaction's Dilithium-3 signature against the public key.
 * Returns EXIT_SUCCESS if the signature is valid, EXIT_FAILURE otherwise.
 */
int verify_transaction(const Transaction *tx, const uint8_t *public_key);

#endif /* TRANSACTION_H */
