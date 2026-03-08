/* key.h — Dilithium-3 key store for zuno identities.
 *
 * Owns everything about .chain/keys/:
 *   .chain/keys/<id>.pk   public key  (MAX_PUBLIC_KEY_LENGTH bytes, mode 0644)
 *   .chain/keys/<id>.sk   secret key  (KEY_SK_LEN bytes,            mode 0600)
 *
 * Generate a keypair before sending transactions:
 *   key_generate("alice");        // library call
 *   build/utils/key_gen alice     // CLI equivalent
 *
 * IMPORTANT: zero sk_out immediately after use to prevent key material leaks.
 */
#ifndef KEY_H
#define KEY_H

#include "transaction.h" /* MAX_PUBLIC_KEY_LENGTH */
#include <stddef.h>
#include <stdint.h>

#define KEYS_DIR   ".chain/keys"

/*
 * Maximum Dilithium-3 secret key length.
 * Calibrated to OQS_SIG_dilithium_3_length_secret_key (4000 bytes).
 */
#define KEY_SK_LEN 4000

/*
 * Generate a Dilithium-3 keypair for 'id' and write it to the key store.
 * Creates .chain/keys/ if it does not exist.
 * Returns EXIT_FAILURE if id is invalid, files already exist, or OQS fails.
 * id must not be empty or contain '/'.
 */
int key_generate(const char *id);

/*
 * Load the public key for 'id' into pk_out (must be >= MAX_PUBLIC_KEY_LENGTH).
 * Returns EXIT_FAILURE if the file is absent or a read error occurs.
 */
int key_load_pk(const char *id, uint8_t *pk_out, size_t pk_len);

/*
 * Load the secret key for 'id' into sk_out (must be >= KEY_SK_LEN).
 * Returns EXIT_FAILURE if the file is absent or a read error occurs.
 * IMPORTANT: caller must zero sk_out after use (memset(sk, 0, KEY_SK_LEN)).
 */
int key_load_sk(const char *id, uint8_t *sk_out, size_t sk_len);

/*
 * Return 1 if a public key file (.chain/keys/<id>.pk) exists, 0 otherwise.
 */
int key_exists(const char *id);

#endif /* KEY_H */
