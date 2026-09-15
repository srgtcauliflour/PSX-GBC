// Minimal set of globals the emulator core (emu.c/opcodes.c) actually
// touches from the platform layer: the cartridge ROM pointer and the raw
// controller state. Split out from the original psx.h, which mixed these
// in with a large block of Psy-Q GsLib rendering state (GsOT, GsSPRITE,
// GsIMAGE, etc.) that PSn00bSDK has no equivalent for and that the core
// itself never touches. The real platform-layer port replaces psx.h's
// rendering half with PSn00bSDK's raw psxgpu.h primitives; this header is
// the half that survives unchanged either way.
#ifndef CORE_STATE_H
#define CORE_STATE_H

extern BYTE *ROM;
extern u_long pad, lastpad;

// Platform hooks emu.c calls into (input polling, frame present). Real
// implementations belong in the platform layer; these are declared here
// so the core compiles standalone against any platform, PSn00bSDK
// included, without dragging in the legacy GsLib rendering types.
u_long PadRead(int pad_num);
void Draw_Buffer(int *screenBuffer);
void PrepScreen(void);
void RenderWorld(BYTE re, BYTE gr, BYTE bl);

#endif
