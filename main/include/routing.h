#ifndef _ROUTING_H_
#define _ROUTING_H_

#include <stddef.h>     // for size_t
#include "node_globals.h"

#define MAX_ROUTING_ENTRIES (4)

typedef struct node_table_entry NodeEntry;
typedef struct router_struct Router;

// public API
Router *create_router(ID for_node);
ID router_query_intermediate(Router *router, ID destination_node);
void router_update(Router *router, ID origin_node, ID destination_node, ID from_node, int steps);
void router_bad_intermediate(Router *router, ID intermediate_node);
void router_link_node(Router *router, ID node);
void router_unlink_node(Router *router, ID bad_node);
void router_answer_rquery(Router *router, NodeEntry *node_obj, int count, char *buffer, size_t buffer_size);
void router_parse_rquery(Router *router, ID from_node, char *buffer);

#endif
