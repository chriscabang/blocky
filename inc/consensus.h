#ifndef CONSENSUS_H
#define CONSENSUS_H

#include "blockchain.h"
/*#include "pos.h"*/

#define SWITCH_INTERVAL 10 // Switch between PoW and PoS every 10 blocks

typedef enum { Work, Stake } ProofOf;
typedef int ConsensusType;

/**/
/*// Hybrid Consensus System*/
/*typedef struct {*/
/*  ConsensusType current_consensus;*/
/*  PoSSystem pos_system;*/
/*} Consensus;*/

/* Verifies a block against the hybrid consensus mechanism (PoW/PoS).
 * Returns 0 if the block meets consensus requirements, 1 otherwise.
 */
int verify_consensus(const Block *block);

/* Verifies that the block is signed by the branch owner.
 * Returns 0 if the block is signed by the branch owner, 1 otherwise.
 */
int verify_signature(const Block *block);

/*// Initialize the hybrid consensus system*/
/*void init_hybrid_consensus(Consensus *consensus);*/
/**/
/*// Determine which mechanism to use for the next block*/
/*ConsensusType get_consensus_for_block(int block_index);*/
/**/
/*// Verify a block using hybrid consensus*/
/*int verify_block_hybrid(Consensus *consensus, Block *block);*/

#endif // CONSENSUS_H
