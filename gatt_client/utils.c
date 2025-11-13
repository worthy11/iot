#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include "host/ble_hs.h"
#include "utils.h"

char *normalize_name(const uint8_t *src, size_t len)
{
    static char dest[32];
    size_t j = 0;
    for (size_t i = 0; i < len && j < sizeof(dest) - 1; i++)
    {
        if (!isspace(src[i]))
        {
            dest[j++] = tolower(src[i]);
        }
    }
    dest[j] = '\0';
    return dest;
}

char *ble_addr_to_str(const ble_addr_t *addr)
{
    static char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);
    return buf;
}

bool is_addr_empty(const ble_addr_t *addr)
{
    for (int i = 0; i < 6; i++)
    {
        if (addr->val[i] != 0)
        {
            return false;
        }
    }
    return true;
}
