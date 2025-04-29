#ifndef ITERATOR_H
#define ITERATOR_H

/**
 * @file iterator.h
 * @brief Implements an iterator for the blockchain
 */

#include "blockchain.h"

// Create iterator struct with a pointer to the current block
// and a pointer to the next block
typedef struct Iterator {
  Block *current;
  Block *next;
} Iterator;

/**
 * @brief Create a new iterator for the blockchain
 * @param chain The blockchain to iterate over
 * @return The new iterator
 */
Iterator* iterate(Block *chain);

/**
 * @brief Get the next block from the iterator
 * @param iter The iterator
 * @return The next block
 */
Block *next_block(Iterator *iter);

#endif//ITERATOR_H
