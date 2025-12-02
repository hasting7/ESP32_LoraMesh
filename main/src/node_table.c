#include "node_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "freertos/semphr.h"

#include "data_table.h"
#include "esp_log.h"

#define BROADCAST_ID (0)
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

    // new nodes should inherit last connection time from parents
    time(&new_entry->last_connection);
    new_entry->ping_id = 0;
    // only add ping msg if not urself
    if (g_address.i_addr != address) {
        new_entry->ping_id = create_data_object(NO_ID, MAINTENANCE, "ping", g_address.i_addr, address, g_address.i_addr, 0, 0, 0, 0);
    }

    int len =sprintf(new_entry->name, "Node %d", address);
    new_entry->name[len] = '\0';

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

int nodes_update(ID msg_id) {
    DataEntry *data = hash_find(g_msg_table, msg_id);
    if (!data) return 0;

    // origin should never be broadcast
    if (data->origin_node == BROADCAST_ID) {
        printf("origin_node should not be 0\n");
        return 0;
    }

    ID src = data->src_node ? data->src_node : data->origin_node;

    NodeEntry *origin_node = node_create_if_needed(data->origin_node);
    NodeEntry *src_node    = node_create_if_needed(src);

    time(&origin_node->last_connection);
    origin_node->status = ALIVE;
    origin_node->misses = 0;

    if (src_node) {
        time(&src_node->last_connection);
        src_node->status = ALIVE;
        src_node->misses = 0;
        update_metrics(src_node, data->rssi, data->snr);
    }

    return 1;
}

// for any time a node is a src or origin run it though this function
// it will do nothing if already in set but if its new it will return 1 and add node
NodeEntry *node_create_if_needed(ID addr) {
    if ((addr == BROADCAST_ID)) return NULL;

    NodeEntry *node = get_node_ptr(addr);
    if (!node) {
        node = create_node_object(addr);
    }
    return node;
}

void attempt_to_reach_node(ID addr) {
    NodeEntry *node = get_node_ptr(addr);
    if (!node) return;

    // if node is new attempt to ping node
    node->status = UNKNOWN;
    if (node->ping_task == NULL) {
        xTaskCreate(
            ping_suspect_node,
            "pingSuspect",
            2048,                // stack size
            (void*)node,         // pvParameters
            tskIDLE_PRIORITY+1,  // priority
            &node->ping_task     // save handle so we don’t double-create
        );
    }
}


void ping_suspect_node(void *args) {
    NodeEntry *node = (NodeEntry *)args;
    if (!node) { vTaskDelete(NULL); return; }

    DataEntry *ping_msg = hash_find(g_msg_table, node->ping_id);
    if (!ping_msg) { 
        ESP_LOGW(TAG, "no ping msg for node %d",node->address.i_addr);
        vTaskDelete(NULL); return; 
    }

    ping_msg->ack_status = 0;

    int delay = 1;
    bool success = false;

    for (int i = 0; i < 4; i++) {
        // send ping
        queue_send(node->ping_id, node->address.i_addr);

        vTaskDelay(pdMS_TO_TICKS(delay * 1500));

        delay <<= 1;

        if (ping_msg->ack_status) {
            success = true;
            break;
        } else {
            node->misses += 1;
        }
    }

    node->status = success ? ALIVE : DEAD;
    node->ping_task = NULL;

    vTaskDelete(NULL);
}

void node_status_task(void *args) {
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(15 * 1000);

    for (;;) {
        time_t now = time(NULL);
        NodeEntry *node = g_node_table;

        while (node) {
            int delta = difftime(now, node->last_connection);

            if (delta <= REQUEST_STATUS_TIME) {
                node->status = ALIVE;
                node->misses = 0;
            } else if (delta > REQUEST_STATUS_TIME &&
                       node->status == ALIVE) {
                ESP_LOGW(TAG,
                         "Node (%d) is suspected to be dead. pinging...",
                         node->address.i_addr);
                attempt_to_reach_node(node->address.i_addr);
            }

            node = node->next;
        }

        vTaskDelayUntil(&last, period);
    }
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
    time_t now = time(NULL);
    double seconds_since_last = difftime(now, data->last_connection);
    if (seconds_since_last < 0) seconds_since_last = 0;

    int n = sprintf(
        out,
        "{\"name\" : \"%s\", \"address\" : \"%s\", \"avg_rssi\" : %.2f, \"avg_snr\" : %.2f, \"messages\" : %d, \"current_node\" : %d, \"last_connection\" : %.0f, \"status\" : %d}",
        (data->name[0] != '\0') ? data->name : "(null)",
        data->address.s_addr,
        data->avg_rssi,
        data->avg_snr,
        data->messages,
        data->address.i_addr == g_address.i_addr,
        seconds_since_last,
        data->status
    );
    out[buff_size - 1] = '\0';
    return n;
}