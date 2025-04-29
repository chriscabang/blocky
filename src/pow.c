/* pow.c */
#include "pow.h"
#include "crypto.h"

#include <openssl/sha.h>
#include <stdio.h>
#include <string.h>

// void mine_block(Block *block, uint32_t difficulty) {
//   char target[64] = {0};
//   memset(target, '0', difficulty);
//   target[difficulty] = '\0';
//
//   while (1) {
//     // compute_block_hash(block);
//     if (strncmp(block->hash, target, difficulty) == 0) {
//       printf("Block mined: %s\n", block->hash);
//       break;
//     }
//     block->nonce++;
//   }
// }

// Helper function to check if hash meets difficulty
/*static int meets_difficulty(const char *hash) {*/
/*  for (int i = 0; i < DIFFICULTY; i++) {*/
/*    if (hash[i] != '0')*/
/*      return 0;*/
/*  }*/
/*  return 1;*/
/*}*/

// Perform Proof of Work mining
/*void mine_block(Block* block) {*/
/*  block->nonce = 0;*/
/*  char hash[HASH_SIZE];*/
/**/
/*  do {*/
/*    block->nonce++;*/
/*    hash(block);*/
/*    memcpy(hash, block->hash, sizeof(block->hash));*/
/*  } while (!meets_difficulty(hash));*/
/**/
/*  memcpy(block->hash, hash, sizeof(hash));*/
/*  printf("Block %d mined with hash: %s (Nonce: %d)\n", block->index, hash,*/
/*         block->nonce);*/
/*}*/

// Validate block using PoW
/*int validate_block_pow(Block *block) {*/
/*  char hash[SHA256_DIGEST_LENGTH * 2 + 1];*/
/*  hash(block);*/
/*  memcpy(hash, block->hash, sizeof(block->hash));*/
/**/
/*  if (!meets_difficulty(hash)) {*/
/*    printf("PoW validation failed for Block %d: %s\n", block->index, hash);*/
/*    return 0;*/
/*  }*/
/**/
/*  printf("PoW validation successful for Block %d: %s\n", block->index, hash);*/
/*  return 1;*/
/*}*/
