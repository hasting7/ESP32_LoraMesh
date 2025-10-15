#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "nvs_flash.h"

#include "mesh_config.h"
#include "web_server.h"

static const char *header = "<html><h1>ESP32 LoRa mesh Network Interface</h1><h3>By: Ben Hastings</h3><body>";
static const char *footer = "</body></html>";
static const char *TAG = "ap_http_hello";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, header);
    httpd_resp_sendstr_chunk(req, "<div><b>Hello world</b></div>");
    httpd_resp_sendstr_chunk(req, footer);
    return ESP_OK;
}

static httpd_handle_t start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &cfg) == ESP_OK) {
        static const httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server, &root);
        ESP_LOGI(TAG, "HTTP server started on port %d", cfg.server_port);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
    return server;
}

void wifi_start_softap(void)
{
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

    wifi_config_t apcfg = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = sizeof(AP_SSID) - 1,
            .channel = AP_CHANNEL,
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .pmf_cfg = { .required = false },
        },
    };
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

    ESP_LOGI(TAG, "SoftAP started | SSID:%s pass:%s channel:%d",
             AP_SSID, (strlen(AP_PASS) ? AP_PASS : "<open>"), AP_CHANNEL);
    ESP_LOGI(TAG, "Connect and open: http://192.168.4.1/");

    start_http_server();
}
