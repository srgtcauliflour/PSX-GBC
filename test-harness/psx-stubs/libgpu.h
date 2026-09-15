#ifndef STUB_LIBGPU_H
#define STUB_LIBGPU_H
typedef struct { unsigned char r,g,b,c; } TILE;
typedef struct { unsigned char r,g,b,c; } SPRT;
typedef struct { short x,y,w,h; } RECT;
typedef unsigned long PACKET[24];
typedef struct { long p[64]; } GsOT_TAG;
typedef struct { int length; GsOT_TAG *org; } GsOT;
typedef struct { int dummy; } GsDOBJ2;
typedef struct { int dummy; } GsCOORDINATE2;
typedef struct {
    unsigned short px, py, pw, ph;
    unsigned short cx, cy;
    unsigned char  *pixel;
    unsigned short *clut;
} GsIMAGE;
typedef struct {
    long tag;
    long code[2];
    short x, y, mx, my, w, h;
    short u, v;
    short attribute;
    short r, g, b;
} GsSPRITE;
#endif
