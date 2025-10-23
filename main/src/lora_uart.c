#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_random.h"

#include "data_table.h"
#include "node_globals.h"
#include "mesh_config.h"
#include "node_table.h"
#include "lora_uart.h"


static QueueHandle_t MessageQueue;

QueueHandle_t q_resp;
QueueHandle_t q_rcv;

static void uart_reader_task(void *arg);
static void rcv_handler_task(void *arg);

static bool parse_rcv_line(const char *line,
                           int *from, int *len, char *data, size_t data_cap,
                           int *origin, int *dest, int *step, int *msg_typ, int *rssi, int *snr);

// Unified parser:
// +RCV=<from>,<len><sep><DATA>,<rssi>,<snr>
// or
// +RCV=<from>,<len><sep><DATA>,<origin>,<dest>,<step>,<msg_type><rssi>,<snr>
static bool parse_rcv_line(const char *line,
                           int *from, int *len, char *data, size_t data_cap,
                           int *origin, int *dest, int *step, int *msg_type, int *rssi, int *snr)
{
    if (!line || !from || !len || !data || data_cap == 0 ||
        !origin || !dest || !step || !rssi || !snr || !msg_type) {
        return false;
    }

    if (strncmp(line, "+RCV=", 5) != 0) return false;
    const char *p = line + 5;
    char *end = NULL;

    // from
    long f = strtol(p, &end, 10);
    if (end == p || *end != ',') return false;
    p = end + 1;

    // len
    long l = strtol(p, &end, 10);
    if (end == p) return false;
    if (*end != ',' && *end != ':') return false;   // allow ',' or ':'
    p = end + 1;

    if (l < 0) return false;

    // Ensure at least len bytes for DATA
    size_t remain = strlen(p);
    if ((size_t)l > remain) return false;

    // Copy exactly len bytes of DATA (may contain commas)
    int data_len = (int)l;
    size_t copy = (size_t)data_len < (data_cap - 1) ? (size_t)data_len : (data_cap - 1);
    memcpy(data, p, copy);
    data[copy] = '\0';
    p += data_len;

    // After DATA must be a comma then the tail integers
    if (*p != ',') return false;
    p++;

    // Parse trailing ints (RSSI,SNR or origin,dest,step,msg_type,RSSI,SNR)
    long tail[6];
    int tcount = 0;
    for (; tcount < 6; tcount++) {
        tail[tcount] = strtol(p, &end, 10);
        if (end == p) break;
        if (*end == ',') p = end + 1;
        else { p = end; tcount++; break; }
    }

    // Case 1: all metadata outside DATA (origin,dest,step,msg_type,rssi,snr)
    if (tcount == 6) {
        *origin   = (int)tail[0];
        *dest     = (int)tail[1];
        *step     = (int)tail[2];
        *msg_type = (int)tail[3];
        *rssi     = (int)tail[4];
        *snr      = (int)tail[5];
    }
    // Case 2: only rssi,snr after DATA; parse last 4 tokens from DATA
    else if (tcount == 2) {
        *rssi = (int)tail[0];
        *snr  = (int)tail[1];

        // Extract the last four comma-separated tokens from DATA
        // DATA format: "<content>,<origin>,<dest>,<step>,<msg_type>"
        int commas_found = 0;
        int pos[4] = {-1,-1,-1,-1};
        for (int i = (int)strlen(data) - 1; i >= 0 && commas_found < 4; --i) {
            if (data[i] == ',') pos[commas_found++] = i;
        }
        if (commas_found < 4) return false; // not enough fields at end

        // Positions from right to left:
        // pos[0] = last comma before msg_type
        // pos[1] = before step
        // pos[2] = before dest
        // pos[3] = before origin  (everything before this is content)

        char *endptr;
        long v_msg_type = strtol(&data[pos[0] + 1], &endptr, 10);
        if (*endptr != '\0') return false;

        data[pos[0]] = '\0';
        long v_step = strtol(&data[pos[1] + 1], &endptr, 10);
        if (*endptr != '\0') return false;

        data[pos[1]] = '\0';
        long v_dest = strtol(&data[pos[2] + 1], &endptr, 10);
        if (*endptr != '\0') return false;

        data[pos[2]] = '\0';
        long v_origin = strtol(&data[pos[3] + 1], &endptr, 10);
        if (*endptr != '\0') return false;

        // Now data[pos[3]] is a comma; set it to '\0' to leave only <content> in data
        data[pos[3]] = '\0';

        *origin   = (int)v_origin;
        *dest     = (int)v_dest;
        *step     = (int)v_step;
        *msg_type = (int)v_msg_type;
    }
    else {
        return false; // unsupported tail shape
    }

    *from = (int)f;
    *len  = (int)l;
    return true;
}


