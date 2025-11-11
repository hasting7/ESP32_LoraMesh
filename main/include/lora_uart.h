#ifndef LORA_UART_H
#define LORA_UART_H

#include <stddef.h>

#include "data_table.h"

typedef enum {
        SEND,
        ADDRESS,
        RESET,
        PARAMETER,
        NETWORKID,
	BAND,
	FACTORY,
	CRFOP
} Command;

typedef char * LoraInstruction;

#define MAX_PAYLOAD (240)
#define REQUEST_STATUS_TIME (120)

void uart_init(void);
LoraInstruction construct_command(Command, const char *[], int);
int send_message_blocking(ID);
MessageSendingStatus uart_send_and_block(LoraInstruction instruction, size_t);
void queue_send(ID msg_id, int target);
void message_sending_task(void *);
void node_status_task(void *args);

#endif // LORA_UART_H
