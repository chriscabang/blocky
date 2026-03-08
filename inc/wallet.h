/* wallet.h — Dilithium-3 keypair storage for user identities. */
#ifndef WALLET_H
#define WALLET_H

#include "transaction.h" /* MAX_PUBLIC_KEY_LENGTH */
#include <stddef.h>
#include <stdint.h>

#define WALLET_DIR ".chain/keys"

/*
 * Maximum Dilithium-3 secret key length.
 * Calibrated to OQS_SIG_dilithium_3_length_secret_key (4000 bytes).
 * Size stack / heap allocations for wallet_load_sk output buffers to this.
 * IMPORTANT: zero sk_out immediately after use to prevent key material leaks.
 */
#define WALLET_SK_LEN 4000

/*
 * Generate a Dilithium-3 keypair for 'id' and persist:
 *   .chain/keys/<id>.pk  — public key  (MAX_PUBLIC_KEY_LENGTH bytes, mode 0644)
 *   .chain/keys/<id>.sk  — secret key  (WALLET_SK_LEN bytes, mode 0600)
 *
 * Returns EXIT_FAILURE if a key with the same id already exists (will not
 * overwrite), if OQS keygen fails, or if .chain/keys/ cannot be created.
 */
int wallet_keygen(const char *id);

/*
 * Load the public key for 'id' from .chain/keys/<id>.pk into pk_out.
 * pk_out must be at least MAX_PUBLIC_KEY_LENGTH bytes.
 * Returns EXIT_FAILURE if the file is absent or a read error occurs.
 */
int wallet_load_pk(const char *id, uint8_t *pk_out, size_t pk_len);

/*
 * Load the secret key for 'id' from .chain/keys/<id>.sk into sk_out.
 * sk_out must be at least WALLET_SK_LEN bytes.
 * Returns EXIT_FAILURE if the file is absent or a read error occurs.
 * IMPORTANT: the caller must zero sk_out after use (memset(sk, 0, sk_len)).
 */
int wallet_load_sk(const char *id, uint8_t *sk_out, size_t sk_len);

/*
 * Return 1 if a public key file exists for 'id', 0 otherwise.
 */
int wallet_exists(const char *id);

#endif /* WALLET_H */
