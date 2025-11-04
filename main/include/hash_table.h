#ifndef _HASH_TABLE_H_
#define _HASH_TABLE_H_

HashTable *create_hashtable(size_t size);
void delete_hashtable(HashTable **ptr);
int hash_insert(HashTable *table, int key, void *value);
void *hash_find(HashTable *table, int key);
void *hash_remove(HashTable *table, int key);

typedef struct entry_struct {
	int key;
	void *value;
	struct entry_struct *next;
} Entry;

typedef struct hash_table_struct {
	size_t size;
	int entries;
	Entry **table;
} HashTable;

#endif