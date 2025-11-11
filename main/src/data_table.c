#include "data_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "hash_table.h"
#include "node_globals.h"
#include "node_table.h"

#define TABLE_SIZE (100)

static SemaphoreHandle_t g_dtb_mutex; 

void msg_table_init(void) {
    g_dtb_mutex = xSemaphoreCreateMutex();
    g_msg_table = create_hashtable(TABLE_SIZE);
}

ID create_data_object(int id, MessageType type, char *content, int src, int dst, int origin, int steps, int rssi, int snr)
{
    printf("creating object for: %s\n",content);
    DataEntry *new_entry = malloc(sizeof(DataEntry));
    if (!new_entry) {
        return 0;
    }

    size_t len = strlen(content);
    char *content_ptr = calloc(len + 1, sizeof(char));
    if (!content_ptr) {
        free(new_entry);
        return 0;
    }
    memcpy(content_ptr, content, len);
    content_ptr[len] = '\0';

    new_entry->content = content_ptr;
    new_entry->src_node = src;
    new_entry->dst_node = dst;
    new_entry->origin_node = origin;
    new_entry->steps = steps;
    new_entry->target_node = 0;
    new_entry->message_type = type;
    new_entry->transfer_status = NO_STATUS;
    new_entry->ack_status = 0;
    time(&new_entry->timestamp);
    new_entry->next = NULL;
    new_entry->rssi = rssi;
    new_entry->snr = snr;
    new_entry->length = (int) len;
    if (id == NO_ID) {
        do {
            new_entry->id = rand_id();
        } while (hash_find(g_msg_table, new_entry->id));
    } else {
        new_entry->id = id;
    }

    if (g_address.i_addr == origin) {
        new_entry->stage = MSG_AT_SOURCE;
    } else if (g_address.i_addr == dst) {
        new_entry->stage = MSG_AT_DESTINATION;
    } else {
        new_entry->stage = MSG_RELAYED;
    }

    printf("inserting into table: %s\n",new_entry->content);

    xSemaphoreTake(g_dtb_mutex, portMAX_DELAY);

    hash_insert(g_msg_table, new_entry->id, (void *) new_entry);

    xSemaphoreGive(g_dtb_mutex);

    printf("data added %s\n", new_entry->content ? new_entry->content : "(null)");
    printf("load factor = %d/%d\n", g_msg_table->entries, g_msg_table->size);

    return new_entry->id;
}

void free_data_object(DataEntry **ptr)
{
    if (!ptr || !*ptr) {
        return;
    }
    DataEntry *root = *ptr;
    free(root->content);
    free(root);
    *ptr = NULL;
}

int format_data_as_json(DataEntry *data, char *out, int buff_size) {
    // content, source, destination, origin, steps, timestamp, id, length, rssi, snr, stage

    char time_buff[32];
    struct tm tm;
    gmtime_r(&data->timestamp, &tm);
    strftime(time_buff, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);

    int n = sprintf(
        out,
        "{\"content\" : \"%s\", \"source\" : %d, \"destination\" : %d, \"origin\" : %d, \"steps\" : %d, \"timestamp\" : \"%s\", \"id\" : %d, \"length\" : %d, \"rssi\" : %d, \"snr\" : %d, \"stage\" : %d, \"transfer_status\" : %d, \"ack_status\" : %d, \"message_type\" : %d}",
        data->content, data->src_node, data->dst_node, data->origin_node, data->steps,
        time_buff, data->id, data->length, data->rssi, data->snr, data->stage, data->transfer_status, data->ack_status, data->message_type
    );
    out[buff_size - 1] = '\0';
    return n;
}



