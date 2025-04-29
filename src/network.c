/* network.c */
// Networking implementation using TLS with post-quantum cryptography. Handles
// the TLS initialization and block broadcasting.

#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <oqs/oqs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "network.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 4433

SSL_CTX *ctx;

// Initialize TLS for secure communication
int init_tls(void) {
  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();
  ctx = SSL_CTX_new(TLS_server_method());

  if (!ctx) {
    fprintf(stderr, "Error initializing TLS context\n");
    return -1;
  }
  return 0;
}

void start_network_server(void) {
  SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
  SSL *ssl;
  int server_sock, client_sock;
  struct sockaddr_in server_addr, client_addr;

  socklen_t client_len = sizeof(client_addr);
  char buffer[256];

  if (!ctx) {
    fprintf(stderr, "Error creating SSL server context\n");
    exit(EXIT_FAILURE);
  }

  // Enable PQC key exchange (Kyber)
  if (SSL_CTX_set1_groups_list(ctx, "p256_kyber768") != 1) {
    fprintf(stderr, "Error setting PQC key exchange for server\n");
    exit(EXIT_FAILURE);
  }

  // Create server socket
  server_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (server_sock < 0) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(SERVER_PORT);

  if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("Bind failed");
    exit(EXIT_FAILURE);
  }

  listen(server_sock, 5);
  printf("Server listening on port %d...\n", SERVER_PORT);

  while (1) {
    client_sock =
        accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
    if (client_sock < 0) {
      perror("Accept failed");
      continue;
    }

    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, client_sock);

    if (SSL_accept(ssl) <= 0) {
      fprintf(stderr, "SSL handshake failed\n");
      SSL_free(ssl);
      close(client_sock);
      continue;
    }

    // Receive block data
    memset(buffer, 0, sizeof(buffer));

    SSL_read(ssl, buffer, sizeof(buffer) - 1);

    printf("Received block data: %s\n", buffer);

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client_sock);
  }

  close(server_sock);
  SSL_CTX_free(ctx);
}

// Initializes OpenSSL with liboqs post-quantum support
SSL_CTX *create_client_context(void) {
  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
  if (!ctx) {
    fprintf(stderr, "Error creating SSL context\n");
    exit(EXIT_FAILURE);
  }

  // Enable post-quantum key exchange (Kyber)
  if (SSL_CTX_set1_groups_list(ctx, "p256_kyber768") != 1) {
    fprintf(stderr, "Error setting PQC key exchange\n");
    exit(EXIT_FAILURE);
  }

  return ctx;
}

// Converts a block into a JSON-like string for transmission
/*void serialize_block(Block* block, char *output, size_t size) {*/
/*  snprintf(output, size, "{\"index\":%u,\"prev_hash\":\"%s\",\"hash\":\"%s\"}",*/
/*           block->index, block->previous_hash, block->hash);*/
/*}*/

// Sends a block over a secure TLS channel using PQC
/*void broadcast_block(Block *block) {*/
/*  SSL_CTX *ctx = create_client_context();*/
/*  SSL *ssl;*/
/*  int sock;*/
/*  struct sockaddr_in server_addr;*/
/**/
/*  // Create socket*/
/*  sock = socket(AF_INET, SOCK_STREAM, 0);*/
/*  if (sock < 0) {*/
/*    perror("Socket creation failed");*/
/*    exit(EXIT_FAILURE);*/
/*  }*/
/**/
/*  // Define server address*/
/*  server_addr.sin_family = AF_INET;*/
/*  server_addr.sin_port = htons(SERVER_PORT);*/
/*  inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);*/
/**/
/*  // Connect to server*/
/*  if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {*/
/*    perror("Connection failed");*/
/*    close(sock);*/
/*    return;*/
/*  }*/
/**/
/*  // Create SSL structure and associate with socket*/
/*  ssl = SSL_new(ctx);*/
/*  SSL_set_fd(ssl, sock);*/
/**/
/*  if (SSL_connect(ssl) != 1) {*/
/*    fprintf(stderr, "SSL connection failed\n");*/
/*    SSL_free(ssl);*/
/*    close(sock);*/
/*    return;*/
/*  }*/
/**/
/*  // Serialize and send block*/
/*  char block_data[256];*/
/*  serialize_block(block, block_data, sizeof(block_data));*/
/*  SSL_write(ssl, block_data, strlen(block_data));*/
/**/
/*  printf("Block %u broadcasted successfully!\n", block->index);*/
/**/
/*  // Cleanup*/
/*  SSL_shutdown(ssl);*/
/*  SSL_free(ssl);*/
/*  close(sock);*/
/*  SSL_CTX_free(ctx);*/
/*}*/
