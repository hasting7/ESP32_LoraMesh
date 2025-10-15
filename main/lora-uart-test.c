#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "structs.h"
#include "functions.h"

QueueHandle_t uart_queue;

void uart_init(void) {
	uart_config_t uart_config = {
	    .baud_rate = BAUD,
	    .data_bits = UART_DATA_8_BITS,
	    .parity = UART_PARITY_DISABLE,
	    .stop_bits = UART_STOP_BITS_1,
	    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
	};
	// Configure UART parameters
	// Setup UART buffered IO with event queue
	// Install UART driver using an event queue here
	ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_READ_BUFF, UART_WRITE_BUFF, 10, &uart_queue, 0));
	ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(UART_PORT, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));


}

void set_address(int address) {
	char buffer[32];
	sprintf(buffer, "AT+ADDRESS=%d\r\n", address);
	uart_write_bytes(UART_PORT, buffer, 32);
}

char* uart_send_and_block(char *message) {
	printf("Command: %s", message);
	uart_write_bytes(UART_PORT, message, strlen(message));

	char buffer[32];
	char response[128];
	int length = 0;
	while (1) {
		int n = uart_read_bytes(UART_PORT, buffer, 32, pdMS_TO_TICKS(300));
		buffer[n] = '\0';
		for (int i = 0; buffer[i] != '\0'; i++) {
			response[length++] = buffer[i];
		}
		// break if \r\n
		if (length >= 2 && response[length-2] == '\r' && response[length-1] == '\n') {
			response[length] = '\0';
			break;
		}
	}
	printf("Collected Response: %s", response);
	return NULL;
}

