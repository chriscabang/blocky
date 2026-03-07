/**
 * @file chain.h
 * @brief In-memory blockchain with a fixed pool allocator.
 *
 * The Chain struct owns a pre-allocated pool of Block slots. Only the current
 * chain tip lives in the pool at steady state; old slots are recycled when a
 * new block is added. This eliminates per-block malloc after chain_load().
 *
 * On-disk persistence is delegated entirely to storage.h.
 */

#ifndef CHAIN_H
#define CHAIN_H

#include "block.h"

#include <stdint.h>

#define CHAIN_POOL_SIZE 64

typedef struct {
  Block   *head;                      /* current in-memory chain tip */
  Block    pool[CHAIN_POOL_SIZE];     /* pre-allocated block storage */
  uint8_t  pool_used[CHAIN_POOL_SIZE];/* 1 = slot occupied, 0 = free */
} Chain;

/**
 * @brief Load the blockchain from disk into memory.
 *
 * If storage has an existing HEAD the tip block is loaded into the pool.
 * If the chain is empty a genesis block is created, persisted, and checked out.
 * Caller must release with chain_unload().
 *
 * @return Heap-allocated Chain, or NULL on allocation failure.
 */
Chain *chain_load(void);

/**
 * @brief Release a Chain and all associated resources.
 * Safe to call with NULL.
 */
void chain_unload(Chain *c);

/**
 * @brief Validate an incoming block against the current chain tip.
 *
 * Checks:
 *   - previous_hash links to c->head->hash
 *   - block hash is internally consistent (block_verify_hash)
 *   - transaction amounts are positive
 *   - consensus field is 0 or 1
 *
 * @return EXIT_SUCCESS if valid, EXIT_FAILURE otherwise.
 */
int chain_validate(const Chain *c, const Block *block);

/**
 * @brief Validate and append a block to the chain.
 *
 * Validates the block, copies it into a pool slot, persists it to disk,
 * advances HEAD, and updates the in-memory tip. The old head pool slot is
 * recycled.
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int chain_add(Chain *c, const Block *block);

/**
 * @brief Broadcast a block to all peers listed in .chain/peers.
 *
 * Reads one "ip:port" entry per line from .chain/peers.  For each peer a
 * TLS client connection is established and the block header is sent via
 * net_broadcast_block.  Returns EXIT_SUCCESS even if some peers are
 * unreachable (partial broadcast is normal in a P2P network).
 * Returns EXIT_SUCCESS with no action if .chain/peers does not exist.
 * Returns EXIT_FAILURE only on NULL arguments or TLS context failure.
 */
int chain_propose(Chain *c, const Block *block);

/**
 * @brief Log info about the current chain tip.
 */
void chain_info(const Chain *c);

/**
 * @brief Log details for the block identified by hash.
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int chain_show(const Chain *c, const char *hash);

/**
 * @brief List blocks starting from HEAD, up to blocks_per_page entries.
 */
void chain_list(const Chain *c, unsigned int blocks_per_page);

#endif /* CHAIN_H */
