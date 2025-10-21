#ifndef DATA_TABLE_H
#define DATA_TABLE_H

#include <time.h>

typedef enum {
    MSG_AT_SOURCE,      // originated here
    MSG_RELAYED,        // passing through (not source, not destination)
    MSG_AT_DESTINATION  // this node is the destination
} MessageRouteStage;


typedef struct data_entry_struct {
    char *content;              // content of message
    int src_node;               // node where message came from last
    int dst_node;               // node where message is trying to be sent
    int origin_node;            // node where message originated
    int steps;                  // nodes visited
    int64_t timestamp;           // timestamp of arrival
    uint16_t id;

    int length;                 // length of message content   
    int rssi;                   // Received Signal Strength Indicator
    int snr;                    // signal to noise ratio
    struct data_entry_struct *next;
    MessageRouteStage stage;
} DataEntry;

DataEntry *create_data_object(char *content, int src, int dst, int origin, int steps, int rssi, int snr);
void free_data_object(DataEntry **ptr);
void table_insert(DataEntry *data);
void msg_table_init(void);
int format_data_as_json(DataEntry *, char *, int);

#endif // DATA_TABLE_H


/*
types of messages
non-critical
critical (needs ack)
broadcast
chunke


*/