/**
 * @file chain.c
 * @brief In-memory blockchain with a fixed pool allocator.
 */

#include "chain.h"
#include "block.h"
#include "consensus.h"
#include "equivocation.h"
#include "network.h"
#include "storage.h"
#include "log.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── internal pool helpers ────────────────────────────────────────────── */

static Block *pool_alloc(Chain *c) {
    for (int i = 0; i < CHAIN_POOL_SIZE; i++) {
        if (!c->pool_used[i]) {
            c->pool_used[i] = 1;
            memset(&c->pool[i], 0, sizeof(Block));
            return &c->pool[i];
        }
    }
    return NULL; /* pool exhausted */
}

static void pool_free(Chain *c, Block *b) {
    int idx = (int)(b - c->pool);
    if (idx >= 0 && idx < CHAIN_POOL_SIZE)
        c->pool_used[idx] = 0;
}

/* ── GHOST fork-choice helpers ────────────────────────────────────────── */

/*
 * Lightweight per-block record used only during fork-choice computation.
 * Avoids keeping full Block structs (with transaction arrays) in memory while
 * scanning the entire object store.
 */
typedef struct {
    char    hash[HASH_SIZE];
    char    prev[HASH_SIZE];
    uint8_t consensus;
} GhostNode;

/*
 * Compute the subtree weight rooted at 'hash'.
 *
 * PoW (consensus == 0): every block contributes 1.
 * PoS (consensus == 1): every block contributes 1.
 *
 * Recursion depth equals the fork depth, not the chain length — safe in
 * practice because forks are shallow (typically 1-3 blocks).
 */
static uint64_t ghost_subtree_weight(const char *hash,
                                     const GhostNode *nodes, unsigned int n)
{
    uint64_t w = 1; /* self */
    for (unsigned int i = 0; i < n; i++) {
        if (strcmp(nodes[i].prev, hash) == 0)
            w += ghost_subtree_weight(nodes[i].hash, nodes, n);
    }
    return w;
}

/*
 * Walk the GHOST fork-choice rule from 'hash' toward a leaf, writing the
 * canonical tip hash into 'out' (must be >= HASH_SIZE bytes).
 *
 * At each step the child with the greatest subtree weight is chosen.
 * Tie-break: lexicographically smaller hash wins (deterministic).
 */
static void ghost_walk(const char *hash,
                       const GhostNode *nodes, unsigned int n,
                       char *out)
{
    const GhostNode *best   = NULL;
    uint64_t         best_w = 0;

    for (unsigned int i = 0; i < n; i++) {
        if (strcmp(nodes[i].prev, hash) != 0) continue;
        uint64_t w = ghost_subtree_weight(nodes[i].hash, nodes, n);
        if (w > best_w ||
            (w == best_w && best != NULL &&
             strcmp(nodes[i].hash, best->hash) < 0)) {
            best_w = w;
            best   = &nodes[i];
        }
    }

    if (!best) {
        /* No children — this node is the canonical tip */
        strncpy(out, hash, HASH_SIZE);
        out[HASH_SIZE - 1] = '\0';
        return;
    }

    ghost_walk(best->hash, nodes, n, out);
}

/* ── public API ───────────────────────────────────────────────────────── */

