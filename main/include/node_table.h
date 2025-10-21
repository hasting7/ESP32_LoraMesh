#ifndef NODE_TABLE_H
#define NODE_TABLE_H

#include <time.h>

typedef struct node_table_entry {
	Address address;
	char *name;
	float avg_rssi;			// avg rssi coming from node to this node
	float avg_snr;			// avg rssi coming from node to this node
	int messages;			// total messages coming from node to this node
	time_t last_connection; // last time received message from node

	struct node_table_entry *next;
} NodeEntry;

extern NodeEntry *g_node_table;

#define EMA_SMOOTHING (0.15)

NodeEntry *get_node_ptr(int);
void node_table_init(void);
NodeEntry *create_node_object(int);
void update_metrics(NodeEntry *node, int rssi, int snr);
int format_node_as_json(NodeEntry *, char *, int);

#endif