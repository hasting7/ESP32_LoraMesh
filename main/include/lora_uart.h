#ifndef LORA_UART_H
#define LORA_UART_H

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

void uart_init(void);
LoraInstruction construct_command(Command, const char *[], int);
int send_message_blocking(DataEntry *, int);
MessageSendingStatus uart_send_and_block(LoraInstruction instruction);
void queue_send(DataEntry *data);
void message_sending_task(void *);

#endif // LORA_UART_H
