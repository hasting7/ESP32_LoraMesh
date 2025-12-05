#ifndef _MAINTENANCE_H
#define _MAINTENANCE_H

#include "node_globals.h"

void handle_maintenance_msg(ID msg_id);
void resolve_system_command(char *cmd_buffer);
void rquery_task(void *arg);

#endif