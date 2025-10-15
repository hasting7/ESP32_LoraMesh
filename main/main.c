#include <stdio.h>
#include "driver/uart.h"

#include "structs.h"
#include "functions.h"

void app_main(void)
{
	wifi_start_softap();
	uart_init();
	printf("UART DRIVER INIT\n");
	
	char buffer[256];
	char response[256];
	int length;
	while (1) {
		length = 0;
		while (1) {
			int n = uart_read_bytes(UART_PORT, buffer, 32, pdMS_TO_TICKS(300));
			buffer[n] = '\0';
			for (int i = 0; buffer[i] != '\0'; i++) {
				response[length++] = buffer[i];
			}
			if (length >= 2 && response[length - 2] == '\r' && response[length-1] == '\n') {
				response[length] = '\0';
				break;
			}
		}
		printf("Message Recieved: %s", response);
	}

}