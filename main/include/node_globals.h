#pragma once

#include <stdint.h>


typedef struct node_table_entry  NodeEntry;
typedef struct router_struct     Router;
typedef struct hash_table_struct HashTable;

#define BROADCAST_ID (0)
#define NO_ID (0)

typedef uint16_t ID;

// typedef struct {
//     ID   i_addr;
//     char s_addr[6];
// } Address;

// extern Address g_address;
extern HashTable *g_msg_table;
extern ID         g_my_address;
extern NodeEntry *g_this_node;
extern Router    *g_router;

ID rand_id(void);
ID rand_msg_id(void);