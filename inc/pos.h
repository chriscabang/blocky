#ifndef POS_H
#define POS_H

#include "blockchain.h"

#define MAX_VALIDATORS 100

typedef struct {
  int id;
  uint64_t stake; // Amount of stake
} Validator;

typedef struct {
  Validator validators[MAX_VALIDATORS];
  int validator_count;
} PoSSystem;

// Initialize the PoS system
void init_pos(PoSSystem *pos_system);

// Add stake for a validator
void stake_coins(PoSSystem *pos_system, int validator_id, uint64_t amount);

// Select a validator for the next block
Validator select_validator(PoSSystem *pos_system);

// Validate a block using PoS
int validate_block_pos(Block *block, Validator validator);

#endif // POS_H
