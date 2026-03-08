/* main.c — Git-like CLI for zuno
 * Author: Chris Cabang <chriscabang@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "block.h"
#include "chain.h"
#include "consensus.h"
#include "crypto.h"
#include "mempool.h"
#include "pow.h"
#include "storage.h"
#include "transaction.h"
#include "key.h"

#define VERSION_STRING "zuno 0.1"

/* ── forward declarations ─────────────────────────────────────────────── */

static int cmd_init(int argc, char **argv);
static int cmd_status(int argc, char **argv);
static int cmd_log(int argc, char **argv);
static int cmd_show(int argc, char **argv);
static int cmd_cat(int argc, char **argv);
static int cmd_verify(int argc, char **argv);
static int cmd_send(int argc, char **argv);
static int cmd_mine(int argc, char **argv);
static int cmd_propose(int argc, char **argv);
static int cmd_version(int argc, char **argv);
static int cmd_help(int argc, char **argv);

/* ── dispatch table ───────────────────────────────────────────────────── */

typedef struct { const char *name; int (*fn)(int, char **); } Cmd;

static const Cmd CMDS[] = {
    {"init",    cmd_init},
    {"status",  cmd_status},
    {"log",     cmd_log},
    {"show",    cmd_show},
    {"cat",     cmd_cat},
    {"verify",  cmd_verify},
    {"send",    cmd_send},
    {"mine",    cmd_mine},
    {"propose", cmd_propose},
    {"version", cmd_version},
    {"help",    cmd_help},
};

static const int NCMDS = (int)(sizeof(CMDS) / sizeof(CMDS[0]));

/* ── command implementations ──────────────────────────────────────────── */

static int cmd_init(int argc, char **argv)
{
    (void)argc; (void)argv;
    Chain *c = chain_load();
    if (!c) {
        fprintf(stderr, "error: failed to initialise chain\n");
        return 2;
    }
    printf("Initialised chain at .chain/\n");
    printf("Tip: block #%u (%s)\n", c->head->index, (char *)c->head->hash);
    chain_unload(c);
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    Chain *c = chain_load();
    if (!c) {
        fprintf(stderr, "error: failed to load chain\n");
        return 2;
    }
    printf("Chain tip:  block #%u (%.16s...)\n",
           c->head->index, (char *)c->head->hash);
    chain_unload(c);

    uint32_t n = mempool_count();
    if (n == 0)
        printf("Mempool:    empty\n");
    else
        printf("Mempool:    %u transaction(s) pending\n", n);
    return 0;
}

static int cmd_log(int argc, char **argv)
{
    unsigned int limit = 10;
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], "--limit") == 0) {
            int v = atoi(argv[i + 1]);
            if (v <= 0) {
                fprintf(stderr, "error: --limit must be a positive integer\n");
                return 1;
            }
            limit = (unsigned int)v;
            i++;
        }
    }

    unsigned int count = limit;
    char **hashes = storage_scan(0, &count);
    if (!hashes || count == 0) {
        printf("(chain is empty)\n");
        return 0;
    }

    for (unsigned int i = 0; i < count; i++) {
        Block *b = storage_read(hashes[i]);
        if (b) {
            printf("block #%-4u  %s  ts=%ld  txns=%u\n",
                   b->index, hashes[i], (long)b->timestamp,
                   b->transaction_count);
            block_free(b);
        }
        free(hashes[i]);
    }
    free(hashes);
    return 0;
}

