#include "lora_uart.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "mesh_config.h"
#include "node_globals.h"
#include "node_table.h"


static QueueHandle_t MessageQueue;

QueueHandle_t q_resp;
QueueHandle_t q_rcv;

static void uart_reader_task(void *arg);
static void rcv_handler_task(void *arg);

static bool parse_rcv_line(const char *line,
                           ID *from, int *len, char *data, size_t data_cap,
                           ID *origin, ID *dest, int *step, int *msg_type, ID *id, ID *ack_for,
                           int *rssi, int *snr)
{
    if (!line || !from || !len || !data || data_cap == 0 ||
        !origin || !dest || !step || !msg_type || !id || !rssi || !snr) {
        return false;
    }

    // Prefix
    if (strncmp(line, "+RCV=", 5) != 0) return false;
    const char *p = line + 5;
    char *end = NULL;

    // <from>
    long f = strtol(p, &end, 10);
    if (end == p || *end != ',') return false;
    p = end + 1;

    // <len>
    long l = strtol(p, &end, 10);
    if (end == p || *end != ',') return false;
    if (l < 0) return false;
    p = end + 1;


    int data_len = (int)l;
    size_t to_copy = (data_cap > 0 && (size_t)data_len >= data_cap) ? (data_cap - 1) : (size_t)data_len;
    if (data_cap == 0) return false;
    memcpy(data, p, to_copy);
    data[to_copy] = '\0';          // safe sentinel for any incidental string use
    p += data_len;                 // advance past DATA in the input string

    // Next must be comma before RSSI/SNR
    if (*p != ',') return false;
    p++;

    // <rssi> (integer)
    long rssi_l = strtol(p, &end, 10);
    if (end == p || *end != ',') return false;
    p = end + 1;

    // <snr> (often float; parse as double then cast to int field you requested)
    double snr_d = strtod(p, &end);
    if (end == p || *end != '\0') return false;

    // // Need at least 7 bytes: 2(origin) + 2(dest) + 1(step+type) + 2(id)
    // if (data_len < 7) return false;

    const uint8_t *bytes = (const uint8_t *)data;
    size_t idx = (size_t)data_len;

    uint8_t ack_hi = bytes[--idx];
    uint8_t ack_lo = bytes[--idx];
    idx -= 1; // ','
    *ack_for = (ID)(((uint16_t)ack_hi << 8) | ack_lo);

    uint8_t id_hi = bytes[--idx];
    uint8_t id_lo = bytes[--idx];
    idx -= 1; // ','
    *id = (ID)(((uint16_t)id_hi << 8) | id_lo);

    uint8_t packed = bytes[--idx];
    idx -= 1; // ','
    *step     = (int)((packed >> 4) & 0x7);
    *msg_type = (int)(packed & 0xF);

    uint8_t dest_hi = bytes[--idx];
    uint8_t dest_lo = bytes[--idx];
    idx -=1; // ','
    *dest = (ID)(((uint16_t)dest_hi << 8) | dest_lo);

    uint8_t orig_hi = bytes[--idx];
    uint8_t orig_lo = bytes[--idx];
    idx -= 1;
    *origin = (ID)(((uint16_t)orig_hi << 8) | orig_lo);

    // Outputs
    *from = (ID)f;
    *len  = (int)l;
    *rssi = (int)rssi_l;
    *snr  = (int)(snr_d);
    data[idx] = '\0';


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


int send_message_blocking(ID msg_id) {

    DataEntry *data = hash_find(g_msg_table, msg_id);
    ID to_address = data->target_node;
    if (!data) {
        printf("msg with ID: %d DNE\n", msg_id);
    }

    //          using string              4bytes  4bytes 1bytes    1 byte     4 bytes (meta data size = 14)
    // using uint16_t (two chars)         2bytes  2bytes       1byte        2 bytes (meta data size = 7) saving 7 bytes
    // Build message payload: "<content>,<origin>,<dest>,<steps>,<msg_type>,<id>,<ack_for_id>"
    // char cmd[512];
    uint8_t cmd[512];
    int cmd_len = 0;

    // add in message first
    for (char *c = data->content; *c ; c++) {
        cmd[cmd_len++] = *c;
    }
    cmd[cmd_len++] = ',';

    // add in origin ID

    // convert origin to an array of uint8_t and add byte at a time
    uint8_t *origin = (uint8_t *) &data->origin_node;
    cmd[cmd_len++] = origin[0];
    cmd[cmd_len++] = origin[1];

    cmd[cmd_len++] = ',';
    // add in destination ID

    uint8_t *dest = (uint8_t *) &data->dst_node;
    cmd[cmd_len++] = dest[0];
    cmd[cmd_len++] = dest[1];

    cmd[cmd_len++] = ',';
    // merge msg type and steps into one byte
    uint8_t steps = (data->steps) > 7 ? 7 : data->steps; // clamp to 3 bits max
    // message type should be less that 15

    // upper 4 bits for steps lower 4 for type
    uint8_t single_byte_data = ((steps & 0x7) << 4) | (data->message_type & 0xF);
    cmd[cmd_len++] = single_byte_data;


    cmd[cmd_len++] = ',';
    // add in id

    uint8_t *this_id = (uint8_t *) &data->id;
    cmd[cmd_len++] = this_id[0];
    cmd[cmd_len++] = this_id[1];
    cmd[cmd_len++] = ',';

    // add ack_for_id
    uint8_t *ack_id = (uint8_t *) &data->ack_for;
    cmd[cmd_len++] = ack_id[0];
    cmd[cmd_len++] = ack_id[1];

    cmd[cmd_len] = '\0';

    printf("msg content: ");
    for (int i = 0 ; i < cmd_len ; i++) {
        printf("%c",cmd[i]);
    }
    printf("\n");

    // int payload_len = snprintf(cmd, sizeof(cmd), "%s,%d,%d,%d,%d,%d",
    //                            data->content, data->origin_node, data->dst_node, data->steps,data->message_type,data->id);
    // if (payload_len < 0) return -1;
    // if (payload_len >= (int)sizeof(cmd)) payload_len = sizeof(cmd) - 1;

    char to_address_s[12];
    char len_s[12];
    snprintf(to_address_s, sizeof(to_address_s), "%d", to_address);
    snprintf(len_s, sizeof(len_s), "%d", cmd_len);

    char final_cmd_buffer[512] = "AT+SEND=";
    int final_len = 8;

    for (char *c = to_address_s; *c; c++) {
        final_cmd_buffer[final_len++] = *c;
    }
    final_cmd_buffer[final_len++] = ',';

    for (char *c = len_s; *c; c++) {
        final_cmd_buffer[final_len++] = *c;
    }
    final_cmd_buffer[final_len++] = ',';
    for (int i = 0 ; i < cmd_len ; i++) {
        final_cmd_buffer[final_len++] = cmd[i];
    }
    final_cmd_buffer[final_len++] = '\r';
    final_cmd_buffer[final_len++] = '\n';

    printf("SENDING: ");
    for (int i = 0 ; i < final_len ; i++) {
        printf("%c",final_cmd_buffer[i]);
    }
    printf("\n");

    int resp = uart_send_and_block((LoraInstruction) final_cmd_buffer, final_len);

    printf("Response code: %d\n", resp);
    data->transfer_status = resp;

    return resp;
}

MessageSendingStatus uart_send_and_block(LoraInstruction cmd, size_t length) {
    if (length == 0) {
        length = strlen(cmd);
    }
    uart_write_bytes(UART_PORT, cmd, length);

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


void queue_send(ID msg_id, int target) {
    DataEntry *data = hash_find(g_msg_table, msg_id);
    data->target_node = target;
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

void handle_maintenance_msg(ID msg_id) {
    DataEntry *respond_to_msg = hash_find(g_msg_table, msg_id);
    printf("MAINTENANCE msg handling for ID=%d : \"%s\"\n", respond_to_msg->id, respond_to_msg->content);
    // make sure msg is either broadcasted, or meant for this node
    // also check to make sure message has not already been received ******* DO THIS LATER

    if ((respond_to_msg->dst_node != 0) && (respond_to_msg->dst_node != g_address.i_addr)) {
        printf("msg %d not for this node\n",respond_to_msg->id);
        return;
    }

    // buffer for message to send back
    char buffer[240];
    int len = 1;

    // branch based on what kinda maintenace
    if (strncmp(respond_to_msg->content, "discovery", 9)) {
        // this is asking about all the nodes on the network
        // respond back with node_id,...,node_id
        NodeEntry *node = g_node_table;
        uint8_t *node_addr_arr;
        uint8_t node_count = 0;
        while (node) {
            node_addr_arr = (uint8_t *) &node->address.s_addr;
            buffer[len++] = node_addr_arr[0];
            buffer[len++] = node_addr_arr[1];
            node_count++;
            node = node->next;
        }
        buffer[0] = node_count;
        buffer[len] = '\0';
    }

    ID response_msg = create_data_object(NO_ID, ACK, buffer, g_address.i_addr, respond_to_msg->origin_node, g_address.i_addr, 0, 0, 0, msg_id);
    queue_send(response_msg, respond_to_msg->origin_node);
}

static void rcv_handler_task(void *arg) {
    char line[256];
    for (;;) {
        if (xQueueReceive(q_rcv, line, portMAX_DELAY) == pdTRUE) {
            int len,step,msg_type,rssi,snr;
            ID from, origin, dest, id, ack_for;
            char data[256];
            if (parse_rcv_line(line, &from, &len, data, sizeof(data), &origin, &dest, &step, &msg_type, &id, &ack_for, &rssi, &snr)) {

                // i should be marking the origin node and the src node as active
                NodeEntry *origin_node = get_node_ptr(origin);
                if (!origin_node) origin_node = create_node_object(origin);

                NodeEntry *src_node = get_node_ptr(from);
                if (!src_node) src_node = create_node_object(from);

                time(&origin_node->last_connection);
                time(&src_node->last_connection);
                update_metrics(src_node, rssi, snr);
                origin_node->status = ALIVE;
                src_node->status = ALIVE;
                // misses not really used rn
                origin_node->misses = 0;
                src_node->misses = 0;



                // check to see if id already exists.
                // only create if NEW
                // handle this diffrently lowkey, if you re-receive a message do somthing else
                DataEntry *existing = hash_find(g_msg_table, id);
                ID rcv_msg_id;
                if (existing) {
                    printf("msg with id=%d already exists.\n\tExisting content = \"%s\"\n\tNew content = \"%s\"\n",id, existing->content, data);
                    rcv_msg_id = existing->id;
                } else {
                    rcv_msg_id = create_data_object(id, msg_type, data, from, dest, origin, step + 1, rssi, snr, NO_ID);
                }

                if (msg_type == MAINTENANCE) {
                    handle_maintenance_msg(rcv_msg_id);

                }

                if (msg_type == ACK) {
                    DataEntry *acked_msg = hash_find(g_msg_table, ack_for);
                    // mark it as acked because it is
                    acked_msg->ack_status = 1;

                    if (dest != g_address.i_addr) {
                        // msg went from src -> dst. but now we wanna send to src
                        queue_send(id, acked_msg->src_node);
                    }
                    // if msg is an ACK
                    // the goal is to send it along the path it came
                }  

                // create ack if msg of type and at destination
                if (((msg_type == CRITICAL) || (msg_type == PING)) && (dest == g_address.i_addr)) {
                    char msg_id_buff[32];
                    // fix and make msg send id with it and return id
                    snprintf(msg_id_buff, 32, "ack msg for %d", id);
                    ID ack_id = create_data_object(NO_ID, ACK, msg_id_buff , g_address.i_addr, origin, g_address.i_addr, 0, 0, 0, id);
                    queue_send(ack_id, from);
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
            for (int i = 0; i < len; i++) {
                printf("%c",line[i]);
            }
            printf("\n");
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

void ping_suspect_node(void *args) {
    NodeEntry *node = (NodeEntry *)args;
    if (!node) { vTaskDelete(NULL); return; }

    DataEntry *ping_msg = hash_find(g_msg_table, node->ping_id);
    if (!ping_msg) { 
        printf("no ping msg for node %d\n",node->address.i_addr);
        vTaskDelete(NULL); return; 
    }

    ping_msg->ack_status = 0;

    int delay = 1;
    bool success = false;

    for (int i = 0; i < 4; i++) {
        // send ping
        queue_send(node->ping_id, node->address.i_addr);

        vTaskDelay(pdMS_TO_TICKS(delay * 1000));

        delay <<= 1;

        if (ping_msg->ack_status) {
            success = true;
            break;
        } else {
            node->misses += 1;
        }
    }

    node->status = success ? ALIVE : DEAD;
    node->ping_task = NULL;

    vTaskDelete(NULL);
}

void node_status_task(void *args) {
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        NodeEntry *node = g_node_table;
        while (node) {
            int delta = difftime(time(NULL), node->last_connection);
            if (delta <= REQUEST_STATUS_TIME) {
                // last connection was within the minute
                node->status = ALIVE;
                node->misses = 0;
            } else if ((delta > REQUEST_STATUS_TIME) && (node->status == ALIVE)) {
                printf("Node (%d) is suspected to be dead. pinging...\n",node->address.i_addr);
                // it was alive but last connection was over a minute ago.
                // mark as suspect then ping_suspect_node
                node->status = UNKNOWN;
                if (node->ping_task == NULL) {
                    xTaskCreate(
                        ping_suspect_node,
                        "pingSuspect",
                        2048,                // stack size
                        (void*)node,         // pvParameters
                        tskIDLE_PRIORITY+1,  // priority
                        &node->ping_task     // save handle so we don’t double-create
                    );
                }
            }
            node = node->next;
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(15 * 1000));   // wait 15 seconcds
    }
}
