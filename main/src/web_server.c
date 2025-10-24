#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "nvs_flash.h"

#include "data_table.h"
#include "node_globals.h"
#include "node_table.h"
#include "mesh_config.h"
#include "web_server.h"
#include "lora_uart.h"


static const char *TAG = "ap_http_hello";
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[]   asm("_binary_style_css_end");

static esp_err_t css_get_handler(httpd_req_t *req)
{
    size_t len = style_css_end - style_css_start;
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
    return httpd_resp_send(req, (const char *)style_css_start, len);
}

// GET /
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    size_t len = (size_t)(index_html_end - index_html_start);
    return httpd_resp_send(req, (const char*)index_html_start, len);
}

static esp_err_t api_get_msgs(httpd_req_t *req) {
    // since_id -> only grab messages before id
    //
    // address -> only returns messages between (this and address) ignore relay
    // modes (conversational, all, relay, broadcasts)
    printf("GET /api/messages\n");
    uint64_t since_id = 0;
    bool have_since_id = false;

    int qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0) {
        char *q = malloc(qlen + 1);
        if (!q) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, q, qlen + 1) == ESP_OK) {
            char v[32];
            if (httpd_query_key_value(q, "since_id", v, sizeof v) == ESP_OK) {
                char *end = NULL;
                unsigned long long tmp = strtoull(v, &end, 10);
                if (end && *end == '\0') {
                    since_id = (uint64_t)tmp;
                    have_since_id = true;
                }
            }
        }
        free(q);
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");

    DataEntry *walk = g_msg_table;
    char buffer[1024];
    bool first = true;
    for (; walk; walk = walk->next) {

        if (have_since_id && walk->id == since_id) break;

        if (!first) httpd_resp_sendstr_chunk(req, ",");
        first = false;

        format_data_as_json(walk, buffer, sizeof buffer);
        httpd_resp_sendstr_chunk(req, buffer);
    }
    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t api_get_nodes(httpd_req_t *req) {
    printf("GET /api/nodes\n");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");

    NodeEntry *walk = g_node_table;
    char buffer[1024];
    bool first = true;
    for (; walk; walk = walk->next) {
        if (!first) httpd_resp_sendstr_chunk(req, ",");
        first = false;

        format_node_as_json(walk, buffer, sizeof buffer);
        httpd_resp_sendstr_chunk(req, buffer);
    }
    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t send_post_handler(httpd_req_t *req)
{
    size_t total = req->content_len;
    if (total > 4096) {
        ESP_LOGW(TAG, "POST too large: %u", (unsigned)total);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
        return ESP_FAIL;
    }

    char *buf = malloc(total + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    size_t read = 0;
    while (read < total) {
        int r = httpd_req_recv(req, buf + read, total - read);
        if (r <= 0) {
            free(buf);
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Timeout");
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv error");
            }
            return ESP_FAIL;
        }
        read += (size_t)r;
    }
    buf[read] = '\0';
    printf("buffer: %s\n",buf);

    // 2) Parse message=... (application/x-www-form-urlencoded)
    char message[250];
    int target;
    sscanf(buf, "target=%d&message=%249s", &target, message);
    message[249] = '\0';
    // add back in spaces to messages
    for (int i = 0; message[i]; i++) {
        if (message[i] == '+') message[i] = ' ';
    }
    free(buf);

    DataEntry *entry = create_data_object(
        NO_ID,
        (0 == target) ? BROADCAST : NORMAL,
        message, 
        g_address.i_addr, 
        target, 
        g_address.i_addr, 
        0, 0, 0
    );
    queue_send(entry, target);


    // 3) Handle it (enqueue / store / broadcast)
    ESP_LOGI(TAG, "POST /send message: \"%s\"", message);
    // e.g., push_message(from=local_addr, msg=message);
    // add_row_to_table(...);

    // 4) Redirect back to "/"
    httpd_resp_set_status(req, "303 See Other"); // avoids form re-submit on reload
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, ""); // body optional
    return ESP_OK;
}

static httpd_handle_t start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &cfg) == ESP_OK) {
        // GET /
        static const httpd_uri_t root = {
            .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL,
        };
        // GET css
        static const httpd_uri_t css = {
            .uri = "/style.css", .method = HTTP_GET, .handler = css_get_handler
        };
        // GET /api/messages?since_id=
        const httpd_uri_t uri_api_msgs = {
             .uri="/api/messages", .method=HTTP_GET, .handler=api_get_msgs
        };
         // GET /api/nodes
        const httpd_uri_t uri_api_nodes = {
            .uri="/api/nodes", .method=HTTP_GET, .handler=api_get_nodes
        };
        // POST /send
        static const httpd_uri_t uri_send = {
            .uri      = "/send", .method   = HTTP_POST, .handler  = send_post_handler,
        };

        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &css);
        httpd_register_uri_handler(server, &uri_send);
        httpd_register_uri_handler(server, &uri_api_msgs);
        httpd_register_uri_handler(server, &uri_api_nodes);
        ESP_LOGI(TAG, "HTTP server started on port %d", cfg.server_port);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
    return server;
}

void wifi_start_softap(Address *address) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wicfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wicfg));

    // TODO replace later with using lora to identifying overlapping nodes and add simple id

    char ssid_buf[33]; // +1 for safety during snprintf                  // HW RNG
    uint16_t suffix = rand_id();
    // Reserve space for "-%03u" (5 chars) if possible; otherwise truncate base.
    const char *base = AP_SSID;
    size_t base_len = strlen(base);
    const char *fmt = "-%04u";
    const size_t suffix_len = 5;                 // "-%03u"
    size_t max_base = (base_len + suffix_len <= 32) ? base_len : (32 - suffix_len);

    // Copy (possibly truncated) base
    memcpy(ssid_buf, base, max_base);
    // Append suffix
    int n = snprintf(ssid_buf + max_base, sizeof(ssid_buf) - max_base, fmt, (unsigned)suffix);
    size_t final_len = max_base + (n > 0 ? (size_t)n : 0);
    if (final_len > 32) final_len = 32;          // hard cap for safety

    wifi_config_t apcfg = {0};
    // Copy SSID into config (no null terminator expected in struct)
    memcpy(apcfg.ap.ssid, ssid_buf, final_len);
    apcfg.ap.ssid_len = final_len;

    apcfg.ap.channel        = AP_CHANNEL;
    strncpy((char *)apcfg.ap.password, AP_PASS, sizeof(apcfg.ap.password)-1);
    apcfg.ap.max_connection = AP_MAX_CONN;
    apcfg.ap.authmode       = WIFI_AUTH_WPA_WPA2_PSK;
    apcfg.ap.pmf_cfg.required = false;

    if (strlen(AP_PASS) == 0) {
        apcfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apcfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_country_t country = {
        .cc = "US",
        .schan = 1,
        .nchan = 11,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    esp_wifi_set_country(&country);

    // Log what we actually set
    char shown_ssid[33] = {0};
    memcpy(shown_ssid, apcfg.ap.ssid, apcfg.ap.ssid_len);
    ESP_LOGI(TAG, "SoftAP started | SSID:%s pass:%s channel:%d",
             shown_ssid, (strlen(AP_PASS) ? AP_PASS : "<open>"), AP_CHANNEL);
    ESP_LOGI(TAG, "Connect and open: http://192.168.4.1/");

    start_http_server();

    address->i_addr = (int) suffix;
    int l = snprintf(address->s_addr, sizeof address->s_addr, "%u", (unsigned)suffix);
    address->s_addr[l] = '\0';

}
