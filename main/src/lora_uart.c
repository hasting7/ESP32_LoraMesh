#include "lora_uart.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "driver/uart.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "node_globals.h"
#include "node_table.h"
#include "maintenance.h"
#include "routing.h"
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

static MessageSendingStatus uart_send_and_block(char *, size_t, char *, size_t);
static int send_message_blocking(ID msg_id);

static void uart_reader_task(void *arg);
static void rcv_handler_task(void *arg);

static const char *TAG = "UART";
// static const int MAX_PAYLOAD = 240;

static QueueHandle_t MessageQueue;

QueueHandle_t q_resp;
QueueHandle_t q_rcv;



static bool parse_rcv_line(const char *line,
                           ID *from, int *len, char *data, size_t data_cap,
                           ID *origin, ID *dest, int *step, int *msg_type, ID *id, ID *ack_for,
                           int *rssi, int *snr)
{
    // data->content, data->origin_node, data->dst_node, data->steps, data->message_type, data->id, data->ack_for);
    //                          +RCV=6121,22,ping,6121,0,0,6,2009,0,-5,11
    int scanned = sscanf(line, "+RCV=%hd,%d,%250[^,],%hd,%hd,%d,%d,%hd,%hd,%d,%d",
        from, len, data, origin, dest, step, msg_type, id, ack_for, rssi, snr
    );
    printf("Scanned args = %d\n",scanned);
    printf("+RCV=from = %hd,len = %d,data = %s,origin = %hd,dest = %hd,step = %d,msg_type = %d,id = %hd,ack_for = %hd,rssi = %d,snr = %d\n",
        *from, *len, data, *origin, *dest, *step, *msg_type, *id, *ack_for, *rssi, *snr);
    if (scanned != 11) {
        printf("wrong number of args read\n");
        return false;
    }

    return true;
}


int format_message_command(ID msg_id, char *command_buffer, size_t length) {
    DataEntry *data = msg_find(msg_id);
    // ID to_address = data->target_node;
    if (!data) {
        ESP_LOGE(TAG, "Formatting message id=%d does not exist",msg_id);
        return 0;
    }

    // using uint16_t (two chars)         2bytes  2bytes       1b yte        2 bytes (meta data size = 7) saving 7 bytes
    // Build message payload: "<content>,<origin>,<dest>,<steps>,<msg_type>,<id>,<ack_for_id>"
    // char cmd[512];
    char payload[256];
    int payload_len = 0;

    payload_len = snprintf(payload, sizeof(payload), "%s,%u,%u,%d,%d,%u,%u",
                        data->content, data->origin_node, data->dst_node, data->steps, data->message_type, data->id, data->ack_for);

    int final_str_length = snprintf(command_buffer, length, "AT+SEND=%d,%d,%s\r\n", data->target_node, payload_len, payload);

    printf("COMMAND: %s",command_buffer);

    return final_str_length;

}


static int send_message_blocking(ID msg_id) {
    DataEntry *data = msg_find(msg_id);

    char command_buffer[256];
    size_t length;

    if (data->message_type == COMMAND) {
        // send message as just content
        length = strlcpy(command_buffer, data->content, sizeof(command_buffer));
        if (length >= sizeof(command_buffer) - 2) {
            length -= 2;
        }
        command_buffer[length++] = '\r';
        command_buffer[length++] = '\n';
        command_buffer[length] = '\0';
        ESP_LOGI(TAG, "Sending command construction \"%s\" (len = %d)",command_buffer, length);
    } else {
        // send formatted message
        length = format_message_command(msg_id, command_buffer, sizeof(command_buffer));
        printf("Command (len = %d) is \"%s\"",length, command_buffer);
        if (!length) {
            ESP_LOGE(TAG, "Issue formatting send message string");

            return 0;
        }
    }

    size_t max_response_length = 40;
    char response_buffer[max_response_length];

    int send_status = uart_send_and_block(command_buffer, length, response_buffer, max_response_length);

    if (data->message_type == COMMAND) {
        // if msg was a command create a ack msg with the result of the command (and mark as acked ig)
        create_data_object(NO_ID, COMMAND, response_buffer, -1, g_my_address, -1, 0, 0, 0, msg_id);
        data->ack_status = 1;
    }
    ESP_LOGI(TAG, "Response = \"%s\" (code %d) for msg %d",response_buffer, send_status, msg_id);

    data->transfer_status = send_status;

    return send_status;
}


