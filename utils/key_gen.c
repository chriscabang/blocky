/* keygen.c — Demo: generate a persistent Dilithium-3 keypair.
 *
 * Usage: keygen <basename>
 *
 * Writes two files:
 *   <basename>.pub   raw public key bytes  (1952 bytes, Dilithium-3)
 *   <basename>.key   raw private key bytes (4000 bytes, Dilithium-3)
 *
 * IMPORTANT: restrict the private key file immediately after generation:
 *   chmod 600 <basename>.key
 *
 * The public key is safe to share; load it into the sender / recipient
 * fields of a Transaction (see send_payment) or provide it to peers for
 * signature verification.
 *
 * Private key material is zeroed in memory before the process exits.
 */

#include <stdio.h>
#include <stdlib.h>

#include <oqs/oqs.h>

static int write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "error: cannot open %s for writing\n", path);
        return -1;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return (written == len) ? 0 : -1;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: keygen <basename>\n");
        return 1;
    }

    const char *base = argv[1];

    OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
    if (!sig) {
        fprintf(stderr, "error: OQS_SIG_new failed\n");
        return 1;
    }

    uint8_t *pub  = malloc(sig->length_public_key);
    uint8_t *priv = malloc(sig->length_secret_key);
    if (!pub || !priv) {
        fprintf(stderr, "error: out of memory\n");
        free(pub); free(priv);
        OQS_SIG_free(sig);
        return 1;
    }

    if (OQS_SIG_keypair(sig, pub, priv) != OQS_SUCCESS) {
        fprintf(stderr, "error: key generation failed\n");
        OQS_MEM_cleanse(priv, sig->length_secret_key);
        free(pub); free(priv);
        OQS_SIG_free(sig);
        return 1;
    }

    char pub_path[512], key_path[512];
    snprintf(pub_path, sizeof(pub_path), "%s.pub", base);
    snprintf(key_path, sizeof(key_path), "%s.key", base);

    int rc = 0;
    if (write_file(pub_path, pub,  sig->length_public_key) != 0 ||
        write_file(key_path, priv, sig->length_secret_key) != 0) {
        fprintf(stderr, "error: failed to write key files\n");
        rc = 1;
    } else {
        printf("[keygen] Public key  -> %s (%zu bytes)\n",
               pub_path, sig->length_public_key);
        printf("[keygen] Private key -> %s (%zu bytes)\n",
               key_path, sig->length_secret_key);
        printf("[keygen] IMPORTANT: chmod 600 %s\n", key_path);
    }

    OQS_MEM_cleanse(priv, sig->length_secret_key);
    free(pub);
    free(priv);
    OQS_SIG_free(sig);
    return rc;
}
