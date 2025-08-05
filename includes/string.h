#ifndef _STRING_H
#define _STRING_H

#include "stddef.h"

void *memset(void *dest, int val, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
int memcmp(const void *a, const void *b, size_t count);
size_t strlen(const char *str);

#endif