static MessageSendingStatus uart_send_and_block(char *cmd, size_t length, char *resp_buffer, size_t max_resp_length) {
    if (length == 0) {
        length = strlen(cmd);
    }

    // 1) Flush stale responses
    char junk[256];
    while (xQueueReceive(q_resp, junk, 0) == pdTRUE) {
        ESP_LOGW(TAG, "Flushing stale line from response queue: \"%s\"",junk);
    }

    // 2) Send command

    // for (int i = 0; i < length; i++) {
    //     char ch = cmd[i];
    //     printf("char: 0x%02X '%c'\n",
    //         ch,
    //         (ch >= 32 && ch <= 126) ? ch : '.');
    // }

    uart_write_bytes(UART_PORT, cmd, length);

    // 3) Collect lines
    char line[256];
    size_t resp_len = 0;
    bool got_any = false;
    MessageSendingStatus status = OK;

    const int overall_timeout_ms  = 2000;
    const int interline_timeout_ms = 100;

    TickType_t start    = xTaskGetTickCount();
    TickType_t deadline = start + pdMS_TO_TICKS(overall_timeout_ms);
    TickType_t last_line_time = 0;

    for (;;) {
        TickType_t now = xTaskGetTickCount();

        // hard overall timeout
        if (now >= deadline) {
            if (resp_len > 0) resp_buffer[resp_len] = '\0';
            return got_any ? status : NO_STATUS;
        }

        if (got_any && (now - last_line_time > pdMS_TO_TICKS(interline_timeout_ms))) {
            if (resp_len > 0) resp_buffer[resp_len] = '\0';
            return status;
        }

        TickType_t wait = pdMS_TO_TICKS(interline_timeout_ms);
        if (now + wait > deadline) {
            wait = deadline - now;
        }

        if (xQueueReceive(q_resp, line, wait) != pdTRUE) {
            continue;
        }

        now = xTaskGetTickCount();
        last_line_time = now;
        got_any = true;

        ESP_LOGD(TAG, "Response line: \"%s\"\n", line);

        // -------- status parsing (both 998 + 993) --------

        // 998-style: +ERR=code
        if (!strncmp(line, "+ERR", 4)) {
            int err;
            if (sscanf(line, "+ERR=%d", &err) == 1) {
                status = err;
            } else {
                status = ERR;
            }
        }

        else if (!strncmp(line, "+OK", 3) || !strncmp(line, "OK",2)) {
            status = OK;
        }

        // 993-style: AT_FOO_ERROR
        else if (!strncmp(line, "AT_", 3) && strstr(line, "_ERROR") != NULL) {
            status = ERR;
        }

        // -------- accumulate all lines into resp_buffer --------

        size_t line_len = strlen(line);
        const char *sep = "<br>";
        size_t sep_len = got_any && resp_len > 0 ? strlen(sep) : 0;

        if (resp_len + sep_len + line_len + 1 < max_resp_length) {
            if (sep_len > 0) {
                memcpy(resp_buffer + resp_len, sep, sep_len);
                resp_len += sep_len;
            }
            memcpy(resp_buffer + resp_len, line, line_len);
            resp_len += line_len;
            resp_buffer[resp_len] = '\0';
        }
    }
}


void queue_send(ID msg_id, ID target, bool use_router) {
    DataEntry *data = msg_find(msg_id);
    ID final_target = target;
    if (use_router) {
        router_print(g_this_node->router);
        final_target = router_query_intermediate(g_this_node->router, target);
        printf("ROUTER: sending msg (%hu) to %hu as intermediate to %hu\n",msg_id, final_target, target);
        if (final_target == NO_ID) {
            printf("ERROR ROUTER CANNOT RESOLVE WHERE TO SEND MSG: %hu\n",msg_id);
            return; // fix this
        }

    }
    data->target_node = final_target;
    data->transfer_status = QUEUED;
    xQueueSend(MessageQueue, &msg_id, pdMS_TO_TICKS(50));
}


