#ifndef _STRUCTS_H_
#define _STRUCTS_H_

#include <time.h>


// WEB SERVER STRUCTS / VARIABLES

#define AP_SSID     "Join Me"
#define AP_PASS     ""   // >= 8 chars for WPA2; empty for open
#define AP_CHANNEL  6
#define AP_MAX_CONN 4

// LORA STRUCTS / VARIABLES

#define TX_PIN (17)
#define RX_PIN (16)
#define RST_PIN (23)

#define UART_READ_BUFF (2048)
#define UART_WRITE_BUFF (0)
#define UART_PORT (UART_NUM_2)
#define BAUD (115200)

// DATA TABLE STRUCTS / VARIABLES

typedef struct data_entry_struct {
	char *content;    				// content of message
	int src_node;	  				// node where message came from last
	int dst_node;	  				// node where message is trying to be sent
	int origin_node;  				// node where message originated
	int steps;		  				// nodes visited
	time_t timestamp; 				// timestamp of arival

	// maybe add some info from lora to help gage dist

	struct data_entry_struct *next; // next node in table
} DataEntry;



#endif