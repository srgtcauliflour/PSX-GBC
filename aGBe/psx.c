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
#include <string.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>
#include <psxcd.h>
#include <sys/fcntl.h>
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

// BUG FIX (tearing): this used to be a single DISPENV/DRAWENV pair, with
// Draw_Buffer() blitting straight into the region currently being shown
// on screen - visible as tearing whenever a blit happened to land while
// that same area was being scanned out to the TV. Standard PS1 double
// buffering: two vertically-stacked regions in VRAM (0-239 and 240-479),
// with disp[i]/draw[i] deliberately pointing at OPPOSITE halves for the
// same index i, so showing disp[db] while drawing into draw[db] always
// targets two different physical areas. Flipping db each frame means
// each half alternates between "currently displayed" and "safe to draw
// the next frame into" - the same pattern PSn00bSDK's own multi-buffer
// examples (e.g. examples/cdrom/cdbrowse) use.
static DISPENV disp_env[2];
static DRAWENV draw_env[2];
static int db = 0;

// ---- Game Boy screen rendering ---------------------------------------
// The emulator core hands off a raw 160x144 buffer of 2-bit shade indices
// (0=white/lightest .. 3=black/darkest - the standard BGP/OBP register
// shade semantics, already resolved by the core's own palette lookups) by
// calling Draw_Buffer() once per emulated frame, from vblank(). Converting
// that to real pixels and blitting it into the currently non-displayed
// half of VRAM (via a synchronous LoadImage(), no primitive/OT queue
// needed for a plain, non-animated blit like this) and then flipping
// which half is displayed is what actually eliminates the tearing the
// single-buffer version had.
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

	// Clears draw_env[db]'s half (the one NOT currently displayed) to
	// black before blitting into it, same as PSn00bSDK's own multi-buffer
	// examples do every frame - isbg+PutDrawEnv together issue a
	// synchronous GPU fill of the draw area.
	PutDrawEnv(&draw_env[db]);

	// draw_env[db] points at the half of VRAM currently NOT being shown
	// (disp_env[db] shows the other half) - safe to write into without
	// tearing whatever is currently on screen.
	RECT rect;
	rect.x = draw_env[db].clip.x + (SCREEN_XRES - GB_SCREEN_WIDTH) / 2;
	rect.y = draw_env[db].clip.y + (SCREEN_YRES - GB_SCREEN_HEIGHT) / 2;
	rect.w = GB_SCREEN_WIDTH;
	rect.h = GB_SCREEN_HEIGHT;
	LoadImage(&rect, (const uint32_t *) gb_framebuffer);
	DrawSync(0);

	// Now that the frame we just drew is complete, show it (and start
	// drawing the next one into what was, until this line, the displayed
	// half) by flipping to the other index.
	PutDispEnv(&disp_env[db]);
	SetDispMask(1);
	db = !db;
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

	// Two vertically-stacked 320x240 regions in VRAM: disp_env[i] and
	// draw_env[i] deliberately point at OPPOSITE halves for the same i
	// (see the comment above the buffer declarations) - this is what
	// makes alternating db between 0 and 1 in Draw_Buffer() actually
	// double-buffer instead of drawing into what's currently shown.
	SetDefDispEnv(&disp_env[0], 0, 0, SCREEN_XRES, SCREEN_YRES);
	SetDefDispEnv(&disp_env[1], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&draw_env[0], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&draw_env[1], 0, 0, SCREEN_XRES, SCREEN_YRES);

	int i;
	for (i = 0; i < 2; i++) {
		setRGB0(&draw_env[i], 0, 0, 0);
		draw_env[i].isbg = 1;
	}
	PutDrawEnv(&draw_env[0]);
	PutDispEnv(&disp_env[0]);
	SetDispMask(1);

	// Loads PSn00bSDK's built-in debug font into an unused corner of VRAM
	// (x=960 is well clear of the 320-wide visible display area) so
	// FntPrint() works - used by the ROM select menu (gui.c) for now, and
	// available for any future debug/UI text.
	FntLoad(960, 0);

	InitPAD(pad_buff[0], 34, pad_buff[1], 34);
	StartPAD();
	// Match the original Psy-Q build's polling behaviour (continuous
	// state rather than the BIOS's optional auto-clear-on-VSync mode).
	ChangeClearPAD(0);

	CdInit();

	// Initializes the BIOS's memory card filesystem driver so the
	// bu00:/bu10: device paths used by Save/LoadCartRAM below work.
	_bu_init();
}

