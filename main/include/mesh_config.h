#ifndef MESH_CONFIG_H
#define MESH_CONFIG_H

// Wi-Fi SoftAP configuration
#define AP_SSID     "LoRa MeshNet"
#define AP_PASS     ""   // >= 8 chars for WPA2; empty for open
#define AP_CHANNEL  6
#define AP_MAX_CONN 4

// LoRa UART configuration
#define TX_PIN  (17)
#define RX_PIN  (16)
#define RST_PIN (23)

#define UART_READ_BUFF  (2048)
#define UART_WRITE_BUFF (0)
#define UART_PORT       (UART_NUM_2)
#define BAUD            (115200)

#endif // MESH_CONFIG_H
