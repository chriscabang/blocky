/* wallet.h — Dilithium-3 keypair loading for user identities.
 *
 * Key generation is handled by the key_gen utility (build/utils/key_gen).
 * This module provides the path-convention layer that cmd_send and cmd_mine
 * use to locate keys stored under .chain/keys/:
 *
 *   .chain/keys/<id>.pk   public key   (MAX_PUBLIC_KEY_LENGTH bytes)
 *   .chain/keys/<id>.sk   secret key   (WALLET_SK_LEN bytes, mode 0600)
 *
 * To generate keys for use with 'zuno send':
 *   build/utils/key_gen <id>
 *   mv <id>.pub .chain/keys/<id>.pk
 *   mv <id>.key .chain/keys/<id>.sk
 *   chmod 600 .chain/keys/<id>.sk
 */
#ifndef WALLET_H
#define WALLET_H

#include "transaction.h" /* MAX_PUBLIC_KEY_LENGTH */
#include <stddef.h>
#include <stdint.h>

#define WALLET_DIR    ".chain/keys"

/*
 * Maximum Dilithium-3 secret key length.
 * Calibrated to OQS_SIG_dilithium_3_length_secret_key (4000 bytes).
 * Size stack / heap allocations for wallet_load_sk output buffers to this.
 * IMPORTANT: zero sk_out immediately after use to prevent key material leaks.
 */
#define WALLET_SK_LEN 4000

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
 * Return 1 if a public key file (.chain/keys/<id>.pk) exists, 0 otherwise.
 */
int wallet_exists(const char *id);

#endif /* WALLET_H */
