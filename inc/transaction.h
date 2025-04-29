/* transaction.h */
#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stdint.h>
#include <stddef.h>

#define MAX_SIGNATURE_LENGTH 512
#define MAX_PUBLIC_KEY_LENGTH 1024

typedef struct {
  char     sender[MAX_PUBLIC_KEY_LENGTH];
  char     recipient[MAX_PUBLIC_KEY_LENGTH];
  double   amount;
  uint8_t  signature[MAX_SIGNATURE_LENGTH];
  size_t   signature_length;
} Transaction;

/*:FIXME: Find another way than having the private key in plane*/
void sign_transaction(Transaction *tx, const uint8_t *private_key); //,
                      // uint32_t private_key_len);
int  verify_transaction(const Transaction *tx, const uint8_t *public_key); //,
                       // uint32_t public_key_len);

#endif//TRANSACTION_H
