/* startchain.c */
#include "blockchain.h"
/*#include "network.h"*/

int main(void) {
  Blockchain blockchain;
  load_blockchain(&blockchain);
  start_network_server();

  return 0;
}
