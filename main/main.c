#include <stdio.h>

#include "driver/uart.h"

#include "node_globals.h"
#include "mesh_config.h"
#include "data_table.h"
#include "node_table.h"
#include "lora_uart.h"
#include "web_server.h"



void app_main(void)
{
    msg_table_init();
    node_table_init();
    wifi_start_softap(&g_address);
    printf("Address of node is: %d, %s\n", (int) g_address.i_addr, g_address.s_addr);
    uart_init();
    printf("UART DRIVER INIT\n");

    LoraInstruction instr = construct_command(ADDRESS, (const char *[]) {g_address.s_addr}, 1);
    uart_send_and_block(instr);
    free(instr);

    // set parame

    // 9,7,1,12 is recommended

    instr = construct_command(BAND, (const char *[]) {"915000000", "M"}, 2);
    uart_send_and_block(instr);
    free(instr);

    instr = construct_command(NETWORKID, (const char *[]) {"18"}, 1);
    uart_send_and_block(instr);
    free(instr);

    instr = construct_command(PARAMETER, (const char *[]) {"9","7","4","16"}, 4);
    uart_send_and_block(instr);
    free(instr);

    create_node_object(g_address.i_addr);
}
