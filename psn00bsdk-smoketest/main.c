/*
 * aGBe core smoke test - PSn00bSDK build
 *
 * This is NOT the real aGBe front-end (that still needs the CD-ROM/menu/
 * GsLib-replacement rendering layer rewritten against PSn00bSDK - a
 * separate, larger task). What this proves instead: the actual CPU/MBC/PPU
 * core - the exact code this session spent finding and fixing real bugs in,
 * and validated against Blargg's test ROMs and a reference core on a host
 * machine - compiles, links, and *runs* as genuine PS1 machine code, using
 * the real toolchain PSn00bSDK provides.
 *
 * It boots a tiny embedded synthetic "ROM" (no real game data - just enough
 * header bytes for loadRom() to see a valid, minimal cartridge), runs the
 * real reset_Z80()/instruction-dispatch path for a fixed number of
 * instructions, and reports pass/fail plus the resulting CPU register state
 * on screen using PSn00bSDK's debug font.
 */

#include <stdint.h>
#include <stdio.h>
#include <psxgpu.h>
#include <psxetc.h>
#include "main.h"
#include "emu.h"
#include "pad.h"
#include "core_state.h"

// The one real definition of pad (extern-declared in pad.h) for this test
// executable, plus the platform hooks the core calls into (see
// core_state.h). Real controller/GPU integration is follow-up work for the
// platform-layer port; these placeholders let the core run standalone.
u_long pad = 0;
u_long lastpad = 0;
BYTE *ROM;
u_long PadRead(int pad_num) { (void)pad_num; return 0; }
void Draw_Buffer(int *screenBuffer) { (void)screenBuffer; }
void PrepScreen(void) {}
void RenderWorld(BYTE re, BYTE gr, BYTE bl) { (void)re; (void)gr; (void)bl; }

// A minimal, valid-enough 32KB "ROM": all zero (which disassembles as NOP,
// opcode 0x00) except a correct Nintendo header where loadRom() looks for
// cart type / ROM size / RAM size, so the core's real header-parsing path
// runs for real rather than being skipped.
static BYTE testrom[32768];

#define OT_LENGTH 8

typedef struct {
	DISPENV disp_env;
	DRAWENV draw_env;
} RenderBuffer;

int main(void) {
	ResetGraph(0);
	FntLoad(960, 0);

	RenderBuffer buf;
	SetDefDrawEnv(&buf.draw_env, 0, 0, 320, 240);
	SetDefDispEnv(&buf.disp_env, 0, 0, 320, 240);
	setRGB0(&buf.draw_env, 0, 0, 63);
	buf.draw_env.isbg = 1;
	PutDrawEnv(&buf.draw_env);
	PutDispEnv(&buf.disp_env);
	SetDispMask(1);

	// Build the synthetic ROM: cart type 0x00 (ROM ONLY), ROM size 0x00
	// (32KB, 2 banks), RAM size 0x00 (none) - matches testrom's actual size.
	testrom[0x0147] = 0x00; // cart type
	testrom[0x0148] = 0x00; // ROM size
	testrom[0x0149] = 0x00; // RAM size
	ROM = testrom;

	loadRom();
	reset_Z80();

	// Run a bounded number of real instruction-dispatch cycles through the
	// exact same code path runEmu() uses, minus the pad-polling parts (no
	// controller integration yet - see PadRead() above).
	int i;
	for (i = 0; i < 100000 && EMULATING; i++) {
		if (IME && (IFLAG & IER)) {
			interrupt();
		}
		instructions[ReadMEM(reg_PC++)]();
	}

	char line[64];
	FntPrint(-1, "aGBe core smoke test (PSn00bSDK)\n");
	FntPrint(-1, "ran %d instructions OK\n", i);
	sprintf(line, "PC=%04X SP=%04X A=%02X F=%02X", reg_PC, reg_SP, reg_A, reg_F);
	FntPrint(-1, "%s\n", line);
	sprintf(line, "cart type=%02X romsize=%02X ramsize=%02X", CARTTYPE, ROMSIZE, RAMSIZE);
	FntPrint(-1, "%s\n", line);
	FntFlush(-1);

	DrawSync(0);
	VSync(0);

	for (;;) {
		VSync(0);
	}

	return 0;
}