int chain_fork_choice(Chain *c) {
    if (!c) {
        log_error("chain_fork_choice: NULL chain");
        return EXIT_FAILURE;
    }

    unsigned int n = 0;
    char **hashes = storage_list_all(&n);
    if (!hashes || n == 0) {
        free(hashes);
        return EXIT_SUCCESS; /* empty store — nothing to do */
    }

    /* Build lightweight node array from full blocks */
    GhostNode *nodes = calloc(n, sizeof(GhostNode));
    if (!nodes) {
        for (unsigned int i = 0; i < n; i++) free(hashes[i]);
        free(hashes);
        return EXIT_FAILURE;
    }

    unsigned int valid = 0;
    for (unsigned int i = 0; i < n; i++) {
        Block *b = storage_read(hashes[i]);
        if (b) {
            memcpy(nodes[valid].hash, b->hash,          HASH_SIZE);
            memcpy(nodes[valid].prev, b->previous_hash, HASH_SIZE);
            nodes[valid].consensus = b->consensus;
            valid++;
            block_free(b);
        }
        free(hashes[i]);
    }
    free(hashes);

    if (valid == 0) { free(nodes); return EXIT_SUCCESS; }

    /* Find genesis: the block whose previous_hash is the sentinel "0\0" */
    const char *root = NULL;
    for (unsigned int i = 0; i < valid; i++) {
        if (nodes[i].prev[0] == GENESIS_PREVIOUS_HASH[0] &&
            nodes[i].prev[1] == '\0') {
            root = nodes[i].hash;
            break;
        }
    }
    if (!root) {
        log_error("chain_fork_choice: no genesis block found in object store");
        free(nodes);
        return EXIT_FAILURE;
    }

    /* Walk GHOST from genesis to the canonical tip */
    char canonical[HASH_SIZE];
    ghost_walk(root, nodes, valid, canonical);
    free(nodes);

    /* No reorg needed */
    if (strcmp(canonical, (char *)c->head->hash) == 0)
        return EXIT_SUCCESS;

    /* Reorg: load the canonical tip into a pool slot and update HEAD */
    log_info("chain_fork_choice: reorg to %.16s...", canonical);

    Block *slot = pool_alloc(c);
    if (!slot) {
        log_error("chain_fork_choice: pool exhausted during reorg");
        return EXIT_FAILURE;
    }
    if (storage_read_into(canonical, slot) != EXIT_SUCCESS) {
        pool_free(c, slot);
        return EXIT_FAILURE;
    }
    if (storage_checkout(canonical) != EXIT_SUCCESS) {
        pool_free(c, slot);
        return EXIT_FAILURE;
    }

    Block *old = c->head;
    c->head    = slot;
    pool_free(c, old);
    return EXIT_SUCCESS;
}

Chain *chain_load(void) {
    Chain *c = calloc(1, sizeof(Chain));
    if (!c) {
        log_error("chain_load: calloc failed");
        return NULL;
    }

    char head_hash[HASH_SIZE];
    if (storage_head(head_hash, sizeof(head_hash)) == EXIT_SUCCESS) {
        /* Existing chain: load tip block into pool */
        Block *slot = pool_alloc(c);
        if (!slot) { free(c); return NULL; }

        if (storage_read_into(head_hash, slot) != EXIT_SUCCESS) {
            log_error("chain_load: failed to read head block");
            free(c);
            return NULL;
        }
        c->head = slot;
        log_info("chain_load: loaded tip block %u", c->head->index);

        /* Resolve any stored forks to the canonical (heaviest) tip */
        if (chain_fork_choice(c) != EXIT_SUCCESS)
            log_warn("chain_load: fork_choice failed; keeping stored HEAD");
    } else {
        /* Empty chain: create and persist a genesis block */
        Block *slot = pool_alloc(c);
        if (!slot) { free(c); return NULL; }

        slot->index            = 0;
        slot->timestamp        = 788918400; /* 1994-12-22 00:00:00 UTC */
        slot->previous_hash[0] = GENESIS_PREVIOUS_HASH[0];
        slot->previous_hash[1] = '\0';
        slot->transaction_count = 0;
        slot->consensus        = 0;
        slot->nonce            = 0;
        slot->next             = NULL;

        if (block_compute_hash(slot) != EXIT_SUCCESS) {
            free(c);
            return NULL;
        }

        if (storage_insert(slot) != EXIT_SUCCESS ||
            storage_checkout((char *)slot->hash) != EXIT_SUCCESS) {
            log_error("chain_load: failed to persist genesis block");
            free(c);
            return NULL;
        }

        c->head = slot;
        log_info("chain_load: genesis block created");
    }

    return c;
}

void chain_unload(Chain *c) {
    free(c);
}

