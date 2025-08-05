#ifndef _STDINT_H
#define _STDINT_H

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef signed short       int16_t;
typedef unsigned short     uint16_t;
typedef signed int         int32_t;
typedef unsigned int       uint32_t;
typedef signed long long   int64_t;
typedef unsigned long long uint64_t;
typedef unsigned int uintptr_t;

#if defined(__x86_64__) || defined(_M_X64)
typedef unsigned long long uintptr_t;
#else
typedef unsigned int uintptr_t;
#endif

#endif