void uart_init(void) {
    q_rcv  = xQueueCreate(16, 256);
    q_resp = xQueueCreate(16, 256);

    uart_config_t uart_config = {
        .baud_rate = BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_READ_BUFF, UART_WRITE_BUFF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(uart_reader_task, "uart_reader_task", 4096, NULL, 10, NULL);
    xTaskCreate(rcv_handler_task, "rcv_reader_task", 4096, NULL, 10, NULL);

    MessageQueue = xQueueCreate(16, sizeof(ID));
}

static void rcv_handler_task(void *arg) {
    char line[256];
    for (;;) {
        if (xQueueReceive(q_rcv, line, portMAX_DELAY) == pdTRUE) {
            int len,step,msg_type,rssi,snr;
            ID from, origin, dest, id, ack_for;
            char data[256];
            if (parse_rcv_line(line, &from, &len, data, sizeof(data), &origin, &dest, &step, &msg_type, &id, &ack_for, &rssi, &snr)) {
                // from the last node to receiving at this node is a step
                step +=1;

                router_update(g_this_node->router, origin, dest, from, step);


                // check to see if id already exists.
                // only create if NEW
                // handle this diffrently lowkey, if you re-receive a message do somthing else
                DataEntry *existing = msg_find(id);
                ID rcv_msg_id;

                bool should_handle = true;
                if (existing) {
                    // if its a gbcast we want to eliminate dups
                    if ((existing->message_type == MAINTENANCE) && (strncmp("gbcast",existing->content,7) == 0)) {
                        // this gbcast msg has already been heard
                        printf("GBcast already received here");
                        should_handle = false;
                    }
                    printf("msg with id=%d already exists.\n\tExisting content = \"%s\"\n\tNew content = \"%s\"\n",id, existing->content, data);
                    rcv_msg_id = existing->id;
                } else {
                    rcv_msg_id = create_data_object(id, msg_type, data, from, dest, origin, step, rssi, snr, ack_for);
                }

                // update node given newest message
                nodes_update(rcv_msg_id);

                // if a duplicate is not forbiden
                if (should_handle) {

                    if (msg_type == MAINTENANCE) {
                        handle_maintenance_msg(rcv_msg_id);

                    }

                    // im switching from msg_type == ACK to check to see if msg has ack_for
                    if (ack_for != NO_ID) {
                        DataEntry *acked_msg = msg_find(ack_for);
                        // mark it as acked because it is
                        acked_msg->ack_status = 1;

                        if (dest != g_my_address) {
                            // msg went from src -> dst. but now we wanna send to src
                            queue_send(id, acked_msg->src_node, true);
                        }
                        // if msg is an ACK
                        // the goal is to send it along the path it came
                    }  

                    // create ack if msg of type and at destination
                    // if (dest == g_address.i_addr) {
                    //     char msg_id_buff[32];
                    //     snprintf(msg_id_buff, 32, "ack msg for %d", id);
                    //     ID ack_id = create_data_object(NO_ID, ACK, msg_id_buff , g_address.i_addr, origin, g_address.i_addr, 0, 0, 0, id);
                    //     queue_send(ack_id, from);
                    // }
                }
            } else {
                printf("UART PARSE FAIL: '%s'\n", line);
            }
        }
    }
}


static void uart_reader_task(void *arg) {
    ESP_LOGI(TAG, "UART READER INIT\n");
    char line[256];
    int len = 0;
    bool saw_cr = false;

    for (;;) {
        uint8_t ch;
        int n = uart_read_bytes(UART_PORT, &ch, 1, pdMS_TO_TICKS(50));
        if (n <= 0) continue;

        // printf("TX char: 0x%02X '%c'\n",
        //     ch,
        //     (ch >= 32 && ch <= 126) ? ch : '.');

        if (len == 0 && ((ch == '\r') || (ch == '\n')) ) {
            printf("Ditching char either '\\r' or '\\n'\n");
            // this is an attempt to remove extra \n or \r being sent after a response is created
            // if the \r\n already terminated the message any tailing \r or \n will be forgotten bc new msg should start with '+'
            continue;
        }
        if (ch == '\r') { saw_cr = true; continue; }
        if (saw_cr && ch == '\n') {
            line[len] = '\0';

            if (strncmp(line, "+RCV=", 5) == 0) {
                xQueueSend(q_rcv, line, 0);
            } else {
                xQueueSend(q_resp, line, 0);
            }

            len = 0; saw_cr = false;
            continue;
        }
        saw_cr = false;

        if (len < (int)sizeof(line) - 1) line[len++] = (char)ch;
        else len = 0; // overflow: reset
    }
}

void message_sending_task(void *args) {
    ID msg_id;
    for (;;) {
        if (xQueueReceive(MessageQueue, &msg_id, portMAX_DELAY)) {
            // change this later
            send_message_blocking(msg_id);

            uint32_t jitter_ms = 10 + (esp_random() % 40);
            vTaskDelay(pdMS_TO_TICKS(jitter_ms));
        }
    }

    vTaskDelete(NULL);
}