int chain_validate(const Chain *c, const Block *block) {
    if (!c || !block) {
        log_error("chain_validate: NULL argument");
        return EXIT_FAILURE;
    }

    /* Link check: block must extend the current head */
    if (memcmp(block->previous_hash, c->head->hash, HASH_SIZE) != 0) {
        log_error("chain_validate: previous_hash mismatch (block %u)", block->index);
        return EXIT_FAILURE;
    }

    /* Hash integrity */
    if (block_verify_hash(block) != EXIT_SUCCESS) {
        log_error("chain_validate: hash verification failed for block %u", block->index);
        return EXIT_FAILURE;
    }

    /* Transaction amounts must be non-zero (uint64_t cannot be negative) */
    for (uint32_t i = 0; i < block->transaction_count; i++) {
        if (block->transactions[i].amount == 0) {
            log_error("chain_validate: zero transaction amount in block %u", block->index);
            return EXIT_FAILURE;
        }
    }

    /* Consensus rules */
    if (verify_consensus(block) != EXIT_SUCCESS) {
        log_error("chain_validate: consensus check failed for block %u",
                  block->index);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int chain_add(Chain *c, const Block *block) {
    if (!c || !block) {
        log_error("chain_add: NULL argument");
        return EXIT_FAILURE;
    }

    if (chain_validate(c, block) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    Block *slot = pool_alloc(c);
    if (!slot) {
        log_error("chain_add: pool exhausted (CHAIN_POOL_SIZE=%d)", CHAIN_POOL_SIZE);
        return EXIT_FAILURE;
    }

    *slot       = *block;
    slot->next  = NULL; /* runtime-only — never persisted */

    if (storage_insert(slot) != EXIT_SUCCESS) {
        pool_free(c, slot);
        return EXIT_FAILURE;
    }

    if (storage_checkout((char *)slot->hash) != EXIT_SUCCESS) {
        pool_free(c, slot);
        return EXIT_FAILURE;
    }

    /* Record PoS slot commitment — prevents the same proposer from committing
     * a second block for this slot (equivocation). Non-fatal: the block is
     * already on disk; a failed write is logged but does not roll back. */
    if (slot->consensus == CONSENSUS_POS && slot->proposer_id[0] != '\0') {
        if (equivocation_record(slot->proposer_id, slot->index) != EXIT_SUCCESS) {
            log_warn("chain_add: equivocation_record failed for '%s' slot %u",
                     slot->proposer_id, slot->index);
        }
    }

    /* Recycle the old head slot — it is now safely on disk */
    Block *old = c->head;
    c->head    = slot;
    pool_free(c, old);

    log_info("chain_add: block %u added", slot->index);
    return EXIT_SUCCESS;
}

int chain_propose(Chain *c, const Block *block) {
    if (!c || !block) {
        log_error("chain_propose: NULL argument");
        return EXIT_FAILURE;
    }

    FILE *f = fopen(".chain/peers", "r");
    if (!f) {
        log_info("chain_propose: no peers file (.chain/peers); nothing to broadcast");
        return EXIT_SUCCESS;
    }

    /*
     * Anonymous client TLS — no client certificate (mutual TLS not required).
     * CA verification uses the system store; nodes with self-signed certs should
     * install their CA into .chain/tls-ca.pem or the system CA bundle.
     * Set pqc_group to NET_PQC_GROUP once all peers load the OQS provider.
     */
    NetConfig cfg = {
        .cert_file = NULL,
        .key_file  = NULL,
        .ca_file   = NULL,
        .pqc_group = NULL,
        .port      = 0,
    };

    NetContext *ctx = net_context_client(&cfg);
    if (!ctx) {
        log_error("chain_propose: failed to create TLS client context");
        fclose(f);
        return EXIT_FAILURE;
    }

    char line[128];
    int  ok   = 0;
    int  fail = 0;

    while (fgets(line, (int)sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len == 0) continue;

        /* Parse "ip:port" — strrchr handles IPv6 addresses with embedded colons. */
        char *colon = strrchr(line, ':');
        if (!colon) {
            log_warn("chain_propose: malformed peer entry '%s' (expected ip:port)",
                     line);
            continue;
        }
        *colon = '\0';
        uint16_t port = (uint16_t)atoi(colon + 1);

        if (net_broadcast_block(ctx, block, line, port) == 0) {
            ok++;
        } else {
            log_warn("chain_propose: broadcast to %s:%u failed", line, port);
            fail++;
        }
    }

    fclose(f);
    net_context_free(ctx);

    log_info("chain_propose: block %u broadcast — ok=%d fail=%d",
             block->index, ok, fail);
    return EXIT_SUCCESS;
}
