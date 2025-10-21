#include <stdio.h>

#include "driver/uart.h"

#include "data_table.h"
#include "node_globals.h"
#include "mesh_config.h"
#include "node_table.h"
#include "lora_uart.h"
#include "web_server.h"

#define RESET (0)


void app_main(void)
{
    msg_table_init();
    node_table_init();
    wifi_start_softap(&g_address);
    printf("Address of node is: %d, %s\n", (int) g_address.i_addr, g_address.s_addr);
    uart_init();
    printf("UART DRIVER INIT\n");

    LoraInstruction instr;
    if (RESET) {
        instr = construct_command(FACTORY, NULL, 0);
        uart_send_and_block(instr);
        free(instr);
    }

    // instr = construct_command(CRFOP, (const char *[]) { "16" }, 1);
    // uart_send_and_block(instr);
    // free(instr);

    instr = construct_command(ADDRESS, (const char *[]) {g_address.s_addr}, 1);
    uart_send_and_block(instr);
    free(instr);

    create_node_object(g_address.i_addr);
}