LoraInstruction construct_command(Command cmd, const char *args[], int n) {
    char cmd_buffer[256] = "AT+";
    int len = 3;

    int expect_args = -1;
    const char *name = NULL;

    if (cmd == SEND)        { expect_args = 3; name = "SEND"; }
    else if (cmd == ADDRESS){ expect_args = 1; name = "ADDRESS"; }
    else if (cmd == RESET)  { expect_args = 0; name = "RESET"; }
    else if (cmd == PARAMETER){ expect_args = 4; name = "PARAMETER"; }
    else if (cmd == BAND)   { expect_args = 2; name = "BAND"; }
    else if (cmd == NETWORKID){ expect_args = 1; name = "NETWORKID"; }
    else if (cmd == FACTORY){ expect_args = 0; name = "FACTORY"; }
    else if (cmd == CRFOP)  { expect_args = 1; name = "CRFOP"; }

    if (expect_args < 0 || n != expect_args) {
        printf("%s cmd should have %d args not %d\n", name ? name : "(unknown)", expect_args, n);
        return NULL;
    }

    // Append name
    for (int i = 0; name[i] != '\0' && len < (int)sizeof(cmd_buffer) - 1; i++) {
        cmd_buffer[len++] = name[i];
    }

    if (expect_args != 0) {
        if (len < (int)sizeof(cmd_buffer) - 1) cmd_buffer[len++] = '=';
        for (int i = 0; i < n; i++) {
            for (int l = 0; args[i][l] != '\0' && len < (int)sizeof(cmd_buffer) - 1; l++) {
                cmd_buffer[len++] = args[i][l];
            }
            if (len < (int)sizeof(cmd_buffer) - 1) cmd_buffer[len++] = ',';
        }
        if (cmd_buffer[len-1] == ',') len--; // remove last comma
    }

    if (len < (int)sizeof(cmd_buffer) - 1) cmd_buffer[len++] = '\r';
    if (len < (int)sizeof(cmd_buffer) - 1) cmd_buffer[len++] = '\n';
    cmd_buffer[len++] = '\0';

    char *ret = (char*)malloc(len);
    if (!ret) return NULL;
    memcpy(ret, cmd_buffer, len);
    return ret;
}


int send_message_blocking(DataEntry *data, int to_address) {
    // Build message payload: "<content>,<origin>,<dest>,<steps>,<msg_type>"
    char cmd[512];
    int payload_len = snprintf(cmd, sizeof(cmd), "%s,%d,%d,%d,%d",
                               data->content, data->origin_node, data->dst_node, data->steps,data->message_type);
    if (payload_len < 0) return -1;
    if (payload_len >= (int)sizeof(cmd)) payload_len = sizeof(cmd) - 1;

    char to_address_s[12];
    char len_s[12];
    snprintf(to_address_s, sizeof(to_address_s), "%d", to_address);
    snprintf(len_s, sizeof(len_s), "%d", payload_len);

    LoraInstruction instruction =
        construct_command(SEND, (const char *[]) {to_address_s, len_s, cmd}, 3);

    if (!instruction) return -1;

    printf("Sending: %s\n", instruction);
    int resp = uart_send_and_block(instruction);

    printf("Response code: %d\n", resp);
    data->transfer_status = resp;

    free(instruction);
    return resp;
}

MessageSendingStatus uart_send_and_block(LoraInstruction cmd) {
    uart_write_bytes(UART_PORT, cmd, strlen(cmd));

    char line[256];
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);

    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) return NO_STATUS;

        if (xQueueReceive(q_resp, line, deadline - now) == pdTRUE) {
            printf("Response: %s\n",line);
            if (strncmp(line, "+OK", 3) == 0) return SENT;
            int err;
            if (sscanf(line, "+ERR=%d", &err) == 1) return err;
            // Ignore unrelated noise; keep waiting until +OK/+ERR or timeout
        }
    }
}


void queue_send(DataEntry *data) {
    xQueueSend(MessageQueue, &data, pdMS_TO_TICKS(50));
    data->transfer_status = QUEUED;
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

    // pattern detection
    // ESP_ERROR_CHECK(uart_enable_pattern_det_baud_intr(UART_PORT, '\n', 1, 9, 0, 0));
    // ESP_ERROR_CHECK(uart_pattern_queue_reset(UART_PORT, 10));
    xTaskCreate(uart_reader_task, "uart_reader_task", 4096, NULL, 10, NULL);
    xTaskCreate(rcv_handler_task, "rcv_reader_task", 4096, NULL, 10, NULL);

    MessageQueue = xQueueCreate(16, sizeof(DataEntry *));
}

static void rcv_handler_task(void *arg) {
    char line[256];
    for (;;) {
        if (xQueueReceive(q_rcv, line, portMAX_DELAY) == pdTRUE) {
            int from,len,origin,dest,step,msg_type,rssi,snr;
            char data[256];
            if (parse_rcv_line(line, &from, &len, data, sizeof(data),
                               &origin, &dest, &step, &msg_type, &rssi, &snr)) {
                NodeEntry *node = get_node_ptr(origin);
                if (!node) node = create_node_object(origin);
                time(&node->last_connection);
                update_metrics(node, rssi, snr);
                node->reachable = 1;

                // step + 1 per your original behavior
                create_data_object(msg_type,data, from, dest, origin, step + 1, rssi, snr);

                // create ack if msg of type and at destination
                if ((msg_type == NORMAL) && (dest == g_address.i_addr)) {
                    char msg_id_buff[6];
                    // fix and make msg send id with it and return id
                    snprintf(msg_id_buff, 6, "ID%d", 1);
                    DataEntry *ack = create_data_object(ACK, msg_id_buff, g_address.i_addr, origin, g_address.i_addr, 0, 0, 0);
                    queue_send(ack);
                }
            } else {
                printf("UART PARSE FAIL: '%s'\n", line);
            }
        }
    }
}

static void uart_reader_task(void *arg) {
    char line[256];
    int len = 0;
    bool saw_cr = false;

    for (;;) {
        uint8_t ch;
        int n = uart_read_bytes(UART_PORT, &ch, 1, pdMS_TO_TICKS(50));
        if (n <= 0) continue;

        if (ch == '\r') { saw_cr = true; continue; }
        if (saw_cr && ch == '\n') {
            line[len] = '\0';

            // Demux once here:
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
    DataEntry *data;
    for (;;) {
        if (xQueueReceive(MessageQueue, &data, portMAX_DELAY)) {
            // change this later
            send_message_blocking(data, data->dst_node);

            uint32_t jitter_ms = 10 + (esp_random() % 40);
            vTaskDelay(pdMS_TO_TICKS(jitter_ms));
        }
    }

    vTaskDelete(NULL);
}
