#ifndef STUB_LIBCD_H
#define STUB_LIBCD_H
typedef struct {
    unsigned char pos[4];
    char name[16];
    unsigned long size;
} CdlFILE;
#endif
