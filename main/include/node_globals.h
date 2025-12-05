#pragma once

#include <stdint.h>
#include "hash_table.h"

#define BROADCAST_ID (0)
#define NO_ID (0)

typedef uint16_t ID;

typedef struct {
    ID   i_addr;
    char s_addr[6];
} Address;

extern Address g_address;
extern NodeEntry *current_node;
extern HashTable *g_msg_table;

uint16_t rand_id(void);
uint16_t rand_msg_id(void);