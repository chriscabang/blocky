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
 * @brief Select the canonical chain tip using the GHOST fork-choice rule.
 *
 * Loads every block in the object store, builds the fork graph, and walks
 * it greedily from the genesis block — at each fork point choosing the child
 * whose subtree has the greatest total weight:
 *
 *   PoW (consensus == 0): weight = 1 per block
 *   PoS (consensus == 1): stake-weighted via the validator registry (ADR-003)
 *
 * Tie-break: lexicographically smaller hash wins (deterministic).
 *
 * If the canonical tip differs from c->head the pool slot is updated and
 * storage_checkout() is called to persist the new HEAD.
 *
 * Called automatically by chain_load() to ensure the node always boots onto
 * the canonical branch, even after storing competing blocks from peers.
 *
 * @return EXIT_SUCCESS or EXIT_FAILURE.
 */
int chain_fork_choice(Chain *c);

#endif /* CHAIN_H */
