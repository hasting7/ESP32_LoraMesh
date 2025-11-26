#include <stdio.h>
#include <stdlib.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "data_table.h"
#include "lora_uart.h"
#include "mesh_config.h"
#include "node_globals.h"
#include "node_table.h"
#include "web_server.h"

#define RESET (0)


void app_main(void)
{
    msg_table_init();
    node_table_init();
    wifi_start_softap(&g_address);
    printf("Address of node is: %d\n", g_address.i_addr);
    uart_init();
    printf("UART DRIVER INIT\n");

    printf("ATTEMPTING TO ESTABLISH BAUD = 9600\n");
    ID set_baud_cmd = create_command("AT+IPR=9600");
    queue_send(set_baud_cmd, NO_ID);

    printf("SETTING NODE ADDRESS\n");

    char update_addr_buffer[32];
    sprintf(update_addr_buffer, "AT+ADDRESS=%d", g_address.i_addr);
    update_addr_buffer[31] = '\0';

    ID addr_set_cmd = create_command(update_addr_buffer);
    queue_send(addr_set_cmd, NO_ID);


    create_node_object(g_address.i_addr);

    // create message sending task

    xTaskCreate(message_sending_task, "message sender", 4096, NULL, 5, NULL);
    xTaskCreate(node_status_task, "node status checker", 4096, NULL, 5, NULL);

    // find neighbors

    // ID neighbor_msg_id = create_data_object(NO_ID, MAINTENANCE, "discovery", g_address.i_addr, 0, g_address.i_addr, 0, 0, 0, NO_ID);
    // queue_send(neighbor_msg_id, 0);

}
