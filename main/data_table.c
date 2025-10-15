
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "structs.h"
#include "functions.h"


// create data entry
// delete data entry
// visualize data entry
// insert to table



DataEntry *create_data_object(char *content, int src, int dst, int origin, int steps) {
	time_t timestamp;
	time(&timestamp);

	DataEntry *new = malloc(sizeof(DataEntry));

	int len = strlen(content);
	char *content_ptr = calloc(len + 1, sizeof(char));
	strcpy(content_ptr, content);
	content_ptr[len] = '\0';

	new->content = content_ptr;
	new->src_node = src;
	new->dst_node = dst;
	new->origin_node = origin;
	new->steps = steps;
	new->timestamp = timestamp;
	new->next = NULL;

	return new;
}

void free_data_object(DataEntry **ptr) {
	if (!ptr || !*ptr) return;
	DataEntry *root = *ptr;
	free(root->content);
	free(root);
	*ptr = NULL;
}

static inline int is_after(time_t a, time_t b) {
    return difftime(a, b) > 0.0;
}

void fmt_time_iso_utc(time_t t, char out[32]) {
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

void table_insert(DataEntry **head, DataEntry *data) {
    if (!head || !data) return;
    data->next = NULL;

    DataEntry **pp = head;
    while (*pp && !is_after(data->timestamp, (*pp)->timestamp)) {
        pp = &(*pp)->next;
    }

    // Insert here
    data->next = *pp;
    *pp = data;
}


void view_table(DataEntry *head) {
	DataEntry *walk = head;
	while (walk) {
		char buffer[32];
		fmt_time_iso_utc(walk->timestamp, buffer);
		printf("%s ) %s\n",buffer, walk->content);
		walk = walk->next;
	}
}

// int main() {
// 	DataEntry* a = create_data_object("hello world", 0, 0, 0, 0);
// 	for (int i = 0; i < 1000000000; i++) {}
// 	DataEntry* b = create_data_object("ben hastings", 0, 0, 0, 0);

// 	DataEntry* table = NULL;

// 	table_insert(&table, a);
// 	table_insert(&table, b);

// 	view_table(table);


// }
