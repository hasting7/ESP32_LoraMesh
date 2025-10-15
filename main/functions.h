#ifndef _FUNCTIONS_H_
#define _FUNCTIONS_H_

// WEB SERVER FUNCTIONS

void wifi_start_softap(void);

// LORA FUNCTIONS

void uart_init(void);
char * uart_send_and_block(char *);
void set_address(int);


// DATA TABLE FUNCTIONS

#endif