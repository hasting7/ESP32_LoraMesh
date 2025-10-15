#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "data_table.h"

DataEntry *create_data_object(char *content, int src, int dst, int origin, int steps)
{
    time_t timestamp;
    time(&timestamp);

    DataEntry *new_entry = malloc(sizeof(DataEntry));
    if (!new_entry) {
        return NULL;
    }

    size_t len = strlen(content);
    char *content_ptr = calloc(len + 1, sizeof(char));
    if (!content_ptr) {
        free(new_entry);
        return NULL;
    }
    memcpy(content_ptr, content, len);
    content_ptr[len] = '\0';

    new_entry->content = content_ptr;
    new_entry->src_node = src;
    new_entry->dst_node = dst;
    new_entry->origin_node = origin;
    new_entry->steps = steps;
    new_entry->timestamp = timestamp;
    new_entry->next = NULL;

    return new_entry;
}

void free_data_object(DataEntry **ptr)
{
    if (!ptr || !*ptr) {
        return;
    }
    DataEntry *root = *ptr;
    free(root->content);
    free(root);
    *ptr = NULL;
}

static inline int is_after(time_t a, time_t b)
{
    return difftime(a, b) > 0.0;
}

void fmt_time_iso_utc(time_t t, char out[32])
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

void table_insert(DataEntry **head, DataEntry *data)
{
    if (!head || !data) {
        return;
    }
    data->next = NULL;

    DataEntry **pp = head;
    while (*pp && !is_after(data->timestamp, (*pp)->timestamp)) {
        pp = &(*pp)->next;
    }

    data->next = *pp;
    *pp = data;
}

void view_table(DataEntry *head)
{
    DataEntry *walk = head;
    while (walk) {
        char buffer[32];
        fmt_time_iso_utc(walk->timestamp, buffer);
        printf("%s ) %s\n", buffer, walk->content);
        walk = walk->next;
    }
}
