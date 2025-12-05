#include "routing.h"
#include "node_table.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
	int steps;
	ID intermediate_node;
	bool in_use;
	bool link_active;
} IntermediateStepInfo;

typedef struct destination_approximator_struct {
	IntermediateStepInfo best_routing_info[MAX_ROUTING_ENTRIES];
	ID destination_node;
	int count;
	uint32_t last_updated_seq;

	struct destination_approximator_struct *next;
} DestinationApproximator;


typedef struct router_struct {
	DestinationApproximator *destination_list;
	int approximators;
	ID node_id;
	uint32_t discovery_seq;
} Router;


// hidden
static DestinationApproximator *create_destination_approximator(Router *router, ID destination_node);
static DestinationApproximator *get_destination_approximator(Router *router, ID destination_node);
static bool update_approximation_entry(Router *router, DestinationApproximator *approximator, ID intermediate_node, int steps);
static bool remove_approximation_entry(DestinationApproximator *approximator, ID intermediate_node);
static IntermediateStepInfo *choose_approximation_route(DestinationApproximator *approximator);
static void router_incorporate_rquery(Router *router, ID from_node, ID destination_node, int steps);


Router *create_router(ID for_node) {
	Router *new_router = malloc(sizeof(Router));
	new_router->node_id = for_node;
	new_router->approximators = 0;
	new_router->destination_list = NULL;

	return new_router;
}

static DestinationApproximator *create_destination_approximator(Router *router, ID destination_node) {
	// attempt to find first
	DestinationApproximator *approx = router->destination_list;
	while (approx) {
		if (approx->destination_node == destination_node) break;
		approx = approx->next;
	}
	if (approx) {
		return approx;
	}

	DestinationApproximator *new_approx = malloc(sizeof(DestinationApproximator));
	new_approx->destination_node = destination_node;
	for (int i = 0; i < MAX_ROUTING_ENTRIES; i++) {
		new_approx->best_routing_info[i].in_use = false;
		new_approx->best_routing_info[i].intermediate_node = NO_ID;
		new_approx->best_routing_info[i].steps = INT_MAX;
		new_approx->best_routing_info[i].link_active = false;
	}
	new_approx->next = router->destination_list;
	new_approx->count = 0;
	router->destination_list = new_approx;
	router->approximators += 1;
	return new_approx;
}

static DestinationApproximator *get_destination_approximator(Router *router, ID destination_node) {
    if (!router) return NULL;

    DestinationApproximator *approx = router->destination_list;
    while (approx) {
        if (approx->destination_node == destination_node) {
            return approx;
        }
        approx = approx->next;
    }

    return create_destination_approximator(router, destination_node);
}


static bool update_approximation_entry(Router *router, DestinationApproximator *approximator, ID intermediate_node, int steps) {
    int free_index = -1;
    int max_steps_index = -1;
    int max_steps = -1;

    // update counter
    router->discovery_seq++;
	approximator->last_updated_seq = router->discovery_seq;

    for (int i = 0; i < MAX_ROUTING_ENTRIES; i++) {
        IntermediateStepInfo *info = &approximator->best_routing_info[i];
        if (info->in_use) {
            if (info->intermediate_node == intermediate_node) {
                info->steps = steps;
                return true;
            }

            if (info->steps > max_steps) {
                max_steps = info->steps;
                max_steps_index = i;
            }
        } else if (free_index == -1) {
            free_index = i;
        }
    }
    int idx;
    if (free_index != -1) {
        idx = free_index;
        approximator->count++;
    } else {
        idx = max_steps_index;
    }
    IntermediateStepInfo *slot = &approximator->best_routing_info[idx];
    slot->in_use = true;
    slot->intermediate_node = intermediate_node;
    slot->steps = steps;
    slot->link_active = true;
    return true;
}


static bool remove_approximation_entry(DestinationApproximator *approximator, ID intermediate_node) {
    for (int i = 0; i < MAX_ROUTING_ENTRIES; i++) {
        if (approximator->best_routing_info[i].in_use &&
            approximator->best_routing_info[i].intermediate_node == intermediate_node) {

            approximator->best_routing_info[i].in_use = false;
            approximator->best_routing_info[i].intermediate_node = NO_ID;
            approximator->best_routing_info[i].steps = INT_MAX;
            approximator->best_routing_info[i].link_active = false;
            if (approximator->count > 0) {
                approximator->count--;
            }
            return true;
        }
    }
    return false;
}

static IntermediateStepInfo *choose_approximation_route(DestinationApproximator *approximator) {
    if (!approximator) {
        return NULL;
    }

    IntermediateStepInfo *best_found = NULL;

    for (int i = 0; i < MAX_ROUTING_ENTRIES; i++) {
        IntermediateStepInfo *info = &approximator->best_routing_info[i];
        if (!info->in_use || !info->link_active) continue;

        if (!best_found || info->steps < best_found->steps) {
            best_found = info;
        }
    }

    return best_found;
}

void router_unlink_node(Router *router, ID bad_node) {
    for (DestinationApproximator *approx = router->destination_list; approx; approx = approx->next) {
        for (int i = 0; i < MAX_ROUTING_ENTRIES; i++) {
            IntermediateStepInfo *info = &approx->best_routing_info[i];
            if (info->in_use && info->intermediate_node == bad_node) {
                info->link_active = false;
            }
        }
    }
}

void router_link_node(Router *router, ID node) {
    for (DestinationApproximator *approx = router->destination_list; approx; approx = approx->next) {
        for (int i = 0; i < MAX_ROUTING_ENTRIES; i++) {
            IntermediateStepInfo *info = &approx->best_routing_info[i];
            if (info->in_use && info->intermediate_node == node) {
                info->link_active = true;
            }
        }
    }
}

