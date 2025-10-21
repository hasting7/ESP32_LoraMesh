#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "data_table.h"
#include "node_globals.h"
#include "node_table.h"


#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_random.h"


static SemaphoreHandle_t g_dtb_mutex; 

void msg_table_init(void) {
    g_dtb_mutex = xSemaphoreCreateMutex();
}

static uint16_t rand_id(void) {
    const uint32_t m = 9000; // (10000 - 1000) no leading 0s
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % m);
    uint32_t r;
    do {
        r = esp_random();
    } while (r >= limit);
    return (uint16_t)((r % m) + 1000); // 1000..9999
}


DataEntry *create_data_object(char *content, int src, int dst, int origin, int steps, int rssi, int snr)
{
    time_t timestamp;
    time(&timestamp);

    DataEntry *new_entry = malloc(sizeof(DataEntry));
    if (!new_entry) {
        return NULL;
    }

    size_t len = strlen(content);
    char *content_ptr = calloc(len + 1, sizeof(char));
    if (!content_ptr) {
        free(new_entry);
        return NULL;
    }
    memcpy(content_ptr, content, len);
    content_ptr[len] = '\0';

    new_entry->content = content_ptr;
    new_entry->src_node = src;
    new_entry->dst_node = dst;
    new_entry->origin_node = origin;
    new_entry->steps = steps;
    new_entry->timestamp = timestamp;
    new_entry->next = NULL;
    new_entry->rssi = rssi;
    new_entry->snr = snr;
    new_entry->length = (int) len;
    new_entry->id = rand_id();

    if (g_address.i_addr == origin) {
        new_entry->stage = MSG_AT_SOURCE;
    } else if (g_address.i_addr == dst) {
        new_entry->stage = MSG_AT_DESTINATION;
    } else {
        new_entry->stage = MSG_RELAYED;
    }

    table_insert(new_entry);

    return new_entry;
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

static inline int is_after(time_t a, time_t b)
{
    return difftime(a, b) > 0.0;
}

void fmt_time_iso_utc(time_t t, char out[32])
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

void table_insert(DataEntry *data)
{
    if (!data) return;

    xSemaphoreTake(g_dtb_mutex, portMAX_DELAY);

    DataEntry **pp = &g_msg_table;

    while (*pp && !is_after(data->timestamp, (*pp)->timestamp)) {
        pp = &(*pp)->next;
    }

    data->next = *pp;  // insert before *pp
    *pp = data;        // works even when g_table == NULL (then *pp is g_table)

    xSemaphoreGive(g_dtb_mutex);

    printf("data added %s\n", data->content ? data->content : "(null)");
}

int format_data_as_json(DataEntry *data, char *out, int buff_size) {
    // content, source, destination, origin, steps, timestamp, id, length, rssi, snr, stage
    char time_buff[32];
    fmt_time_iso_utc(data->timestamp, time_buff);
    int n = sprintf(
        out,
        "{\"content\" : \"%s\", \"source\" : %d, \"destination\" : %d, \"origin\" : %d, \"steps\" : %d, \"timestamp\" : \"%s\", \"id\" : %d, \"length\" : %d, \"rssi\" : %d, \"snr\" : %d, \"stage\" : %d}",
        data->content, data->src_node, data->dst_node, data->origin_node, data->steps,
        time_buff, data->id, data->length, data->rssi, data->snr, data->stage
    );
    out[buff_size - 1] = '\0';
    return n;
}

