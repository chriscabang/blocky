/**
 * {
 *    "transactions": [
 *        {"sender": "Alice", "receiver": "Bob", "amount": 10.5},
 *        {"sender": "Charlie", "receiver": "David", "amount": 5.0}
 *    ]
 * }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blockchain.h"
#include "transaction.h"
#include "network.h"
#include "consensus.h"
#include "cJSON.h"

#define MAX_TRANSACTIONS 10
#define MAX_JSON_SIZE 1024

void parse_transactions_from_json(const char *json_input, Transaction transactions[], uint8_t *transaction_count) {
    cJSON *json = cJSON_Parse(json_input);
    if (!json) {
        printf("Invalid JSON format.\n");
        return;
    }

    cJSON *tx_array = cJSON_GetObjectItemCaseSensitive(json, "transactions");
    if (!cJSON_IsArray(tx_array)) {
        printf("JSON must contain a 'transactions' array.\n");
        cJSON_Delete(json);
        return;
    }

    *transaction_count = 0;
    cJSON *tx;
    cJSON_ArrayForEach(tx, tx_array) {
        if (*transaction_count >= MAX_TRANSACTIONS) break;

        cJSON *sender = cJSON_GetObjectItemCaseSensitive(tx, "sender");
        cJSON *receiver = cJSON_GetObjectItemCaseSensitive(tx, "receiver");
        cJSON *amount = cJSON_GetObjectItemCaseSensitive(tx, "amount");

        if (cJSON_IsString(sender) && cJSON_IsString(receiver) && cJSON_IsNumber(amount)) {
            strncpy(transactions[*transaction_count].sender, sender->valuestring, sizeof(transactions[*transaction_count].sender) - 1);
            strncpy(transactions[*transaction_count].receiver, receiver->valuestring, sizeof(transactions[*transaction_count].receiver) - 1);
            transactions[*transaction_count].amount = amount->valuedouble;
            (*transaction_count)++;
        }
    }
    cJSON_Delete(json);
}

int main() {
    Block *latest_block = get_latest_block();
    if (latest_block == NULL) {
        printf("Error: No existing blockchain found.\n");
        return 1;
    }

    char json_input[MAX_JSON_SIZE];
    printf("Enter transactions in JSON format:\n");
    fgets(json_input, MAX_JSON_SIZE, stdin);

    Transaction transactions[MAX_TRANSACTIONS];
    uint8_t transaction_count = 0;

    parse_transactions_from_json(json_input, transactions, &transaction_count);

    if (transaction_count == 0) {
        printf("No valid transactions found.\n");
        return 1;
    }

    // :TODO: Verify transaction

    Block *new_block = create_new_block(latest_block, transactions, transaction_count);
    if (new_block == NULL) {
        printf("Failed to create new block.\n");
        return 1;
    }

    if (!validate_block(new_block, latest_block)) {
        printf("Block validation failed.\n");
        return 1;
    }

    if (!broadcast_block(new_block)) {
        printf("Failed to broadcast new block.\n");
        return 1;
    }

    printf("Block successfully created and broadcasted!\n");
    return 0;
}
