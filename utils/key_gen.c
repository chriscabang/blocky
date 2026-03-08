/* key_gen.c — Generate a Dilithium-3 keypair for a zuno identity.
 *
 * Usage: key_gen <id>
 *
 * Writes two files into the chain's key store:
 *   .chain/keys/<id>.pk   public key  (1952 bytes, Dilithium-3)
 *   .chain/keys/<id>.sk   secret key  (4000 bytes, mode 0600)
 *
 * The directory .chain/keys/ is created if it does not exist.
 * Fails if either key file already exists (no silent overwrite).
 * Private key material is zeroed in memory before the process exits.
 *
 * Run this once per identity before using 'zuno send':
 *   build/utils/key_gen alice
 *   zuno send --from alice --to bob --amount 10
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include <oqs/oqs.h>

#define KEYS_DIR ".chain/keys"

static int ensure_dirs(void)
{
    if (mkdir(".chain", 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "error: cannot create .chain: %s\n", strerror(errno));
        return -1;
    }
    if (mkdir(KEYS_DIR, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "error: cannot create %s: %s\n",
                KEYS_DIR, strerror(errno));
        return -1;
    }
    return 0;
}

static int write_file(const char *path, const uint8_t *data, size_t len,
                      mode_t mode)
{
    if (access(path, F_OK) == 0) {
        fprintf(stderr, "error: file already exists: %s\n", path);
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "error: cannot open %s for writing: %s\n",
                path, strerror(errno));
        return -1;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    if (written != len) {
        fprintf(stderr, "error: short write to %s\n", path);
        return -1;
    }
    if (chmod(path, mode) != 0)
        fprintf(stderr, "warn: chmod %o %s: %s\n", mode, path, strerror(errno));
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: key_gen <id>\n");
        return 1;
    }

    const char *id = argv[1];
    if (id[0] == '\0') {
        fprintf(stderr, "error: id must not be empty\n");
        return 1;
    }

    if (ensure_dirs() != 0)
        return 1;

    char pk_path[512], sk_path[512];
    snprintf(pk_path, sizeof(pk_path), "%s/%s.pk", KEYS_DIR, id);
    snprintf(sk_path, sizeof(sk_path), "%s/%s.sk", KEYS_DIR, id);

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

    int rc = 0;
    if (write_file(pk_path, pub,  sig->length_public_key, 0644) != 0 ||
        write_file(sk_path, priv, sig->length_secret_key, 0600) != 0) {
        rc = 1;
    } else {
        printf("Generated keypair for '%s'\n", id);
        printf("  public key : %s\n", pk_path);
        printf("  secret key : %s  (keep this private)\n", sk_path);
    }

    OQS_MEM_cleanse(priv, sig->length_secret_key);
    free(pub);
    free(priv);
    OQS_SIG_free(sig);
    return rc;
}
