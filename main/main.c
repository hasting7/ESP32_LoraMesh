#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "data_table.h"
#include "lora_uart.h"
#include "node_globals.h"
#include "node_table.h"
#include "web_server.h"
#include "esp_log.h"
#include "maintenance.h"

static const char *TAG = "Main";


void app_main(void)
{
    // INIT DRIVERS

    msg_table_init();
    node_table_init();
    ID address = (ID) wifi_start_softap();
    g_my_address = address;
    ESP_LOGI(TAG, "Address is %d", address);
    g_this_node = create_node_object(address);

    uart_init();

    ID query_baud = create_command("AT+IPR?");
    queue_send(query_baud, NO_ID, false);

    // SET ADDRESS

    ESP_LOGI(TAG, "Setting node address to %d", address);

    char update_addr_buffer[32];
    sprintf(update_addr_buffer, "AT+ADDRESS=%d", address);
    update_addr_buffer[31] = '\0';

    ID addr_set_cmd = create_command(update_addr_buffer);
    queue_send(addr_set_cmd, NO_ID, false);

    // CREATE TASKS

    xTaskCreate(message_sending_task, "message sender",      4096, NULL, 5, NULL);
    xTaskCreate(node_status_task,     "node status checker", 4096, NULL, 5, NULL);
    xTaskCreate(rquery_task,          "rquery_task",         4096, NULL, 5, NULL);

    // INIT NEIGHBOR SEARCH
 
    ID neighbor_msg_id = create_data_object(NO_ID, MAINTENANCE, "gbcast", address, 0, address, 0, 0, 0, NO_ID);
    queue_send(neighbor_msg_id, 0, false);

}