void router_parse_rquery(Router *router, ID from_node, char *buffer) {
    if (!router || !buffer) {
        return;
    }

    char *saveptr = NULL;
    char *token   = strtok_r(buffer, "|", &saveptr);
    if (!token) {
        return;
    }

    int advertised_count = 0;
    if (sscanf(token, "%d", &advertised_count) != 1) {
        advertised_count = 0;  // treat as "unknown / unlimited"
    }

    int parsed = 0;

    // Remaining tokens: "dest:steps"
    while ((token = strtok_r(NULL, "|", &saveptr)) != NULL) {
        if (advertised_count > 0 && parsed >= advertised_count) {
            break;  // processed as many as the sender claimed
        }

        unsigned int tmp_id = 0;  // for %u
        int steps = 0;

        // dest is uint32_t (ID), steps is int
        if (sscanf(token, "%u:%d", &tmp_id, &steps) != 2) {
            // malformed pair, skip
            continue;
        }

        ID dest_id = (ID)tmp_id;
        router_incorporate_rquery(router, from_node, dest_id, steps);
        parsed++;
    }
}


int router_answer_rquery(Router *router, NodeEntry *node_obj, int count, char *buffer, size_t buffer_size) {
	uint32_t node_last_updated = node_obj->last_rquery;

	typedef struct {
		ID destination_node;
		int steps;
	} RqueryResult;

	RqueryResult info_to_return[count];
	DestinationApproximator *potential_other_approximators[count];
	int inter_steps_found = 0;
	int potetial_approx_found = 0;
	IntermediateStepInfo *best_step = NULL;

	DestinationApproximator *approx = router->destination_list;
	for (; approx != NULL; approx = approx->next) {
	    if (inter_steps_found >= count) break;
	    if (approx->destination_node == node_obj->address.i_addr) continue; // skip self

	    if (approx->last_updated_seq > node_last_updated) {
	        // NEW info for this requester
	        IntermediateStepInfo *best_step = choose_approximation_route(approx);
	        if (!best_step) continue; 

	        info_to_return[inter_steps_found++] =
	            (RqueryResult){
	                .destination_node = approx->destination_node,
	                .steps = best_step->steps
	            };
	    } else {
	        if (potetial_approx_found >= count) continue; 
	        potential_other_approximators[potetial_approx_found++] = approx;
	    }
	}


	if (inter_steps_found < count) {
		approx = NULL;
		for (int i = 0; i < potetial_approx_found; i++) {
			if (inter_steps_found >= count) break; // too many
			approx = potential_other_approximators[i];
			best_step = choose_approximation_route(approx);
			if (!best_step) continue; 
			info_to_return[inter_steps_found++] = (RqueryResult){ .destination_node = approx->destination_node,
																  .steps = best_step->steps};
		}
	}
	node_obj->last_rquery = router->discovery_seq;

	if (buffer_size == 0) return 0; // nothing we can do

    int offset = 0;

    // 1) write the count: count
    int n = snprintf(buffer + offset, buffer_size - offset, "%d", inter_steps_found);
    if (n < 0 || (size_t)n >= buffer_size - offset) {
        // truncated or error; ensure null-termination and bail
        buffer[buffer_size - 1] = '\0';
        node_obj->last_rquery = router->discovery_seq;
        return 0;
    }
    offset += n;

    for (int i = 0; i < inter_steps_found; i++) {
        if (offset >= (int)buffer_size - 1) {
            break; // no more space
        }

        n = snprintf(buffer + offset,
                     buffer_size - offset,
                     "|%u:%d",
                     (unsigned) info_to_return[i].destination_node,
                     info_to_return[i].steps);

        if (n < 0 || (size_t)n >= buffer_size - offset) {
            // truncated or error; stop appending
            buffer[buffer_size - 1] = '\0';
            break;
        }

        offset += n;
    }

    buffer[offset] = '\0';
    return offset;
}


ID router_query_intermediate(Router *router, ID destination_node) {
	DestinationApproximator *approx = router->destination_list;
	while (approx) {
		if (approx->destination_node == destination_node) break;
		approx = approx->next;
	}
	if (!approx) {
		printf("NO APPROXIMATOR TABLE FOR DESTINATION NODE %d\n",destination_node);
		return NO_ID;
	}
	IntermediateStepInfo *info = choose_approximation_route(approx);
	if (!info) return NO_ID;
	return info->intermediate_node;
}

static void router_incorporate_rquery(Router *router, ID from_node, ID destination_node, int steps) {
	DestinationApproximator *dest_approx = get_destination_approximator(router, destination_node);
	update_approximation_entry(router, dest_approx, from_node, steps + 1);
}

void router_update(Router *router, ID origin_node, ID destination_node, ID from_node, int steps) {
	DestinationApproximator *from_approx = get_destination_approximator(router, from_node);
	DestinationApproximator *origin_approx = get_destination_approximator(router, origin_node);

	// we can get to the from node in one step
	update_approximation_entry(router, from_approx, from_node, 1);
	update_approximation_entry(router, origin_approx, from_node, steps);
}

void router_bad_intermediate(Router *router, ID intermediate_node) {
	if (router->approximators == 0) return;
	DestinationApproximator *approx = router->destination_list;
	while (approx) {
		for (int i = 0; i < MAX_ROUTING_ENTRIES; i++) {
			IntermediateStepInfo *info = &approx->best_routing_info[i];
			if (info->in_use && (info->intermediate_node == intermediate_node)) {
				remove_approximation_entry(approx, intermediate_node);
				break;
			}
		}

		approx = approx->next;
	}
} 


