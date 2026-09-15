// psx.c - platform layer implementation (PSn00bSDK version).
//
// REWRITE NOTE: replaces the original Psy-Q/GsLib-based psx.c (711 lines,
// see git history or /tmp/old_psx.c during the session this was written
// in). That version's rendering, CD-ROM ROM-bank loading, and GUI/menu
// code were all built on Psy-Q's GsLib (GsSPRITE/GsOT/GsSortSprite/...),
// which PSn00bSDK has no equivalent for. This version covers what the
// emulator core actually needs to run and be visible/playable: real GPU
// output (the emulated Game Boy screen, centered on the PS1's 320x240
// display) and real controller input. Deliberately NOT yet covered here
// (tracked in STATUS.md): the CD-ROM ROM-bank loader, and the GsSPRITE/
// font-based GUI (splash screen, ROM select menu) from the original
// gui.c - both need their own from-scratch rewrites against psxcd.h/
// mkpsxiso and raw psxgpu.h primitives respectively, which is real,
// separate follow-up work.

#include <stdint.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>
#include "main.h"
#include "pad.h"
#include "psx.h"

// The one real (defining) declaration of the cartridge ROM pointer and
// the raw pad state - psx.h/pad.h only extern-declare them (see the BUG
// FIX notes added to those headers earlier this session).
BYTE *ROM;
u_long pad, lastpad;

// ---- Display setup --------------------------------------------------
#define SCREEN_XRES 320
#define SCREEN_YRES 240

static DISPENV disp_env;
static DRAWENV draw_env;

// ---- Game Boy screen rendering ---------------------------------------
// The emulator core hands off a raw 160x144 buffer of 2-bit shade indices
// (0=white/lightest .. 3=black/darkest - the standard BGP/OBP register
// shade semantics, already resolved by the core's own palette lookups) by
// calling Draw_Buffer() once per emulated frame, from vblank(). Converting
// that to real pixels and blitting it straight into the currently
// displayed VRAM area (via a synchronous LoadImage(), no primitive/OT
// queue needed) is the simplest rendering path that actually works -
// proper double buffering to eliminate tearing is a real, separate
// follow-up once the core pipeline is confirmed solid on real hardware.
static uint16_t gb_framebuffer[GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT];

// BGR555 grayscale shades, brightest (0) to darkest (3).
static const uint16_t gb_shade_colors[4] = {
	0x7FFF, // shade 0: white
	0x56B5, // shade 1: light gray
	0x2D6B, // shade 2: dark gray
	0x0000, // shade 3: black
};

void Draw_Buffer(int *screenBuffer) {
	int i;
	for (i = 0; i < GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT; i++) {
		gb_framebuffer[i] = gb_shade_colors[screenBuffer[i] & 0x03];
	}

	RECT rect;
	rect.x = (SCREEN_XRES - GB_SCREEN_WIDTH) / 2;
	rect.y = (SCREEN_YRES - GB_SCREEN_HEIGHT) / 2;
	rect.w = GB_SCREEN_WIDTH;
	rect.h = GB_SCREEN_HEIGHT;
	LoadImage(&rect, (const uint32_t *) gb_framebuffer);
	DrawSync(0);
}

// Legacy GsLib-era hooks the core still calls from vblank() (PrepScreen();
// DrawBG(); RenderWorld(0,0,0);) around the Draw_Buffer() call above.
// GsLib used these for its ordering-table submit/present cycle; the
// direct-LoadImage approach here doesn't need either, so both are no-ops.
void PrepScreen(void) {}
void RenderWorld(BYTE re, BYTE gr, BYTE bl) { (void) re; (void) gr; (void) bl; }

// ---- Controller input -------------------------------------------------
// PSn00bSDK doesn't wrap the BIOS's pad driver the way the official Sony
// SDK's PadInit()/PadRead() did (see psxpad.h's own comments) - but the
// BIOS driver itself is the same either way, so this uses it directly via
// psxapi.h's InitPAD/StartPAD, matching what PadInit()/PadRead() would
// have done underneath in the original Psy-Q build.
static uint8_t pad_buff[2][34];

unsigned long PadRead(int pad_num) {
	(void) pad_num; // only controller port 1 is read for now
	PADTYPE *pad0 = (PADTYPE *) pad_buff[0];

	// stat != 0 means no controller responded (disconnected, or the BIOS
	// hasn't completed a poll cycle yet) - report nothing pressed rather
	// than garbage.
	if (pad0->stat != 0) {
		return 0;
	}

	// Real hardware button bits are active-low (0 = pressed); invert so
	// the bit tests below read naturally as "1 = pressed". Then remap
	// from PSn00bSDK's real controller-protocol bit layout (PadButton in
	// psxpad.h) into the historical bit layout aGBe's own pad.h macros
	// use - the two differ, and rather than touch the many existing
	// (pad & Pad1x)-style checks in emu.c, all the translation is
	// localized here in one place.
	uint16_t raw = ~(pad0->btn);
	unsigned long result = 0;
	if (raw & PAD_UP)       result |= Pad1Up;
	if (raw & PAD_DOWN)     result |= Pad1Down;
	if (raw & PAD_LEFT)     result |= Pad1Left;
	if (raw & PAD_RIGHT)    result |= Pad1Right;
	if (raw & PAD_L1)       result |= Pad1L1;
	if (raw & PAD_L2)       result |= Pad1L2;
	if (raw & PAD_R1)       result |= Pad1R1;
	if (raw & PAD_R2)       result |= Pad1R2;
	if (raw & PAD_TRIANGLE) result |= Pad1tri;
	if (raw & PAD_SQUARE)   result |= Pad1sqr;
	if (raw & PAD_CIRCLE)   result |= Pad1crc;
	if (raw & PAD_CROSS)    result |= Pad1x;
	if (raw & PAD_START)    result |= Pad1Start;
	if (raw & PAD_SELECT)   result |= Pad1Select;
	return result;
}

// ---- Initialization ----------------------------------------------------
void init_PSX(void) {
	ResetGraph(0);

	SetDefDrawEnv(&draw_env, 0, 0, SCREEN_XRES, SCREEN_YRES);
	SetDefDispEnv(&disp_env, 0, 0, SCREEN_XRES, SCREEN_YRES);
	setRGB0(&draw_env, 0, 0, 0);
	draw_env.isbg = 1;
	PutDrawEnv(&draw_env);
	PutDispEnv(&disp_env);
	SetDispMask(1);

	InitPAD(pad_buff[0], 34, pad_buff[1], 34);
	StartPAD();
	// Match the original Psy-Q build's polling behaviour (continuous
	// state rather than the BIOS's optional auto-clear-on-VSync mode).
	ChangeClearPAD(0);
}
