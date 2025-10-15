#ifndef LORA_UART_H
#define LORA_UART_H

void uart_init(void);
void set_address(int address);
char *uart_send_and_block(char *message);

#endif // LORA_UART_H
