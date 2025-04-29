/* pow.h */
#ifndef POW_H
#define POW_H

#include "blockchain.h"

// Proof of Work Difficulty Level
#define DIFFICULTY 4

// Perform Proof of Work mining
//void mine_block(Block *block);
void mine_block(Block *block);

// Validate block using PoW
int validate_block_pow(Block *block);

#endif//POW_H
