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

// Real CD-ROM ROM loading (multi-game disc support). Reads the named
// ISO9660 file whole into dest (which must be at least as large as the
// file). Returns the file size in bytes on success, or a negative value
// if the file couldn't be found/read. filename is matched case-
// insensitively; the ISO9660 version suffix (";1") is optional - it's
// appended automatically if not already present.
//
// NOTE: this reads the whole ROM into RAM up front, same as the rest of
// the emulator core assumes (ROM[loc] is a flat pointer, no bank
// streaming). That's fine for the vast majority of the GB/GBC library
// (32KB-512KB), but the small number of very large late-era GBC games
// (up to 4-8MB) won't fit in the PS1's 2MB of RAM alongside everything
// else this way - see STATUS.md for the real fix (stream only the
// active MBC bank from CD on demand), which is a separate, bigger task.
#define MAX_ROM_SIZE (512 * 1024)
int LoadROMFromCD(const char *filename, BYTE *dest, int maxSize);

// List up to maxFiles entries from the disc's root directory into
// outFiles (an array of CdlFILE, from psxcd.h - forward-declared via
// void* here so this header doesn't need to pull in psxcd.h itself).
// Returns the number of entries found. For the future ROM-select menu.
int ListRootDirectory(void *outFiles, int maxFiles);

// Game Boy screen dimensions (used when centering on the PS1's 320x240
// output).
#define GB_SCREEN_WIDTH  160
#define GB_SCREEN_HEIGHT 144

#endif
