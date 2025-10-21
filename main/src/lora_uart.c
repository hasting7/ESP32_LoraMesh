#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "data_table.h"
#include "node_globals.h"
#include "mesh_config.h"
#include "node_table.h"
#include "lora_uart.h"


static QueueHandle_t uart_queue;

static void uart_event_task(void *arg)
{
    uart_event_t event;
    uint8_t *tmp = NULL;

    for (;;) {
        if (xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
            switch (event.type) {
            case UART_PATTERN_DET: {
                // Get position of the first pending '\n' in ring buffer
                int pos = uart_pattern_pop_pos(UART_PORT);
                if (pos >= 0) {
                    // Read exactly [pos+1] bytes (including '\n') from the buffer
                    int to_read = pos + 1;
                    tmp = (uint8_t *)malloc(to_read + 1);
                    if (!tmp) { uart_flush_input(UART_PORT); break; }

                    int n = uart_read_bytes(UART_PORT, tmp, to_read, pdMS_TO_TICKS(50));
                    if (n > 0) {
                        tmp[n] = '\0';
                        // Trim optional '\r\n' → keep clean line
                        while (n > 0 && (tmp[n-1] == '\n' || tmp[n-1] == '\r')) tmp[--n] = '\0';

                        // >>> This is your “message complete” point <<<
                        // Push to your message table / queue / handler:
                        printf("UART LINE: %s\n", (char*)tmp);

                        // message like "+RCV=7296,11,Hello World,origin,dest,-45,11"  
                        //                    from,len,data,    rssi, snr
                        int from, origin, dest, step, len, rssi, snr;
                        char data[250];
                        sscanf((char *)tmp,"+RCV=%d,%d,%249[^,],%d,%d,%d,%d,%d",&from, &len, data, &origin, &dest, &step, &rssi, &snr);

                        // REPLACE THIS WITH PROPER PROPAGATION OF NODES
                        NodeEntry *node = get_node_ptr(origin);
                        if (!node) {
                            node = create_node_object(origin);
                        }
                        update_metrics(node, rssi, snr);

                        // compile the table obj but for now dont worry
                        create_data_object(data, from, dest, origin, step + 1, rssi, snr);

                        // e.g., table_insert_from_uart((char*)tmp);
                    }
                    free(tmp); tmp = NULL;

                    // There might be more complete lines already in the buffer;
                    // let the while-loop iterate via next queue events (driver posts one per pattern)
                } else {
                    // No position? Drain to keep buffer sane
                    uart_flush_input(UART_PORT);
                }
                break;
            }

            case UART_DATA: {
                // Data arrived but no pattern yet; you can ignore here and wait for PATTERN_DET
                // or implement a size/timeout-based frame if some senders don’t send '\n'.
                // Example of “idle frame” handling would use UART_FIFO_TOUT event (if set_rx_timeout).
                break;
            }

            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                // Ring buffer overflow—drain and warn
                uart_flush_input(UART_PORT);
                xQueueReset(uart_queue);
                printf("UART overflow, flushed\n");
                break;

            case UART_BREAK:
            case UART_PARITY_ERR:
            case UART_FRAME_ERR:
                // You can log or ignore; usually just continue
                break;

            default:
                break;
            }
        }
    }
}

int send_message(DataEntry *data, int to_address) {
    // send args "to_address, len, message, origin, dest, steps"
    char cmd[512];

    char to_address_s[5];
    char len_s[5];

    int arg_length = sprintf(cmd, "%s,%d,%d,%d", data->content, data->origin_node, data->dst_node, data->steps);
    sprintf(len_s, "%d", arg_length);
    sprintf(to_address_s, "%d", to_address);

    LoraInstruction instruction = construct_command(SEND, (const char *[]) {to_address_s, len_s, cmd}, 3);
    printf("Sending: %s\n",instruction);

    int resp = uart_send_and_block(instruction);

    printf("Response: %d\n",resp);
    free(instruction);

    return resp;

    // arg_length += sprintf(to_address_s, "%d", to_address);
    // arg_length += sprintf(len_s, "%d", )

}

LoraInstruction construct_command(Command cmd, const char *args[], int n) {
    char cmd_buffer[256] = "AT+";
    int len = 3;

    int expect_args = -1;
    const char *name = NULL;

    if (cmd == SEND) {
        expect_args = 3;
        name = "SEND";
    } else if (cmd == ADDRESS) {
        expect_args = 1;
        name = "ADDRESS";
    } else if (cmd == RESET) {
        name = "RESET";
        expect_args = 0;
    } else if (cmd == PARAMETER) {
        name = "PARAMETER";
        expect_args = 4;
    } else if (cmd == BAND) {
        name = "BAND";
        expect_args = 2;
    } else if (cmd == NETWORKID) {
        name = "NETWORKID";
        expect_args = 1;
    } else if (cmd == FACTORY) {
        name = "FACTORY";
        expect_args = 0;
    } else if (cmd == CRFOP) {
        name = "CRFOP";
        expect_args = 1;
    }
    if (n != expect_args) {
        printf("%s cmd should have %d args not %d\n",name, expect_args, n);
        return NULL;
    }
    for (int i = 0; name[i] != '\0'; i++) {
        cmd_buffer[len++] = name[i];
    }
    if (expect_args != 0) {   
        cmd_buffer[len++] = '=';
        for (int i = 0; i < n; i++) {
            for (int l = 0; args[i][l] != '\0'; l++) {
                cmd_buffer[len++] = args[i][l];
            }
            cmd_buffer[len++] = ',';
        }
        len--;
    }
    cmd_buffer[len++] = '\r';
    cmd_buffer[len++] = '\n';
    cmd_buffer[len++] = '\0';

    char *ret = calloc(len, sizeof(char));
    strncpy(ret, cmd_buffer, len);
    return ret;
}

int uart_send_and_block(LoraInstruction instruction) {
    printf("instruction: %s", instruction);
    uart_write_bytes(UART_PORT, instruction, strlen(instruction));

    char buffer[32];
    static char response[128];
    int length = 0;
    while (1) {
        int n = uart_read_bytes(UART_PORT, buffer, sizeof(buffer), pdMS_TO_TICKS(300));
        if (n <= 0) {
            continue;
        }
        buffer[n] = '\0';
        for (int i = 0; i < n; i++) {
            if (length < (int)(sizeof(response) - 1)) {
                response[length++] = buffer[i];
            }
        }
        if (length >= 2 && response[length - 2] == '\r' && response[length - 1] == '\n') {
            break;
        }
    }
    response[length] = '\0';
    printf("Collected Response: %s", response);
    return strcmp("+OK", response);
}

void uart_init(void) {
    uart_config_t uart_config = {
        .baud_rate = BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_READ_BUFF, UART_WRITE_BUFF, 20, &uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // pattern detection
    ESP_ERROR_CHECK(uart_enable_pattern_det_baud_intr(UART_PORT, '\n', 1, 9, 0, 0));
    ESP_ERROR_CHECK(uart_pattern_queue_reset(UART_PORT, 10));
    xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 10, NULL);
}
