#include "pos.h"

#include <stdio.h>
#include <stdlib.h>

// Initialize the PoS system
void init_pos(PoSSystem *pos_system) { pos_system->validator_count = 0; }

// Add stake for a validator
void stake_coins(PoSSystem *pos_system, int validator_id, uint64_t amount) {
  for (int i = 0; i < pos_system->validator_count; i++) {
    if (pos_system->validators[i].id == validator_id) {
      pos_system->validators[i].stake += amount;
      return;
    }
  }

  if (pos_system->validator_count < MAX_VALIDATORS) {
    pos_system->validators[pos_system->validator_count].id = validator_id;
    pos_system->validators[pos_system->validator_count].stake = amount;
    pos_system->validator_count++;
  }
}

// Select a validator for the next block
Validator select_validator(PoSSystem *pos_system) {
  uint64_t total_stake = 0;
  for (int i = 0; i < pos_system->validator_count; i++) {
    total_stake += pos_system->validators[i].stake;
  }

  if (total_stake == 0) {
    fprintf(stderr, "No validators available.\n");
    exit(1);
  }

  uint64_t rand_val = rand() % total_stake;
  uint64_t running_total = 0;

  for (int i = 0; i < pos_system->validator_count; i++) {
    running_total += pos_system->validators[i].stake;
    if (rand_val < running_total) {
      return pos_system->validators[i];
    }
  }

  return pos_system->validators[pos_system->validator_count - 1]; // Fallback
}

// Validate a block using PoS
int validate_block_pos(Block *block, Validator validator) {
  // Basic validation check
  if (block->index % 10 == 0) { // Example: PoS validates every 10th block
    printf("Block %d validated by validator %d (Stake: %llu)\n", block->index,
           validator.id, (unsigned long long)validator.stake);
    return 1;
  }
  return 0;
}
