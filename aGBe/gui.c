// gui.c - ROM select menu (PSn00bSDK version).
//
// REWRITE NOTE: see gui.h for why this replaces the original Psy-Q/GsLib
// version wholesale rather than porting it.
//
// VISUAL POLISH NOTE: the first working version of this menu was plain
// FntPrint text on a black background - functional, not pretty. This
// version draws real GPU primitives (a title bar, a bordered menu panel,
// and a selection-highlight bar that tracks the cursor) underneath the
// same FntPrint text, using the double-buffer lifecycle (BeginFrame/
// PresentFrame) psx.c already exposes for the emulator's own screen
// rendering - the menu shares those same two VRAM buffers rather than
// needing its own separate display setup.

#include <string.h>
#include <psxgpu.h>
#include <psxetc.h>
#include <psxcd.h>
#include "main.h"
#include "pad.h"
#include "psx.h"
#include "gui.h"

#define MAX_MENU_ENTRIES 30

// Case-insensitive check for whether `name` ends with `suffix`.
static int has_suffix_ci(const char *name, const char *suffix) {
	int nameLen = strlen(name);
	int suffixLen = strlen(suffix);
	if (suffixLen > nameLen) {
		return 0;
	}
	const char *tail = name + (nameLen - suffixLen);
	int i;
	for (i = 0; i < suffixLen; i++) {
		char a = tail[i];
		char b = suffix[i];
		if (a >= 'a' && a <= 'z') a -= 32;
		if (b >= 'a' && b <= 'z') b -= 32;
		if (a != b) {
			return 0;
		}
	}
	return 1;
}

// Strips the ISO9660 ";1" (or ";N") version suffix from a file name in
// place, so what's shown on screen and returned to the caller is just
// the plain game name.
static void strip_version_suffix(char *name) {
	char *semi = strchr(name, ';');
	if (semi) {
		*semi = '\0';
	}
}

// ---- Layout ------------------------------------------------------------
// FntPrint's font is a fixed 8x8-pixel grid, one line per 8 vertical
// pixels - the row math below (ROW_HEIGHT) assumes that spacing so the
// highlight bar tracks the text exactly. If a future PSn00bSDK version
// changes the built-in font's line spacing, this is the constant to
// revisit.
#define ROW_HEIGHT   8
#define TITLE_X      20
#define TITLE_Y      8
#define PANEL_X      24
#define PANEL_Y      32
#define PANEL_WIDTH  272
#define LIST_X       48
#define LIST_Y       44
#define LIST_MARGIN  8

// Small flat-shaded rectangle helper - fills the primitive buffer pointed
// to by *next with one POLY_F4, adds it to the ordering table, and
// advances *next past it.
static void draw_rect(uint32_t *ot, char **next, int x, int y, int w, int h,
                       uint8_t r, uint8_t g, uint8_t b) {
	POLY_F4 *p = (POLY_F4 *) *next;
	setPolyF4(p);
	setRGB0(p, r, g, b);
	setXY4(p, x, y, x + w, y, x, y + h, x + w, y + h);
	addPrim(ot, p);
	*next += sizeof(POLY_F4);
}

