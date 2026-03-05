/* main.c — Git-like CLI for blocky / QuteChain
 * Author: Chris Cabang <chriscabang@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "block.h"
#include "chain.h"
#include "crypto.h"
#include "storage.h"
#include "transaction.h"

#define VERSION_STRING "blocky 0.1"
#define STAGED_PATH    ".chain/STAGED"

/* ── forward declarations ─────────────────────────────────────────────── */

static int cmd_init(int argc, char **argv);
static int cmd_status(int argc, char **argv);
static int cmd_log(int argc, char **argv);
static int cmd_show(int argc, char **argv);
static int cmd_cat(int argc, char **argv);
static int cmd_verify(int argc, char **argv);
static int cmd_send(int argc, char **argv);
static int cmd_commit(int argc, char **argv);
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
    {"commit",  cmd_commit},
    {"propose", cmd_propose},
    {"version", cmd_version},
    {"help",    cmd_help},
};

static const int NCMDS = (int)(sizeof(CMDS) / sizeof(CMDS[0]));

/* ── helpers ──────────────────────────────────────────────────────────── */

/* Count newline-terminated lines in STAGED; returns 0 if file is absent. */
static int staged_count(void)
{
    FILE *f = fopen(STAGED_PATH, "r");
    if (!f) return 0;
    int n = 0, c;
    while ((c = fgetc(f)) != EOF)
        if (c == '\n') n++;
    fclose(f);
    return n;
}

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

    int n = staged_count();
    if (n == 0)
        printf("Staged:     nothing staged\n");
    else
        printf("Staged:     %d transaction(s) pending\n", n);
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
            free(b);
        }
        free(hashes[i]);
    }
    free(hashes);
    return 0;
}

static int cmd_show(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: blocky show <hash>\n");
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
               b->transactions[i].amount);
    }
    free(b);
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: blocky cat <hash>\n");
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
        printf("  [%u] %s -> %s  %f\n", i,
               b->transactions[i].sender,
               b->transactions[i].recipient,
               b->transactions[i].amount);
    }
    free(b);
    return 0;
}

static int cmd_verify(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: blocky verify <hash>\n");
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
                "usage: blocky send --from <sender> --to <recipient>"
                " --amount <value>\n");
        return 1;
    }

    double amt = atof(amount);
    if (amt <= 0.0) {
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

    int n = staged_count();
    if (n >= MAX_TRANSACTIONS) {
        fprintf(stderr,
                "error: staging area full (%d transactions). commit first.\n",
                MAX_TRANSACTIONS);
        return 1;
    }

    FILE *f = fopen(STAGED_PATH, "a");
    if (!f) {
        fprintf(stderr, "error: cannot open staging file\n");
        return 2;
    }
    fprintf(f, "%s\t%s\t%f\n", from, to, amt);
    fclose(f);

    printf("Staged: %s -> %s  %.6f\n", from, to, amt);
    return 0;
}

static int cmd_commit(int argc, char **argv)
{
    (void)argc; (void)argv;

    Chain *c = chain_load();
    if (!c) {
        fprintf(stderr, "error: failed to load chain\n");
        return 2;
    }

    FILE *f = fopen(STAGED_PATH, "r");
    if (!f) {
        printf("nothing to commit\n");
        chain_unload(c);
        return 1;
    }

    /* Parse staged transactions */
    Transaction txns[MAX_TRANSACTIONS];
    int count = 0;
    /* line buffer: sender + tab + recipient + tab + amount + newline */
    char line[2 * MAX_PUBLIC_KEY_LENGTH + 64];

    while (count < MAX_TRANSACTIONS && fgets(line, (int)sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';

        char *tab1 = strchr(line, '\t');
        if (!tab1) continue;
        *tab1 = '\0';
        char *tab2 = strchr(tab1 + 1, '\t');
        if (!tab2) continue;
        *tab2 = '\0';

        memset(&txns[count], 0, sizeof(Transaction));
        strncpy(txns[count].sender,    line,      sizeof(txns[count].sender)    - 1);
        strncpy(txns[count].recipient, tab1 + 1,  sizeof(txns[count].recipient) - 1);
        txns[count].amount = atof(tab2 + 1);
        count++;
    }
    fclose(f);

    if (count == 0) {
        printf("nothing to commit\n");
        chain_unload(c);
        return 1;
    }

    /* Build the new block */
    Block *b = block_create(c->head->index + 1, c->head->hash);
    if (!b) {
        fprintf(stderr, "error: block_create failed\n");
        chain_unload(c);
        return 2;
    }

    for (int i = 0; i < count; i++)
        b->transactions[i] = txns[i];
    b->transaction_count = (uint32_t)count;

    compute_merkle_root(b, (char *)b->merkle_root);

    if (block_compute_hash(b) != EXIT_SUCCESS) {
        fprintf(stderr, "error: failed to compute block hash\n");
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

    printf("Committed block #%u (%s)\n", b->index, (char *)b->hash);
    block_free(b);
    unlink(STAGED_PATH);
    chain_unload(c);
    return 0;
}

static int cmd_propose(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("propose: not yet implemented\n");
    return 0;
}

static int cmd_version(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("%s\n", VERSION_STRING);
    return 0;
}

static const char *USAGE =
    "Usage: blocky <command> [options]\n"
    "\n"
    "Commands:\n"
    "  init                             Initialise chain (creates genesis block)\n"
    "  status                           Show chain tip and staged transactions\n"
    "  log [--limit N]                  List recent blocks (default 10)\n"
    "  show <hash>                      Show block details\n"
    "  cat  <hash>                      Raw field dump of a block\n"
    "  verify <hash>                    Verify a block's hash integrity\n"
    "  send --from <s> --to <r> --amount <a>\n"
    "                                   Stage a transaction\n"
    "  commit                           Build a block from staged transactions\n"
    "  propose                          Broadcast tip to network (stub)\n"
    "  version                          Print version\n"
    "  help [command]                   Show this help or per-command help\n";

static int cmd_help(int argc, char **argv)
{
    if (argc >= 3) {
        const char *sub = argv[2];
        if (strcmp(sub, "send") == 0)
            printf("send --from <sender> --to <recipient> --amount <value>\n"
                   "  Stage one transaction. Commit with 'commit'.\n");
        else if (strcmp(sub, "commit") == 0)
            printf("commit\n"
                   "  Build a block from staged transactions and append to chain.\n");
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
    fprintf(stderr, "Run 'blocky help' for usage.\n");
    return 1;
}
