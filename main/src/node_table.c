#include "node_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "freertos/semphr.h"

#include "data_table.h"
#include "esp_log.h"

NodeEntry *g_node_table = NULL;
static SemaphoreHandle_t g_ntb_mutex;
static const char *TAG = "NODE TABLE";


void node_table_init(void) {
    ESP_LOGI(TAG, "NODE TABLE INIT");
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
    new_entry->misses = 0;
    new_entry->ping_task = NULL;
    new_entry->status = UNKNOWN;
    new_entry->address.i_addr = address;
    int l = snprintf(new_entry->address.s_addr, sizeof new_entry->address.s_addr, "%u", (unsigned)address);
    new_entry->address.s_addr[l] = '\0';
    new_entry->name[0] = '\0';
    // new nodes should inherit last connection time from parents
    time(&new_entry->last_connection);
    new_entry->ping_id = 0;
    // only add ping msg if not urself
    if (g_address.i_addr != address) {
        new_entry->ping_id = create_data_object(NO_ID, MAINTENANCE, "ping", g_address.i_addr, address, g_address.i_addr, 0, 0, 0, 0);
    }

    xSemaphoreTake(g_ntb_mutex, portMAX_DELAY);

    new_entry->next = g_node_table;
    g_node_table = new_entry;

    xSemaphoreGive(g_ntb_mutex);

    ESP_LOGI(TAG, "Node added (%s)",new_entry->address.s_addr);

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

// update the node collection given this new message
int nodes_update(ID msg_id) {
    // add node to table if doesn't exist
    // update metrics
    DataEntry *data = hash_find(g_msg_table, msg_id);

    src = (!data->src_node) ? data->origin_node : data->src_node;
    NodeEntry *src_node = get_node_ptr(src);

    NodeEntry *origin_node = get_node_ptr(data->origin_node);

    if (!src_node) {
        src_node = create_node_object(data->src_node);
    }
    time(&origin_node->last_connection);
    time(&src_node->last_connection);
    origin_node->status = ALIVE;
    src_node->status = ALIVE;
    // misses not really used rn
    origin_node->misses = 0;
    src_node->misses = 0;
    update_metrics(src_node, data->rssi, data->snr);
    
    return 1;
}

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
        (data->name[0] != '\0') ? data->name : "(null)", data->address.s_addr, data->avg_rssi, data->avg_snr, data->messages, data->address.i_addr == g_address.i_addr, difftime(time(NULL), data->last_connection), data->status
    );
    out[buff_size - 1] = '\0';
    return n;
}