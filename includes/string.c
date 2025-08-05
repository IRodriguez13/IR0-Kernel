/*
Son las implementaciones de las funciones mágicas que usás cuando hacés tus To Do Lists. (todo bien con las To Do)

*/

#include "string.h"

void *memset(void *dest, int val, size_t count)
{
    unsigned char *ptr = dest;
    while (count--)
        *ptr++ = (unsigned char)val;
    return dest;
}

void *memcpy(void *dest, const void *src, size_t count)
{
    const unsigned char *s = src;
    unsigned char *d = dest;
    while (count--)
        *d++ = *s++;
    return dest;
}

int memcmp(const void *a, const void *b, size_t count)
{
    const unsigned char *p1 = a, *p2 = b;
    while (count--)
        if (*p1++ != *p2++)
            return *(p1 - 1) - *(p2 - 1);
    return 0;
}

size_t strlen(const char *str)
{
    size_t len = 0;
    while (*str++) len++;
    return len;
}
