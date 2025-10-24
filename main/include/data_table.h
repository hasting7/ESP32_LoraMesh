#ifndef DATA_TABLE_H
#define DATA_TABLE_H

#include <time.h>

typedef enum {
    MSG_AT_SOURCE,      // originated here
    MSG_RELAYED,        // passing through (not source, not destination)
    MSG_AT_DESTINATION  // this node is the destination
} MessageRouteStage;

typedef enum { 
    BROADCAST,
    NORMAL,
    ACK,
    CRITICAL,
    MAINTENANCE,
    PING,
} MessageType;

typedef enum {
    NO_STATUS = -2,
    QUEUED = -1,
    SENT = 0,
    ERR,
} MessageSendingStatus;

#define NO_ID (0)


typedef struct data_entry_struct {
    int64_t timestamp;           // timestamp of arrival
    char *content;              // content of message
    struct data_entry_struct *next;
    int src_node;               // node where message came from last
    int dst_node;               // node where message is trying to be sent
    int origin_node;            // node where message originated
    int target_node;            // node where msg is going next
    int steps;                  // nodes visited
    int length;                 // length of message content   
    int rssi;                   // Received Signal Strength Indicator
    int snr;                    // signal to noise ratio
    MessageSendingStatus transfer_status;        // status coming from lora chip after trying to be sent
    MessageType message_type;   // this is the type of message it is. broadcast, normal, critical, maintnace, etc
    MessageRouteStage stage;

    uint16_t id;                // unique id of message
    int ack_status;              // if it is a message requiring ack, did it get one?

} DataEntry;

DataEntry *create_data_object(int id, MessageType type, char *content, int src, int dst, int origin, int steps, int rssi, int snr);
void free_data_object(DataEntry **ptr);
void table_insert(DataEntry *data);
void msg_table_init(void);
int format_data_as_json(DataEntry *, char *, int);
DataEntry *get_msg_ptr(int id);

#endif // DATA_TABLE_H


/*
types of messages
non-critical
critical (needs ack)
broadcast
chunke


*/