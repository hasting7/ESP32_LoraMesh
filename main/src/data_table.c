#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "data_table.h"
#include "node_globals.h"


#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


static DataEntry *g_table = NULL;
static SemaphoreHandle_t g_dtb_mutex; 

void msg_table_init(void) {
    g_dtb_mutex = xSemaphoreCreateMutex();
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

    DataEntry **pp = &g_table;

    while (*pp && !is_after(data->timestamp, (*pp)->timestamp)) {
        pp = &(*pp)->next;
    }

    data->next = *pp;  // insert before *pp
    *pp = data;        // works even when g_table == NULL (then *pp is g_table)

    xSemaphoreGive(g_dtb_mutex);

    printf("data added %s\n", data->content ? data->content : "(null)");
}


DataEntry* render_messages_table_chunk(char *out, size_t out_cap,
                                       const DataEntry *ptr, size_t *out_len)
{
    if (out_len) *out_len = 0;
    if (!out || out_cap == 0) return (DataEntry*)ptr;

    char *p = out;
    size_t left = out_cap;

    // ---- helpers ----
    #define APPENDF(...) do {                                           \
        if (left > 1) {                                                 \
            int _n = snprintf(p, left, __VA_ARGS__);                    \
            if (_n < 0) _n = 0;                                         \
            size_t _w = (size_t)_n;                                     \
            if (_w >= left) { p += (left - 1); left = 1; }              \
            else         { p += _w;            left -= _w; }            \
        }                                                               \
    } while (0)

    #define APPEND_ESC_HTML(S) do {                                     \
        const unsigned char *_s = (const unsigned char*)((S)?(S):"");   \
        for (; *_s; ++_s) {                                             \
            switch (*_s) {                                              \
                case '&': APPENDF("&amp;");  break;                     \
                case '<': APPENDF("&lt;");   break;                     \
                case '>': APPENDF("&gt;");   break;                     \
                case '\"':APPENDF("&quot;"); break;                     \
                case '\'':APPENDF("&#39;");  break;                     \
                default: if (left > 1) { *p++ = (char)*_s; --left; }    \
            }                                                           \
        }                                                               \
    } while (0)
    // -----------------

    const DataEntry *next_ptr = NULL;

    if (ptr == NULL) {
        // Header
        APPENDF(
            "<table class=\"msg-table\">"
            "<thead><tr>"
            "<th>Route Stage</th>"
            "<th>Origin</th>"
            "<th>Src</th>"
            "<th>RSSI (dBm)</th>"
            "<th>SNR (dB)</th>"
            "<th>Content</th>"
            "</tr></thead><tbody>"
        );

        if (!g_table) {
            // Empty table: body + footer
            APPENDF("<tr><td colspan=\"6\">No messages yet.</td></tr>");
            APPENDF("</tbody></table>");
            next_ptr = NULL;
        } else {
            // More rows to come
            next_ptr = g_table;
        }
    } else {
        // One row
        APPENDF("<tr class=\"type-%d\">", (int)ptr->stage);
        APPENDF("<td>%d</td>", ptr->stage);
        APPENDF("<td>%d</td>", ptr->origin_node);
        APPENDF("<td>%d</td>", ptr->src_node);
        APPENDF("<td>%d</td>", ptr->rssi);
        APPENDF("<td>%d</td>", ptr->snr);

        APPENDF("<td>");
        APPEND_ESC_HTML(ptr->content);  // if content may be binary, switch to length-based escaping
        APPENDF("</td>");

        APPENDF("</tr>");

        // Footer if this was the last row
        if (ptr->next == NULL) {
            APPENDF("</tbody></table>");
        }

        next_ptr = ptr->next;
    }

    // NUL-terminate even if truncated
    if (left > 0) *p = '\0'; else out[out_cap - 1] = '\0';
    if (out_len) *out_len = (size_t)(p - out);

    #undef APPENDF
    #undef APPEND_ESC_HTML

    return (DataEntry*)next_ptr;
}