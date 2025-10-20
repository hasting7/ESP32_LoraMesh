#include <stdio.h>

#include "driver/uart.h"

#include "node_globals.h"
#include "mesh_config.h"
#include "data_table.h"
#include "lora_uart.h"
#include "web_server.h"



void app_main(void)
{
    msg_table_init();
    wifi_start_softap(&g_address);
    printf("Address of node is: %d, %s\n", (int) g_address.i_addr, g_address.s_addr);
    uart_init();
    printf("UART DRIVER INIT\n");

    LoraInstruction instr = construct_command(ADDRESS, (const char *[]) {g_address.s_addr}, 1);
    uart_send_and_block(instr);
    free(instr);
}
