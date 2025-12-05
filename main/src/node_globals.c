#include "node_globals.h"
#include "hash_table.h"
#include "esp_random.h"

#include <stdint.h>

HashTable *g_msg_table = NULL;
ID g_my_address = NO_ID;
NodeEntry *g_this_node = NULL;

ID rand_msg_id(void) {
    ID x;
    do {
        x = (ID) esp_random();
    } while (x == 0);
    return x;
}

ID rand_id(void) {
    const uint32_t m = 9000; // (10000 - 1000) no leading 0s
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % m);
    uint32_t r;
    do {
        r = esp_random();
    } while (r >= limit);
    return (ID)((r % m) + 1000); // 1000..9999
}
