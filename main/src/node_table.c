#include <stdlib.h>

#include "data_table.h"
#include "node_globals.h"
#include "node_table.h"


#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


NodeEntry *g_node_table = NULL;
static SemaphoreHandle_t g_ntb_mutex; 


void node_table_init(void) {
    g_ntb_mutex = xSemaphoreCreateMutex();
}


NodeEntry *create_node_object(int address) {
	NodeEntry *new_entry = malloc(sizeof(NodeEntry));
    if (!new_entry) {
        return NULL;
    }
    new_entry->avg_rssi = 0;
    new_entry->avg_snr = 0;
    new_entry->messages = 0;
    new_entry->status = UNKNOWN;
    new_entry->address.i_addr = address;
    int l = snprintf(new_entry->address.s_addr, sizeof new_entry->address.s_addr, "%u", (unsigned)address);
    new_entry->address.s_addr[l] = '\0';
    new_entry->name = NULL;
    // new nodes should inherit last connection time from parents
    time(&new_entry->last_connection);
    new_entry->ping_id = 0;
    // only add ping msg if not urself
    if (g_address.i_addr != address) {
        new_entry->ping_id = create_data_object(NO_ID, PING, "ping", g_address.i_addr, address, g_address.i_addr, 0, 0, 0);
    }

    xSemaphoreTake(g_ntb_mutex, portMAX_DELAY);

    new_entry->next = g_node_table;
    g_node_table = new_entry;

    xSemaphoreGive(g_ntb_mutex);

    printf("node added %s\n", new_entry->address.s_addr);

    return new_entry;
}

NodeEntry *get_node_ptr(int address) {
	NodeEntry *walk = g_node_table;
	while (walk) {
		if (walk->address.i_addr == address) return walk;
		walk = walk->next;
	}
	return NULL;
}


// void remove_node(NodeEntry *node) {
//     // remove node and all messages (destination = node, origin = node, source = node) 
// }

void update_metrics(NodeEntry *node, int rssi, int snr) {
	if (node->messages == 0) { // init
		node->avg_rssi = rssi;
		node->avg_snr = snr;
		node->messages = 1;
		return;
	}
	node->avg_rssi = (rssi * EMA_SMOOTHING) + (1.0f - EMA_SMOOTHING) * node->avg_rssi;
	node->avg_snr = (snr * EMA_SMOOTHING) + (1.0f - EMA_SMOOTHING) * node->avg_snr;
	node->messages += 1;
}


int format_node_as_json(NodeEntry *data, char *out, int buff_size) {
    // name, address, avg_rssi, avg_snr, messages
    int n = sprintf(
        out,
        "{\"name\" : \"%s\", \"address\" : \"%s\", \"avg_rssi\" : %.2f, \"avg_snr\" : %.2f, \"messages\" : %d, \"current_node\" : %d, \"last_connection\" : %.2f, \"status\" : %d}",
        (data->name) ? data->name : "(null)", data->address.s_addr, data->avg_rssi, data->avg_snr, data->messages, data->address.i_addr == g_address.i_addr, difftime(time(NULL), data->last_connection), data->status
    );
    out[buff_size - 1] = '\0';
    return n;
}