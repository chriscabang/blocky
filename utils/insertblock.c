// Implementation of the insertblock function
// This function inserts a block into the blockchain
// The block is inserted at the end of the blockchain
// If there are no blocks in the blockchain, the block is inserted as the genesis block
// The function returns 0 if the block is successfully inserted
// Otherwise, the function returns 1
// The function prints the new block hash and the block index to stdout
// The function should not print anything else to stdout
// The block is saved to disk after insertion using the save function
// The block is saved to disk when this function returns 0
// If block insertion fails, the block is not saved to disk
// The main function accepts arguments from the command line
// The arguments are the sender, recipient, and amount of the transaction
// The main function should create a transaction from the arguments
// The main function should create a block from the transaction
// The main function argument also accepts json formatted transactions
// The main function should parse the json formatted transactions
// The main function should create a block from the parsed transactions

#include "block.h"
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int insertblock(const char *sender, const char *recipient, int amount) {
  Block *genesis = load();
  if (genesis == NULL) {
    genesis = genesis_block();
  }

  // Create a transaction from the arguments
  // Use memcpy instead of strcpy to avoid buffer overflow when copying strings
  // to the transaction struct
  Transaction transaction = {
      .amount = amount,
  };
  memcpy(transaction.sender, sender, strlen(sender));
  memcpy(transaction.recipient, recipient, strlen(recipient));
  Block *block = create_block(genesis, &transaction, 1);
  save(block);

  printf("Block %d mined with hash: %s\n", block->index, block->hash);

  destroy_block(genesis);
  destroy_block(block);

  return 0;
}

int main(int argc, char *argv[]) {
  if (argc != 4) {
    return 1;
  }

  // Print the arguments to stdout with a newline character and label each
  for (int i = 0; i < argc; i++) {
    printf("Argument %d: %s\n", i, argv[i]);
  }

  return insertblock(argv[1], argv[2], atoi(argv[3]));
}
