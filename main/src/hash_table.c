#include "hash_table.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


static inline uint32_t mix32(uint32_t x){
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static int isPrime(int n) {
    if (n <= 1) return 0;
    if (n <= 3) return 1;
    if (n % 2 == 0 || n % 3 == 0) return 0;
    for (int i = 5; i * i <= n; i = i + 6)
        if (n % i == 0 || n % (i + 2) == 0) return 0;
    return 1;
}


// create hash table
HashTable *create_hashtable(size_t size) {
	// move to next prime
	size_t n = size;
	while (!isPrime(n)) {
		n++;
	}
	HashTable *new_table = malloc(sizeof(HashTable));
	new_table->size = n;
	new_table->entries = 0;
	new_table->table = calloc(n, sizeof(Entry *));

	return new_table;
}

void delete_hashtable(HashTable **ptr) {
	if (!ptr || !*ptr) return;
	HashTable *table = *ptr;
	for (int i = 0; i < table->size; i++) {
		Entry *temp;
		Entry *head = table->table[i];
		while (head) {
			temp = head->next;
			free(head);
			head = temp;
		}
	}
	free(table->table);
	free(table);
	*ptr = NULL;
}


int hash_insert(HashTable *table, int key, void *value) {
	size_t idx = (size_t) mix32((uint32_t) key) % table->size;

	Entry *new_entry = malloc(sizeof(Entry));
	new_entry->key = key;
	new_entry->value = value;
	new_entry->next = NULL;
	table->entries++;

	// if slot is empty
	if (!table->table[idx]) {
		table->table[idx] = new_entry;
		return 1;
	}

	new_entry->next = table->table[idx];
	table->table[idx] = new_entry;

	return 0;
}

void *hash_find(HashTable *table, int key) {
	size_t idx = (size_t ) mix32((uint32_t) key) % table->size;

	Entry *walk = table->table[idx];

	while (walk) {
		if (walk->key == key) return walk->value;
		walk = walk->next;
	}

	return NULL;
}

void *hash_remove(HashTable *table, int key) {
    if (!table || table->size == 0) return NULL;

    size_t idx = (size_t)(mix32((uint32_t)key) % (uint32_t)table->size);

    Entry *cur  = table->table[idx];
    Entry *prev = NULL;

    while (cur) {
        if (cur->key == key) {
            void *val = cur->value;

            // unlink
            if (prev) prev->next = cur->next;
            else      table->table[idx] = cur->next;

            free(cur);
            table->entries--;
            printf("table.size = %d\n",table->entries);
            return val;
        }
        prev = cur;
        cur  = cur->next;
    }
    return NULL;  // not found
}

void** sort_hash_to_array(HashTable *table, int (*cmp)(const void *, const void *)) {
	if (!table) return NULL;
	if (table->entries == 0) return NULL;

	// create 1d array
	void **entries = calloc(table->entries, sizeof(void *));

	int c = 0;
	for (int i = 0; i < table->size; i++) {
		Entry *walk = table->table[i];
		while (walk) {
			entries[c++] = walk->value;
			walk = walk->next;
		}
	}

	qsort(entries, table->entries, sizeof(void *), cmp);

	return entries;
}

// int main() {
// 	HashTable *table = create_hashtable(10);

// 	struct test_d *ptr;

// 	for (int i = 0; i < 100; i++) {
// 		ptr = malloc(sizeof(struct test_d));
// 		ptr->a = (int) mix32(i) % (1000);
// 		ptr->b = i;
// 		hash_insert(table, i, (void *) ptr);
// 	}

// 	printf("table.size = %d\n",table->entries);



// 	// for (int i = 99; i >= 0; i--) {
// 	// 	int *status = (int *) hash_remove(table, i);
// 	// 	if (status) {
// 	// 		printf("table[%d] = %d Removed\n",i, *status);
// 	// 	}
// 	// 	printf("table.size = %d\n",table->entries);
// 	// }
// 	printf("SORTING\n");

// 	void **sort_order = sort_hash_to_array(table, cmp);
// 	for (int i = 0; i < table->entries; i++) {
// 		struct test_d *data = (struct test_d *) sort_order[i];
// 		printf("table order = a = %d, b = %d\n",data->a, data->b);
// 	}
// 	free(sort_order);

// 	for (int i = 0; i < 100; i++) {
// 		int *status = (int *) hash_find(table, i);
// 		if (status) {
// 			printf("table[%d] = %d found\n",i, *status);
// 		}
// 	}

// 	// delete_hashtable(&table);


// 	return 0;
// }