int ShowROMMenu(char *selectedFilename, int maxLen) {
	CdlFILE allFiles[MAX_MENU_ENTRIES];
	char gameNames[MAX_MENU_ENTRIES][16];
	int gameCount = 0;
	int i;

	int found = ListRootDirectory(allFiles, MAX_MENU_ENTRIES);
	for (i = 0; i < found && gameCount < MAX_MENU_ENTRIES; i++) {
		if (has_suffix_ci(allFiles[i].name, ".GB;1") ||
		    has_suffix_ci(allFiles[i].name, ".GBC;1")) {
			strncpy(gameNames[gameCount], allFiles[i].name, 15);
			gameNames[gameCount][15] = '\0';
			strip_version_suffix(gameNames[gameCount]);
			gameCount++;
		}
	}

	if (gameCount == 0) {
		return 0;
	}

	int panelHeight = LIST_MARGIN * 2 + gameCount * ROW_HEIGHT + ROW_HEIGHT + 8;

	// Two independent text streams so the title and the game list can be
	// positioned precisely, rather than relying on a fixed number of
	// leading blank lines to separate them (fragile if that count ever
	// needs to change).
	FntOpen(TITLE_X, TITLE_Y, 280, ROW_HEIGHT * 2, 0, 64);
	FntOpen(LIST_X, LIST_Y, PANEL_WIDTH - (LIST_X - PANEL_X) * 2, panelHeight, 0, 512);

	static uint32_t ot[1];
	static char primBuffer[4096];

	int cursor = 0;
	unsigned long lastPad = 0;
	int confirmed = 0;

	while (!confirmed) {
		unsigned long padNow = PadRead(0);
		unsigned long pressed = padNow & ~lastPad; // edge-detect new presses only
		lastPad = padNow;

		if (pressed & Pad1Up) {
			cursor--;
			if (cursor < 0) {
				cursor = gameCount - 1;
			}
		}
		if (pressed & Pad1Down) {
			cursor++;
			if (cursor >= gameCount) {
				cursor = 0;
			}
		}
		if (pressed & (Pad1x | Pad1Start)) {
			confirmed = 1;
		}

		int offsetX, offsetY;
		BeginFrame(&offsetX, &offsetY);

		// Two separate DrawOTag passes rather than relying on a single
		// ordering table's same-index chain order (which insertion order
		// resolves to "on top" is easy to get backwards) - issuing the
		// background rects, syncing, then the highlight bar as its own
		// pass guarantees the highlight paints over the panel fill
		// regardless of that ambiguity, the same way the text (via
		// FntFlush, issued after both passes below) is guaranteed to
		// paint over all of this.
		ClearOTagR(ot, 1);
		char *next = primBuffer;

		// Title bar, full width, a deep blue accent.
		draw_rect(ot, &next, offsetX, offsetY, 320, TITLE_Y + ROW_HEIGHT + 4, 32, 40, 96);

		// Menu panel: a slightly lighter border rect with a darker fill
		// rect on top - two flat rects is a cheap way to fake a 2px frame
		// without needing four separate thin border primitives. Fill is
		// added after border in the same pass, on the same OT index, but
		// since it's fully contained within the border rect either
		// ordering would leave a visible result here - only the
		// highlight-vs-fill case (below) actually needs a real ordering
		// guarantee.
		draw_rect(ot, &next, offsetX + PANEL_X, offsetY + PANEL_Y,
		          PANEL_WIDTH, panelHeight, 80, 88, 140);
		draw_rect(ot, &next, offsetX + PANEL_X + 2, offsetY + PANEL_Y + 2,
		          PANEL_WIDTH - 4, panelHeight - 4, 24, 24, 44);

		DrawOTag(ot);
		DrawSync(0);

		// Selection highlight, tracking the cursor row exactly - its own
		// pass so it's guaranteed to paint over the panel fill above.
		ClearOTagR(ot, 1);
		next = primBuffer;
		draw_rect(ot, &next, offsetX + PANEL_X + 4, offsetY + LIST_Y + cursor * ROW_HEIGHT,
		          PANEL_WIDTH - 8, ROW_HEIGHT, 64, 72, 160);
		DrawOTag(ot);
		DrawSync(0);

		FntPrint(0, "aGBe - select a game");
		for (i = 0; i < gameCount; i++) {
			FntPrint(1, "%s\n", gameNames[i]);
		}
		FntPrint(1, "\nUp/Down: move   Cross/Start: play");
		FntFlush(0);
		FntFlush(1);

		DrawSync(0);
		VSync(0);
		PresentFrame();
	}

	strncpy(selectedFilename, gameNames[cursor], maxLen - 1);
	selectedFilename[maxLen - 1] = '\0';
	return 1;
}
