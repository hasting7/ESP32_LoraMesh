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
    time_t timestamp;           // timestamp of arrival

    int length;
    int rssi;
    int snr;
    struct data_entry_struct *next;
    MessageRouteStage stage;
} DataEntry;

DataEntry *create_data_object(char *content, int src, int dst, int origin, int steps, int rssi, int snr);
void free_data_object(DataEntry **ptr);
void fmt_time_iso_utc(time_t t, char out[32]);
void table_insert(DataEntry *data);
size_t render_messages_table_html(char *out, size_t out_cap);
void msg_table_init(void);

#endif // DATA_TABLE_H
