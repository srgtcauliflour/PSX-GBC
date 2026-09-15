#ifndef STUB_SYS_TYPES_H
#define STUB_SYS_TYPES_H
#include_next <sys/types.h>
#ifndef _U_LONG_DEFINED
typedef unsigned long u_long;
typedef unsigned char u_char;
typedef unsigned short u_short;
#define _U_LONG_DEFINED
#endif
#endif
