// psx.h - platform layer interface (PSn00bSDK version).
//
// REWRITE NOTE: this replaces the original Psy-Q/GsLib-based psx.h (see
// git history for the old version, or /tmp/old_psx.h during this session).
// The original mixed core emulator state (ROM pointer, pad input) in with
// a large block of GsLib rendering globals (GsOT, GsSPRITE, GsIMAGE, ...)
// that PSn00bSDK has no equivalent for - GsLib was Sony's higher-level
// sprite/OT abstraction built on top of raw libgpu, and PSn00bSDK
// deliberately doesn't reimplement it (see PSn00bSDK's own docs: porting
// existing homebrew is "easier with minimal modification provided they do
// not depend on libgs"). This version only declares what the emulator
// core (emu.c/opcodes.c) and the new platform layer (psx.c/main.c)
// actually need, built on raw psxgpu.h primitives instead.
#ifndef PSX_H
#define PSX_H

#include "pad.h"

// The cartridge ROM buffer pointer - defining declaration lives in psx.c.
extern BYTE *ROM;

// Platform hooks the emulator core calls into.
void init_PSX(void);
void Draw_Buffer(int *screenBuffer);
void PrepScreen(void);
void RenderWorld(BYTE re, BYTE gr, BYTE bl);
unsigned long PadRead(int pad_num);

// Game Boy screen dimensions (used when centering on the PS1's 320x240
// output).
#define GB_SCREEN_WIDTH  160
#define GB_SCREEN_HEIGHT 144

#endif
