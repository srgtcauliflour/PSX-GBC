// gui.c - ROM select menu (PSn00bSDK version).
//
// REWRITE NOTE: see gui.h for why this replaces the original Psy-Q/GsLib
// version wholesale rather than porting it.

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

		FntPrint(-1, "\n  aGBe - select a game\n\n");
		for (i = 0; i < gameCount; i++) {
			FntPrint(-1, "  %s %s\n", (i == cursor) ? "->" : "  ", gameNames[i]);
		}
		FntPrint(-1, "\n  Up/Down: move   Cross/Start: play\n");
		FntFlush(-1);

		DrawSync(0);
		VSync(0);
		SetDispMask(1);
	}

	strncpy(selectedFilename, gameNames[cursor], maxLen - 1);
	selectedFilename[maxLen - 1] = '\0';
	return 1;
}
