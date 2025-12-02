#ifndef _MAINTENANCE_H
#define _MAINTENANCE_H
#include "node_globals.h"
#include "data_table.h"
#include "node_table.h"
#include "lora_uart.h"
#include <string.h>

void handle_maintenance_msg(ID msg_id);
void resolve_system_command(char *cmd_buffer);

#endif