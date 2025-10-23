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

#include "data_table.h"
#include "node_globals.h"
#include "mesh_config.h"
#include "node_table.h"
#include "lora_uart.h"


static QueueHandle_t uart_queue;

static bool parse_rcv_line(const char *line,
                           int *from, int *len, char *data, size_t data_cap,
                           int *origin, int *dest, int *step, int *rssi, int *snr);


static void uart_event_task(void *arg)
{
    uart_event_t event;

    for (;;) {
        if (!xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
            continue;
        }

        switch (event.type) {
        case UART_PATTERN_DET: {
            int pos = uart_pattern_pop_pos(UART_PORT);
            if (pos < 0) {
                uart_flush_input(UART_PORT);
                break;
            }

            int to_read = pos + 1; // include '\n'
            uint8_t *tmp = (uint8_t *)malloc(to_read + 1);
            if (!tmp) {
                uart_flush_input(UART_PORT);
                break;
            }

            int n = uart_read_bytes(UART_PORT, tmp, to_read, pdMS_TO_TICKS(50));
            if (n > 0) {
                tmp[n] = '\0';
                // Trim trailing CR/LF
                while (n > 0 && (tmp[n - 1] == '\n' || tmp[n - 1] == '\r')) {
                    tmp[--n] = '\0';
                }

                int from=0, len=0, origin=0, dest=0, step=0, rssi=0, snr=0;
                char data[256] = {0};

                if (parse_rcv_line((char *)tmp, &from, &len, data, sizeof(data),
                                   &origin, &dest, &step, &rssi, &snr)) {

                    NodeEntry *node = get_node_ptr(origin);
                    if (!node) node = create_node_object(origin);
                    time(&node->last_connection);
                    update_metrics(node, rssi, snr);

                    // step + 1 per your original behavior
                    create_data_object(data, from, dest, origin, step + 1, rssi, snr);
                } else {
                    printf("UART PARSE FAIL: '%s'\n", (char *)tmp);
                }
            }

            free(tmp);
            break;
        }

        case UART_DATA:
            break;

        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            uart_flush_input(UART_PORT);
            xQueueReset(uart_queue);
            printf("UART overflow, flushed\n");
            break;

        default:
            break;
        }
    }
}

static bool extract_ods_from_data_tail(const char *data, int data_len,
                                       int *origin, int *dest, int *step,
                                       int *msg_len_out)
{
    if (!data || data_len <= 0 || !origin || !dest || !step || !msg_len_out) return false;

    // Find last three commas scanning backward
    int commas_found = 0;
    int idx = data_len - 1;
    int pos[3] = {-1,-1,-1}; // positions of last 3 commas
    while (idx >= 0 && commas_found < 3) {
        if (data[idx] == ',') pos[commas_found++] = idx;
        idx--;
    }
    if (commas_found < 3) return false; // not enough commas to hold O,D,S

    int c0 = pos[0]; // last comma (before step)
    int c1 = pos[1]; // before dest
    int c2 = pos[2]; // before origin

    // Substrings
    const char *s_origin = data + c2 + 1;
    const char *s_dest   = data + c1 + 1;
    const char *s_step   = data + c0 + 1;

    char *endptr;

    long lo = strtol(s_origin, &endptr, 10);
    if (endptr == s_origin || (*endptr != ',' && *endptr != '\0')) return false;

    long ld = strtol(s_dest, &endptr, 10);
    if (endptr == s_dest || (*endptr != ',' && *endptr != '\0')) return false;

    long ls = strtol(s_step, &endptr, 10);
    // allow endptr at end of data
    if (endptr == s_step || (*endptr != '\0')) return false;

    *origin = (int)lo;
    *dest   = (int)ld;
    *step   = (int)ls;
    *msg_len_out = c2; // message ends just before the comma before origin
    return true;
}

