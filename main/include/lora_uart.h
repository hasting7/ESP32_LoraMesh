#ifndef LORA_UART_H
#define LORA_UART_H

typedef enum {
	SEND,
	ADDRESS,
	RESET,
	PARAMETER,
	NETWORKID,
	BAND
} Command;

typedef char * LoraInstruction;

void uart_init(void);
int uart_send_and_block(LoraInstruction);
LoraInstruction construct_command(Command, const char *[], int);
int send_message(DataEntry *, int);

#endif // LORA_UART_H
