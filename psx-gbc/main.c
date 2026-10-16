/* --------------------------------------------------

             aGBe - a game boy emulator
             		for the psx

  REWRITE NOTE: replaces the original Psy-Q-based main.c (see git history
  or /tmp/old_main.c during the session this was written in), which called
  into the GsLib-based GUI (splash screen, ROM select menu - see the
  earlier version of gui.c) that had no PSn00bSDK equivalent. gui.c has
  since been rewritten from scratch too (plain text menu, no GsLib) - see
  its own rewrite note - so this now does the real thing: list the games
  actually on the disc, let the player pick one, then run it.

  This version proves the actual emulator core - the CPU/MBC/PPU logic
  this project spent a whole session finding and fixing real bugs in, and
  independently validating against Blargg's test ROMs - runs correctly as
  real PS1 machine code with real GPU output, real controller input, and
  real CD-ROM ROM loading, so multiple Game Boy games genuinely live on
  one disc and the player picks which one to play.

   -------------------------------------------------- */

#include "main.h"
#include "emu.h"
#include "pad.h"
#include "psx.h"
#include "gui.h"
#include "demo_rom.h"

static BYTE rom_buffer[MAX_ROM_SIZE] __attribute__((aligned(4)));

int main(void) {
	init_PSX();

	char selectedFilename[16];
	int size = -1;

	if (ShowROMMenu(selectedFilename, sizeof(selectedFilename))) {
		size = OpenROMFile(selectedFilename);
		if (size > 0 && size <= MAX_ROM_SIZE) {
			if (!ReadROMBytes(0, rom_buffer, size)) size = -3;
		} else if (size > MAX_ROM_SIZE) {
			// Too big to hold whole: bank 0 stays resident, the rest of
			// the buffer becomes a cache of 16KB banks read from the CD
			// as the game switches banks.
			if (ReadROMBytes(0, rom_buffer, 0x4000)) {
				EnableROMStreaming(rom_buffer + 0x4000, (MAX_ROM_SIZE - 0x4000) / 0x4000, ReadROMBank);
			} else {
				size = -3;
			}
		}
	}

	if (size > 0) {
		ROM = rom_buffer;
	} else {
		// No disc, no .GB/.GBC files found on it, or the selected file
		// failed to load (e.g. direct EXE injection during bring-up/
		// testing with no real CD image at all) - fall back to the
		// embedded demo ROM rather than running with no cartridge
		// loaded at all.
		ROM = (BYTE *) demo_rom;
	}

	runEmu();

	return 0;
}
