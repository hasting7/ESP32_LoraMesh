#include <stdlib.h>

#include "node_globals.h"
#include "node_table.h"


#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


static NodeEntry *g_node_table = NULL;
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
    new_entry->address.i_addr = address;
    int l = snprintf(new_entry->address.s_addr, sizeof new_entry->address.s_addr, "%u", (unsigned)address);
    new_entry->address.s_addr[l] = '\0';
    new_entry->name = NULL;

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



size_t render_node_table_html(char *out, size_t out_cap)
{
    if (!out || out_cap == 0) return 0;

    char *p = out;
    size_t left = out_cap;

    /* inline helpers as macros (kept inside this function) */
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
                default:                                                \
                    if (left > 1) { *p++ = (char)*_s; --left; }         \
            }                                                           \
        }                                                               \
    } while (0)

    /* table open + header */
    APPENDF(
        "<table class=\"msg-table\">"
        "<thead><tr>"
        "<th>Addr</th>"
        "<th>Name</th>"
        "<th># Msgs</th>"
        "<th>Avg RSSI (dBm)</th>"
        "<th>Avg SNR (dB)</th>"
        "</tr></thead><tbody>"
    );

    if (!g_node_table) {
        APPENDF("<tr><td colspan=\"5\">No nodes seen yet.</td></tr>");
    } else {
        const NodeEntry *walk = g_node_table;
        size_t row = 0;
        while (walk && left > 1) {
            /* Address string (null-terminated in your Address.s_addr[5]) */
            const char *addr_str = walk->address.s_addr;
            const char *name_str = walk->name ? walk->name : "";

            APPENDF("<tr %s>", (g_address.i_addr == walk->address.i_addr) ? "class=\"this-node\"" : "");

            /* Addr */
            APPENDF("<td>");
            APPEND_ESC_HTML(addr_str);
            APPENDF("</td>");

            /* Name (escaped) */
            APPENDF("<td>");
            APPEND_ESC_HTML(name_str);
            APPENDF("</td>");

            /* # Msgs */
            APPENDF("<td>%d</td>", walk->messages);

            /* Averages — format to 1 decimal; clamp to sane ranges if desired */
            APPENDF("<td>%.1f</td>", (double)walk->avg_rssi); // e.g., -87.4
            APPENDF("<td>%.1f</td>", (double)walk->avg_snr);  // e.g.,  9.7

            APPENDF("</tr>");

            walk = walk->next;

            /* If you might have many rows, yield occasionally to keep WDT happy */
            if ((++row & 7) == 0) {
                vTaskDelay(1); // one tick; safe no-op if very few rows
            }
        }
    }

    APPENDF("</tbody></table>");

    /* NUL-terminate even if truncated */
    if (left > 0) *p = '\0'; else out[out_cap - 1] = '\0';

    #undef APPENDF
    #undef APPEND_ESC_HTML

    return (size_t)(p - out);
}