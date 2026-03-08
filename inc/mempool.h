/* mempool.h — Local pending-transaction queue (signed Transaction objects). */
#ifndef MEMPOOL_H
#define MEMPOOL_H

#include "transaction.h"
#include <stdint.h>

#define MEMPOOL_DIR ".chain/mempool"

/*
 * Add a signed Transaction to the local mempool.
 *
 * Each entry is stored as a binary Transaction struct at
 * .chain/mempool/<txhash>, where txhash is SHA-256 of the canonical
 * message bytes (sender||recipient||amount||nonce — same layout as
 * build_message in transaction.c).
 *
 * Returns EXIT_SUCCESS on success, EXIT_FAILURE otherwise.
 */
int mempool_add(const Transaction *tx);

/*
 * Load up to 'max' pending transactions from .chain/mempool/ into 'out'.
 * Sets *count_out to the number of transactions loaded.
 * Returns EXIT_SUCCESS even if the mempool is empty.
 */
int mempool_load_all(Transaction *out, uint32_t max, uint32_t *count_out);

/*
 * Remove 'count' transactions from the mempool (call after mining a block).
 * Each entry is identified by recomputing its content hash.
 */
void mempool_purge(const Transaction *txns, uint32_t count);

/*
 * Return the number of pending transactions in the mempool.
 * Returns 0 if the mempool directory does not exist.
 */
uint32_t mempool_count(void);

#endif /* MEMPOOL_H */
