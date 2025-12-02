#ifndef DATA_TABLE_H
#define DATA_TABLE_H

#include <stdint.h>
#include <time.h>

#include "hash_table.h"
#include "node_globals.h"

typedef enum {
    MSG_AT_SOURCE,      // originated here
    MSG_RELAYED,        // passing through (not source, not destination)
    MSG_AT_DESTINATION  // this node is the destination
} MessageRouteStage;

typedef enum { // 15 max
    BROADCAST = 1,
    NORMAL,
    ACK,
    CRITICAL,
    MAINTENANCE,
    PING,
    COMMAND
} MessageType;

typedef enum {
    NO_STATUS = -2,
    QUEUED = -1,
    OK = 0,
    ERR,
} MessageSendingStatus;

#define NO_ID (0)

typedef struct data_entry_struct {
    time_t timestamp;           // timestamp of arrival
    char *content;               // content of message
    ID src_node;                 // node where message came from last
    ID dst_node;                 // node where message is trying to be sent
    ID origin_node;              // node where message originated
    ID target_node;              // node where msg is going next

    int steps;                   // nodes visited
    int length;                  // length of message content
    int rssi;                    // Received Signal Strength Indicator
    int snr;                     // signal to noise ratio
    MessageSendingStatus transfer_status;        // status coming from lora chip after trying to be sent
    MessageType message_type;    // this is the type of message it is. broadcast, normal, critical, maintnace, etc
    MessageRouteStage stage;

    ID id;                // unique id of message
    int ack_status;       // if it is a message requiring ack, did it get one?
    ID ack_for;           // if 0 then msg is not an ack, if non-zero then msg is meant to ack an existing node (msg_id = ack_for)

} DataEntry;

ID create_command(char *content);
ID create_data_object(int id, MessageType type, char *content, int src, int dst, int origin, int steps, int rssi, int snr, ID ack_for);
void free_data_object(DataEntry **ptr);
void msg_table_init(void);
int format_data_as_json(DataEntry *, char *, int);

#endif // DATA_TABLE_H
