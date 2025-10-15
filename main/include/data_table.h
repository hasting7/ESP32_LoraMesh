#ifndef DATA_TABLE_H
#define DATA_TABLE_H

#include <time.h>

typedef struct data_entry_struct {
    char *content;              // content of message
    int src_node;               // node where message came from last
    int dst_node;               // node where message is trying to be sent
    int origin_node;            // node where message originated
    int steps;                  // nodes visited
    time_t timestamp;           // timestamp of arrival
    struct data_entry_struct *next;
} DataEntry;

DataEntry *create_data_object(char *content, int src, int dst, int origin, int steps);
void free_data_object(DataEntry **ptr);
void fmt_time_iso_utc(time_t t, char out[32]);
void table_insert(DataEntry **head, DataEntry *data);
void view_table(DataEntry *head);

#endif // DATA_TABLE_H
