#include "hash_table.h"
#include "node_globals.h"

#include <stdint.h>

#include "esp_random.h"


Address g_address = {0};
HashTable *g_msg_table = NULL;

uint16_t rand_id(void) {
    const uint32_t m = 9000; // (10000 - 1000) no leading 0s
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % m);
    uint32_t r;
    do {
        r = esp_random();
    } while (r >= limit);
    return (uint16_t)((r % m) + 1000); // 1000..9999
}