// Minimal set of globals/hooks the emulator core (emu.c/opcodes.c) needs
// from the platform layer, beyond what pad.h already provides (u_long,
// pad/lastpad, the Pad1*/Pad2* button masks). Split out from the original
// psx.h, which mixed this in with a large block of Psy-Q GsLib rendering
// state (GsOT, GsSPRITE, GsIMAGE, etc.) that PSn00bSDK has no equivalent
// for and that the core itself never touches. The real platform-layer
// port replaces psx.h's rendering half with PSn00bSDK's raw psxgpu.h
// primitives; this header is the half that survives unchanged either way.
#ifndef CORE_STATE_H
#define CORE_STATE_H

extern BYTE *ROM;
extern u_long lastpad;

// Platform hooks emu.c calls into (input polling, frame present). Real
// implementations belong in the platform layer; these are declared here
// so the core compiles standalone against any platform, PSn00bSDK
// included, without dragging in the legacy GsLib rendering types.
u_long PadRead(int pad_num);
void Draw_Buffer(int *screenBuffer);
void PrepScreen(void);
void RenderWorld(BYTE re, BYTE gr, BYTE bl);

#endif