// ---- CD-ROM ROM loading (multi-game disc support) ----------------------
// Real ISO9660 file access via PSn00bSDK's psxcd.h - CdSearchFile() finds
// a named file's disc position and size (the same two-call pattern the
// original Psy-Q-based version of this file used, just against a plain
// ISO9660 file instead of a custom AGBEBANK.BIN multi-ROM bundle format:
// plain files are simpler, standard, and don't need a bespoke packing
// tool - the original's packer was never even committed to the project's
// CVS history in the first place, see STATUS.md).
int LoadROMFromCD(const char *filename, BYTE *dest, int maxSize) {
	CdlFILE file;
	char name[32];

	// CdSearchFile wants the ISO9660 version suffix; add it if the caller
	// didn't already include one, so callers can just pass "GAME.GB".
	strncpy(name, filename, sizeof(name) - 3);
	name[sizeof(name) - 3] = '\0';
	if (!strchr(name, ';')) {
		strcat(name, ";1");
	}

	if (!CdSearchFile(&file, name)) {
		return -1;
	}
	if (file.size > maxSize) {
		return -2;
	}

	CdControl(CdlSetloc, &file.pos, 0);
	// Sectors are 2048 bytes each in the default (non-CdlModeSize) mode
	// CdRead's own doc comments describe; round up so a file that isn't
	// an exact multiple of 2048 bytes still gets fully read.
	int sectors = (file.size + 2047) / 2048;
	if (!CdRead(sectors, (uint32_t *) dest, 0)) {
		return -3;
	}
	CdReadSync(0, 0);

	return file.size;
}

int ListRootDirectory(void *outFiles, int maxFiles) {
	CdlFILE *files = (CdlFILE *) outFiles;
	CdlDIR *dir = CdOpenDir("\\");
	int found = 0;

	if (!dir) {
		return 0;
	}
	while (found < maxFiles && CdReadDir(dir, &files[found])) {
		found++;
	}
	CdCloseDir(dir);
	return found;
}

// ---- Memory card save/load (battery-backed cart RAM) -------------------
// Real BIOS filesystem access via psxapi.h's open/close/read/write and
// sys/fcntl.h's FREAD/FWRITE/FCREATE/FNBLOCKS - the same "bu00:" device
// path convention and flag values the official SDK's memory card access
// uses (PSn00bSDK deliberately mirrors it; _bu_init() below is its name
// for what the official SDK exposes as InitCARD/StartCARD's underlying
// driver init).
//
// One save file per cartridge, named from the cart's own header title so
// multiple games on one disc (see the CD-loading/ROM-select work earlier
// this session) don't collide with each other's saves.
#define SAVE_BLOCK_SIZE 8192

// Builds a memory-card-safe file path from a cartridge title: "bu00:"
// plus up to 10 sanitized characters (alphanumeric only, everything else
// dropped) from the title, prefixed "AGBE-" so these saves are
// identifiable and don't collide with any other homebrew's saves on the
// same card.
static void build_save_path(char *out, const char *title) {
	strcpy(out, "bu00:AGBE-");
	int outLen = strlen(out);
	int i;
	for (i = 0; i < 10 && title[i] != '\0'; i++) {
		char c = title[i];
		int isAlnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
		if (isAlnum) {
			out[outLen++] = c;
		}
	}
	out[outLen] = '\0';
}

int SaveCartRAM(const char *saveId, BYTE *buf, int size) {
	char path[32];
	build_save_path(path, saveId);

	int blocks = (size + SAVE_BLOCK_SIZE - 1) / SAVE_BLOCK_SIZE;
	if (blocks < 1) {
		blocks = 1;
	}

	// Make sure a correctly-sized file exists first (creating one is a
	// no-op error, harmlessly ignored, if it already does).
	int f = open(path, FCREATE | FWRITE | FNBLOCKS(blocks));
	if (f >= 0) {
		close(f);
	}

	f = open(path, FWRITE);
	if (f < 0) {
		return 0; // no card present, card full, or some other I/O error
	}
	int written = write(f, buf, size);
	close(f);
	return written == size;
}

int LoadCartRAM(const char *saveId, BYTE *buf, int size) {
	char path[32];
	build_save_path(path, saveId);

	int f = open(path, FREAD);
	if (f < 0) {
		return 0; // no save yet - not an error, just a fresh cartridge
	}
	int got = read(f, buf, size);
	close(f);
	return got == size;
}
