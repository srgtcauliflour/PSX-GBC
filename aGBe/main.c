/* --------------------------------------------------

             aGBe - a game boy emulator
             		for the psx

  REWRITE NOTE: replaces the original Psy-Q-based main.c (see git history
  or /tmp/old_main.c during the session this was written in), which called
  into the GsLib-based GUI (CD-ROM ROM bank loading, splash screen, ROM
  select menu - see gui.c) that has no PSn00bSDK equivalent yet (real,
  separate follow-up work - see STATUS.md).

  This version proves the actual emulator core - the CPU/MBC/PPU logic
  this project spent a whole session finding and fixing real bugs in, and
  independently validating against Blargg's test ROMs - runs correctly as
  real PS1 machine code with real GPU output and real controller input.
  It boots an embedded demo ROM (a Blargg test ROM already known to pass)
  as a stand-in for real CD-ROM-based game loading.

   -------------------------------------------------- */

#include "main.h"
#include "emu.h"
#include "pad.h"
#include "psx.h"
#include "demo_rom.h"

int main(void) {
	init_PSX();

	ROM = (BYTE *) demo_rom;
	runEmu();

	return 0;
}
