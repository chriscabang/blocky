/**
 * @file chain.c
 * @brief In-memory blockchain with a fixed pool allocator.
 */

#include "chain.h"
#include "block.h"
#include "consensus.h"
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

/* ── public API ───────────────────────────────────────────────────────── */

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

void chain_info(const Chain *c) {
  if (!c || !c->head) {
    log_error("chain_info: no chain loaded");
    return;
  }
  log_info("Chain tip: index=%u hash=%.16s...", c->head->index,
           (char *)c->head->hash);
}

int chain_show(const Chain *c, const char *hash) {
  (void)c;
  if (!hash || hash[0] == '\0') {
    log_error("chain_show: empty hash");
    return EXIT_FAILURE;
  }

  Block *b = storage_read(hash);
  if (!b) return EXIT_FAILURE;

  log_info("Block %u | hash=%.16s... | prev=%.16s... | ts=%ld | txns=%u",
           b->index, (char *)b->hash, (char *)b->previous_hash,
           (long)b->timestamp, b->transaction_count);

  block_free(b);
  return EXIT_SUCCESS;
}

void chain_list(const Chain *c, unsigned int blocks_per_page) {
  (void)c;
  if (blocks_per_page == 0) blocks_per_page = 10;

  unsigned int count = blocks_per_page;
  char **hashes = storage_scan(0, &count);
  if (!hashes) {
    log_debug("chain_list: chain is empty");
    return;
  }

  for (unsigned int i = 0; i < count; i++) {
    Block *b = storage_read(hashes[i]);
    if (b) {
      log_info("  [%u] %.16s...", b->index, hashes[i]);
      block_free(b);
    }
    free(hashes[i]);
  }
  free(hashes);
}