static int cmd_show(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: zuno show <hash>\n");
        return 1;
    }
    Block *b = storage_read(argv[2]);
    if (!b) {
        fprintf(stderr, "error: block not found: %s\n", argv[2]);
        return 2;
    }
    printf("block #%u\n", b->index);
    printf("  hash:      %s\n", (char *)b->hash);
    printf("  prev:      %s\n", (char *)b->previous_hash);
    printf("  merkle:    %s\n", (char *)b->merkle_root);
    printf("  timestamp: %ld\n", (long)b->timestamp);
    printf("  nonce:     %u\n", b->nonce);
    printf("  consensus: %u\n", (unsigned)b->consensus);
    printf("  txns:      %u\n", b->transaction_count);
    for (uint32_t i = 0; i < b->transaction_count; i++) {
        printf("    [%u] %s -> %s  %.6f\n", i,
               b->transactions[i].sender,
               b->transactions[i].recipient,
               (double)b->transactions[i].amount / (double)MICRO_PER_TOKEN);
    }
    free(b);
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: zuno cat <hash>\n");
        return 1;
    }
    Block *b = storage_read(argv[2]);
    if (!b) {
        fprintf(stderr, "error: block not found: %s\n", argv[2]);
        return 2;
    }
    printf("index:     %u\n",  b->index);
    printf("hash:      %s\n",  (char *)b->hash);
    printf("prev:      %s\n",  (char *)b->previous_hash);
    printf("merkle:    %s\n",  (char *)b->merkle_root);
    printf("timestamp: %ld\n", (long)b->timestamp);
    printf("nonce:     %u\n",  b->nonce);
    printf("consensus: %u (%s)\n", (unsigned)b->consensus,
           b->consensus == 0 ? "PoW" : "PoS");
    printf("txns:      %u\n",  b->transaction_count);
    for (uint32_t i = 0; i < b->transaction_count; i++) {
        printf("  [%u] %s -> %s  %.6f\n", i,
               b->transactions[i].sender,
               b->transactions[i].recipient,
               (double)b->transactions[i].amount / (double)MICRO_PER_TOKEN);
    }
    free(b);
    return 0;
}

static int cmd_verify(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: zuno verify <hash>\n");
        return 1;
    }
    Block *b = storage_read(argv[2]);
    if (!b) {
        fprintf(stderr, "error: block not found: %s\n", argv[2]);
        return 2;
    }
    int rc = block_verify_hash(b);
    free(b);
    if (rc == EXIT_SUCCESS) {
        printf("OK    %s\n", argv[2]);
        return 0;
    }
    printf("FAIL  %s\n", argv[2]);
    return 2;
}

/*
 * send — sign a transaction with the sender's Dilithium-3 key and add it
 *        to the local mempool (.chain/mempool/).
 *
 *   zuno send --from <sender> --to <recipient> --amount <value>
 *
 * The sender must have a keypair registered with 'keygen' first.
 * The signed transaction is stored in the mempool until a miner picks it
 * up with 'mine'.  In a multi-node deployment, the mempool entry would be
 * broadcast to peers (ADR-019 — not yet implemented).
 */
static int cmd_send(int argc, char **argv)
{
    const char *from   = NULL;
    const char *to     = NULL;
    const char *amount = NULL;

    for (int i = 2; i < argc - 1; i++) {
        if      (strcmp(argv[i], "--from")   == 0) { from   = argv[++i]; }
        else if (strcmp(argv[i], "--to")     == 0) { to     = argv[++i]; }
        else if (strcmp(argv[i], "--amount") == 0) { amount = argv[++i]; }
    }

    if (!from || !to || !amount) {
        fprintf(stderr,
                "usage: zuno send --from <sender> --to <recipient>"
                " --amount <value>\n");
        return 1;
    }

    /* Amount validation */
    double amt_d = atof(amount);
    uint64_t amt_u = (uint64_t)(amt_d * (double)MICRO_PER_TOKEN + 0.5);
    if (amt_u == 0) {
        fprintf(stderr, "error: amount must be positive\n");
        return 1;
    }

    /* Ensure .chain/ directory exists */
    Chain *c = chain_load();
    if (!c) {
        fprintf(stderr, "error: failed to load chain\n");
        return 2;
    }
    chain_unload(c);

    /* Check sender has a key */
    if (!key_exists(from)) {
        fprintf(stderr,
                "error: no key found for '%s'\n"
                "       run: build/utils/key_gen %s\n", from, from);
        return 1;
    }

    if (mempool_count() >= (uint32_t)MAX_TRANSACTIONS) {
        fprintf(stderr,
                "error: mempool full (%d transactions). mine a block first.\n",
                MAX_TRANSACTIONS);
        return 1;
    }

    /* Build the transaction */
    Transaction tx;
    memset(&tx, 0, sizeof tx);
    strncpy(tx.sender,    from, sizeof(tx.sender)    - 1);
    strncpy(tx.recipient, to,   sizeof(tx.recipient) - 1);
    tx.amount = amt_u;
    tx.nonce  = (uint64_t)time(NULL); /* monotonically increasing replay guard */

    /* Sign with sender's private key */
    uint8_t sk[KEY_SK_LEN];
    if (key_load_sk(from, sk, KEY_SK_LEN) != EXIT_SUCCESS) {
        fprintf(stderr, "error: cannot load secret key for '%s'\n", from);
        return 2;
    }

    int rc = sign_transaction(&tx, sk);
    memset(sk, 0, KEY_SK_LEN); /* zero key material immediately */

    if (rc != EXIT_SUCCESS) {
        fprintf(stderr, "error: failed to sign transaction\n");
        return 2;
    }

    if (mempool_add(&tx) != EXIT_SUCCESS) {
        fprintf(stderr, "error: failed to add transaction to mempool\n");
        return 2;
    }

    printf("Sent: %s -> %s  %.6f  (queued in mempool)\n",
           from, to, (double)amt_u / (double)MICRO_PER_TOKEN);
    return 0;
}

