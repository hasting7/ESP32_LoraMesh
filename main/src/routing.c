#include "routing.h"

#include <limits.h>

#define MAX_ROUTING_ENTRIES (4)


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

	struct destination_approximator_struct *next;
} DestinationApproximator;


typedef struct {
	DestinationApproximator *destination_list;
	int approximators;
	ID node_id;
} Router;

// public
Router *create_router(ID for_node);
ID router_query_intermediate(Router *router, ID destination_node);
void router_update(Router *router, ID origin_node, ID destination_node, ID from_node, int steps);
void router_bad_intermediate(Router *router, ID intermediate_node);
void router_link_node(Router *router, ID node);
void router_unlink_node(Router *router, ID bad_node);


// hidden
static DestinationApproximator *create_destination_approximator(Router *router, ID destination_node);
static DestinationApproximator *get_destination_approximator(Router *router, ID destination_node);
static bool update_approximation_entry(DestinationApproximator *approximator, ID intermediate_node, int steps);
static bool remove_approximation_entry(DestinationApproximator *approximator, ID intermediate_node);
static ID choose_approximation_route(DestinationApproximator *approximator);


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


static bool update_approximation_entry(DestinationApproximator *approximator, ID intermediate_node, int steps) {
    int free_index = -1;
    int max_steps_index = -1;
    int max_steps = -1;

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

static ID choose_approximation_route(DestinationApproximator *approximator) {
    if (!approximator) {
        return NO_ID;
    }

    ID best_id = NO_ID;
    int best_steps = INT_MAX;

    for (int i = 0; i < MAX_ROUTING_ENTRIES; i++) {
        IntermediateStepInfo *info = &approximator->best_routing_info[i];
        if (!info->in_use || !info->link_active) continue;

        if (info->steps < best_steps) {
            best_steps = info->steps;
            best_id = info->intermediate_node;
        }
    }

    return best_id;
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

void router_answer_rquery(Router *router, ID requestor_node, int count, char *buffer) {

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
	return choose_approximation_route(approx);
}

void router_update(Router *router, ID origin_node, ID destination_node, ID from_node, int steps) {
	DestinationApproximator *from_approx = get_destination_approximator(router, from_node);
	DestinationApproximator *origin_approx = get_destination_approximator(router, origin_node);

	// we can get to the from node in one step
	update_approximation_entry(from_approx, from_node, 1);
	update_approximation_entry(origin_approx, from_node, steps);

	return true;
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


