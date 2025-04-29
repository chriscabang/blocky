/* network.h */
#ifndef NETWORK_H
#define NETWORK_H

/*#include "block.h"*/

// Initialize the TLS network
int init_tls(void);

// Start the network server
void start_network_server(void);

/*// Broadcast block to the network (supports PoW & PoS)*/
/*void broadcast_block(Block *block);*/
/**/
/*// Receive and validate incoming blocks*/
/*void receive_block(Block *block);*/

#endif//NETWORK_H