/*
 * mine — collect pending transactions from the mempool, verify their
 *        Dilithium-3 signatures, build a PoW block, and append to the chain.
 *
 *   zuno mine
 *
 * Only transactions whose sender key is present in .chain/keys/ can be
 * verified.  Unverifiable transactions are skipped with a warning.
 * In a multi-node deployment, each node would receive sender public keys
 * alongside the transactions (ADR-019 — not yet implemented).
 *
 * After a block is successfully added to the chain the mined transactions
 * are removed from the mempool.
 */
static int cmd_mine(int argc, char **argv)
{
    (void)argc; (void)argv;

    Chain *c = chain_load();
    if (!c) {
        fprintf(stderr, "error: failed to load chain\n");
        return 2;
    }

    /* Load all pending transactions from the mempool */
    Transaction pending[MAX_TRANSACTIONS];
    uint32_t pending_count = 0;
    if (mempool_load_all(pending, MAX_TRANSACTIONS, &pending_count) != EXIT_SUCCESS) {
        fprintf(stderr, "error: failed to read mempool\n");
        chain_unload(c);
        return 2;
    }

    if (pending_count == 0) {
        printf("nothing to mine\n");
        chain_unload(c);
        return 1;
    }

    /*
     * Verify each transaction's Dilithium-3 signature against the sender's
     * public key.  Only verified transactions are included in the block.
     * This is the key correctness guarantee: a miner cannot forge or tamper
     * with a transaction because they do not hold the sender's private key.
     */
    Transaction verified[MAX_TRANSACTIONS];
    uint32_t verified_count = 0;

    for (uint32_t i = 0; i < pending_count; i++) {
        const char *sender = pending[i].sender;

        uint8_t pk[MAX_PUBLIC_KEY_LENGTH];
        if (key_load_pk(sender, pk, MAX_PUBLIC_KEY_LENGTH) != EXIT_SUCCESS) {
            fprintf(stderr, "warn: no public key for '%s' — skipping tx\n",
                    sender);
            continue;
        }

        if (verify_transaction(&pending[i], pk) != EXIT_SUCCESS) {
            fprintf(stderr, "warn: invalid signature from '%s' — skipping tx\n",
                    sender);
            continue;
        }

        verified[verified_count++] = pending[i];
    }

    if (verified_count == 0) {
        printf("nothing to mine (all %u transaction(s) failed verification)\n",
               pending_count);
        chain_unload(c);
        return 1;
    }

    /* Build the block */
    Block *b = block_create(c->head->index + 1, c->head->hash);
    if (!b) {
        fprintf(stderr, "error: block_create failed\n");
        chain_unload(c);
        return 2;
    }

    for (uint32_t i = 0; i < verified_count; i++)
        b->transactions[i] = verified[i];
    b->transaction_count = verified_count;

    compute_merkle_root(b, (char *)b->merkle_root);

    b->consensus = CONSENSUS_POW;
    if (mine_block(b, DIFFICULTY) != EXIT_SUCCESS) {
        fprintf(stderr, "error: failed to mine block (nonce exhausted)\n");
        block_free(b);
        chain_unload(c);
        return 2;
    }

    if (chain_add(c, b) != EXIT_SUCCESS) {
        fprintf(stderr, "error: chain_add failed\n");
        block_free(b);
        chain_unload(c);
        return 2;
    }

    printf("Mined block #%u (%s)  [%u tx]\n",
           b->index, (char *)b->hash, verified_count);

    /* Remove the mined transactions from the mempool */
    mempool_purge(verified, verified_count);

    block_free(b);
    chain_unload(c);
    return 0;
}

