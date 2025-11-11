#pragma once

#include <stdint.h>
#include "hash_table.h"

typedef uint16_t ID;

typedef struct {
    ID   i_addr;
    char s_addr[6];
} Address;

extern Address g_address;
extern HashTable *g_msg_table;

uint16_t rand_id(void);
