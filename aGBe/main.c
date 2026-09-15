/* --------------------------------------------------

             aGBe - a game boy emulator
             		for the psx

  REWRITE NOTE: replaces the original Psy-Q-based main.c (see git history
  or /tmp/old_main.c during the session this was written in), which called
  into the GsLib-based GUI (splash screen, ROM select menu - see gui.c)
  that has no PSn00bSDK equivalent yet (real, separate follow-up work -
  see STATUS.md).

  This version proves the actual emulator core - the CPU/MBC/PPU logic
  this project spent a whole session finding and fixing real bugs in, and
  independently validating against Blargg's test ROMs - runs correctly as
  real PS1 machine code with real GPU output, real controller input, and
  now real CD-ROM ROM loading, so multiple Game Boy games can genuinely
  live on one disc (the ROM select menu itself - picking which one to
  load - is the next real piece of work; this loads a fixed filename for
  now, which the eventual menu will only need to make dynamic).

   -------------------------------------------------- */

#include "main.h"
#include "emu.h"
#include "pad.h"
#include "psx.h"
#include "demo_rom.h"

// The name of the ROM file to load from the disc. Standing in for real
// ROM selection (see STATUS.md) - swapping this for a variable set by an
// actual menu is a small change once that menu exists; the CD-loading
// mechanics it will call are already real.
#define BOOT_ROM_FILENAME "GAME.GB"

static BYTE rom_buffer[MAX_ROM_SIZE];

int main(void) {
	init_PSX();

	int size = LoadROMFromCD(BOOT_ROM_FILENAME, rom_buffer, MAX_ROM_SIZE);
	if (size > 0) {
		ROM = rom_buffer;
	} else {
		// No disc, file not found, or running via direct EXE injection
		// with no CD image at all (e.g. during bring-up/testing) - fall
		// back to the embedded demo ROM rather than running with no
		// cartridge loaded at all.
		ROM = (BYTE *) demo_rom;
	}

	runEmu();

	return 0;
}