/* Count non-empty lines in .chain/peers (one "ip:port" entry per line). */
static int count_peers(void)
{
    FILE *f = fopen(".chain/peers", "r");
    if (!f) return 0;
    int n = 0;
    char line[128];
    while (fgets(line, (int)sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'
                           || line[len-1] == ' ' || line[len-1] == '\t'))
            len--;
        if (len > 0) n++;
    }
    fclose(f);
    return n;
}

static int cmd_propose(int argc, char **argv)
{
    (void)argc; (void)argv;

    Chain *c = chain_load();
    if (!c) {
        fprintf(stderr, "error: failed to load chain\n");
        return 2;
    }

    int peers = count_peers();
    if (peers == 0) {
        printf("Proposed block #%u (%.16s...)  [no peers configured]\n",
               c->head->index, (char *)c->head->hash);
        chain_unload(c);
        return 0;
    }

    int rc = chain_propose(c, c->head);
    if (rc != EXIT_SUCCESS) {
        fprintf(stderr, "error: chain_propose failed\n");
    } else {
        printf("Proposed block #%u (%.16s...)  [broadcast to %d peer(s)]\n",
               c->head->index, (char *)c->head->hash, peers);
    }

    chain_unload(c);
    return (rc == EXIT_SUCCESS) ? 0 : 2;
}

static int cmd_version(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("%s\n", VERSION_STRING);
    return 0;
}

static const char *USAGE =
    "Usage: zuno <command> [options]\n"
    "\n"
    "Commands:\n"
    "  init                             Initialise chain (creates genesis block)\n"
    "  status                           Show chain tip and mempool\n"
    "  log [--limit N]                  List recent blocks (default 10)\n"
    "  show <hash>                      Show block details\n"
    "  cat  <hash>                      Raw field dump of a block\n"
    "  verify <hash>                    Verify a block's hash integrity\n"
    "  send --from <s> --to <r> --amount <a>\n"
    "                                   Sign and queue a transaction (mempool)\n"
    "  mine                             Build a PoW block from mempool transactions\n"
    "  propose                          Broadcast tip to all configured peers\n"
    "  version                          Print version\n"
    "  help [command]                   Show this help or per-command help\n";

static int cmd_help(int argc, char **argv)
{
    if (argc >= 3) {
        const char *sub = argv[2];
        if (strcmp(sub, "send") == 0)
            printf("send --from <sender> --to <recipient> --amount <value>\n"
                   "  Sign a transaction with the sender's private key and\n"
                   "  add it to the local mempool. Mine with 'mine'.\n");
        else if (strcmp(sub, "mine") == 0)
            printf("mine\n"
                   "  Verify mempool transactions, build a PoW block, and\n"
                   "  append it to the chain. Clears mined txs from mempool.\n");
        else if (strcmp(sub, "log") == 0)
            printf("log [--limit N]\n"
                   "  List recent blocks. Default limit is 10.\n");
        else if (strcmp(sub, "show") == 0)
            printf("show <hash>\n"
                   "  Print block details for the given hash.\n");
        else if (strcmp(sub, "cat") == 0)
            printf("cat <hash>\n"
                   "  Raw field dump of the block identified by hash.\n");
        else if (strcmp(sub, "verify") == 0)
            printf("verify <hash>\n"
                   "  Verify the hash integrity of a stored block.\n");
        else
            printf("No help available for '%s'.\n", sub);
        return 0;
    }
    printf("%s", USAGE);
    return 0;
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "%s", USAGE);
        return 1;
    }

    for (int i = 0; i < NCMDS; i++) {
        if (strcmp(CMDS[i].name, argv[1]) == 0)
            return CMDS[i].fn(argc, argv);
    }

    fprintf(stderr, "error: unknown command '%s'\n", argv[1]);
    fprintf(stderr, "Run 'zuno help' for usage.\n");
    return 1;
}
