#pragma once
#include <stdint.h>
typedef struct {
    uint16_t i_addr;
    char     s_addr[6];
} Address;

extern Address g_address;
