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


#define MAX_PAYLOAD (240)
#define REQUEST_STATUS_TIME (120)

void uart_init(void);
// LoraInstruction construct_command(Command, const char *[], int);
int send_message_blocking(ID);
MessageSendingStatus uart_send_and_block(char *, size_t, char *, size_t);
void queue_send(ID msg_id, ID target, bool use_router);
void message_sending_task(void *);

#endif // LORA_UART_H