// Unified parser:
// +RCV=<from>,<len><sep><DATA>,<rssi>,<snr>
// or
// +RCV=<from>,<len><sep><DATA>,<origin>,<dest>,<step>,<rssi>,<snr>
static bool parse_rcv_line(const char *line,
                           int *from, int *len, char *data, size_t data_cap,
                           int *origin, int *dest, int *step, int *rssi, int *snr)
{
    if (!line || !from || !len || !data || data_cap == 0 ||
        !origin || !dest || !step || !rssi || !snr) {
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
    if (*end != ',' && *end != ':') return false;   // support ',' or ':' separator
    p = end + 1;

    if (l < 0) return false;

    // Ensure we have at least len bytes for DATA
    size_t remain = strlen(p);
    if ((size_t)l > remain) return false; // incomplete line

    // Copy exactly len bytes as DATA (may contain commas)
    int data_len = (int)l;
    size_t copy = (size_t)data_len < (data_cap - 1) ? (size_t)data_len : (data_cap - 1);
    memcpy(data, p, copy);
    data[copy] = '\0';
    p += data_len;

    // After DATA must be a comma then the tail integers
    if (*p != ',') return false;
    p++;

    // Parse up to 5 trailing integers separated by commas
    long tail[5];
    int tcount = 0;
    for (; tcount < 5; tcount++) {
        tail[tcount] = strtol(p, &end, 10);
        if (end == p) break;          // no number
        if (*end == ',') { p = end + 1; }
        else { p = end; tcount++; break; } // reached last token
    }

    // Two supported tail shapes:
    //  (A) 5 ints: origin,dest,step,rssi,snr
    //  (B) 2 ints: rssi,snr    (then extract origin,dest,step from end of DATA)
    if (tcount == 5) {
        *origin = (int)tail[0];
        *dest   = (int)tail[1];
        *step   = (int)tail[2];
        *rssi   = (int)tail[3];
        *snr    = (int)tail[4];
    } else if (tcount == 2) {
        *rssi = (int)tail[0];
        *snr  = (int)tail[1];

        int msg_len = 0, o=0,d=0,s=0;
        if (!extract_ods_from_data_tail(data, data_len, &o, &d, &s, &msg_len)) {
            return false; // cannot recover origin/dest/step
        }
        *origin = o;
        *dest   = d;
        *step   = s;

        // Trim data to the pure "message" (exclude the ",origin,dest,step" suffix)
        if (msg_len >= 0 && msg_len < (int)data_cap) {
            data[msg_len] = '\0';
        }
    } else {
        return false; // unsupported tail
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


int send_message(DataEntry *data, int to_address) {
    // Build message payload: "<content>,<origin>,<dest>,<steps>"
    char cmd[512];
    int payload_len = snprintf(cmd, sizeof(cmd), "%s,%d,%d,%d",
                               data->content, data->origin_node, data->dst_node, data->steps);
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

int uart_send_and_block(LoraInstruction instruction) {
    uart_write_bytes(UART_PORT, instruction, strlen(instruction));

    // Reserve a spare char for '\0'
    uint8_t buffer[64];
    static char response[128];
    int length = 0;

    TickType_t started = xTaskGetTickCount();

    while (1) {
        int n = uart_read_bytes(UART_PORT, buffer, sizeof(buffer) - 1, pdMS_TO_TICKS(300));
        if (n > 0) {
            buffer[n] = '\0';
            for (int i = 0; i < n; i++) {
                if (length < (int)sizeof(response) - 1) {
                    response[length++] = (char)buffer[i];
                }
            }
            if (length >= 2 && response[length - 2] == '\r' && response[length - 1] == '\n') {
                break;
            }
        }
        // Optional timeout (2 seconds)
        if ((xTaskGetTickCount() - started) > pdMS_TO_TICKS(2000)) break;
    }

    response[length] = '\0';

    // Strip trailing CR/LF
    while (length > 0 && (response[length - 1] == '\n' || response[length - 1] == '\r')) {
        response[--length] = '\0';
    }

    printf("Collected Response: '%s'\n", response);

    // Consider success if "+OK" appears anywhere in the line
    if (strncmp(response, "+OK", 3) == 0) return OK;
    // error
    int error_num;
    if (sscanf(response, "+ERR=%d", &error_num) == 1) return error_num;

    return NO_STATUS;
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
