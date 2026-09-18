// aGBe
// EMU.C
// TODO: Write File Description

// includes ////////////////////////////////////////////////////
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "main.h"
//#include "mem.h"
#include "emu.h"
// BUG FIX: emu.c calls dozens of ALU/rotate/shift helper functions defined
// in opcodes.c (INCreg, ADDreg, RLC, ADDWreg, BIT, SET, RES, ...) but never
// included the header that declares them - same class of bug as the
// opcodes.c-missing-emu.h issue fixed earlier, just the mirror image. Only
// ever caught as compiler warnings (implicit int return, unknown parameter
// types) rather than hard errors, so it went unnoticed; confirmed for real
// against the actual PS1 MIPS cross-compiler.
#include "opcodes.h"
#include "psx.h"
#include "pad.h"

// defines ////////////////////////////////////////////////////
#define HBLANKMODE 0 // 00: Entire Display Ram can be accessed
#define VBLANKMODE 1 // 01: During V-Blank
#define OAMMODE 2    // 10: During Searching OAM-RAM
#define TRANSFERMODE 3 // 11: During Transfering Data to LCD Driver
// BUG FIX: these were scaled by an ad-hoc "CLOCKSPEED" fudge-factor (4.123)
// at each use site instead of just being the real, exact, well-documented
// T-state (cycle) counts real DMG hardware uses per PPU mode. The fudge
// factor didn't reproduce them either: a full visible scanline came out to
// ~448 cycles here vs the correct 456 (OAM 80 + pixel-transfer 172 +
// HBlank 204), and a VBlank line came out to ~449 vs the correct 456. That
// shortfall compounds every scanline and every frame, so LY (and anything
// timed relative to it - raster effects, VBlank-wait loops, the shared
// timing assumptions Blargg's test ROMs rely on) drifts further from real
// hardware the longer a program runs.
#define VBLANK_CYCLES   456
#define HBLANK_CYCLES   204
#define OAM_CYCLES       80
#define TRANSFER_CYCLES 172


// globals ////////////////////////////////////////////////////
int *fp;
BYTE currOp;
BYTE reg_A, reg_B, reg_C, reg_D, reg_E, reg_F;
WORD reg_HL, reg_SP, reg_PC;
BYTE IER;
BYTE IFLAG;
int videoMode = 0;
int curframe = 0;
int frameskip = 2;
int IME;
// BUG FIX (severe, real hardware CPU-timing quirk): real hardware delays
// EI's effect by one full instruction - the instruction immediately
// after EI always executes as if interrupts were still disabled, and
// only the instruction after *that* can actually be interrupted. This
// emulator previously set IME=1 immediately inside EI itself, with no
// delay at all. Most games never notice, since they don't have anything
// timing-critical happening in the single instruction right after EI -
// but code that deliberately uses that exact guaranteed-atomic window
// (a real, documented technique - "EI reti" and similar idioms rely on
// it) can behave completely differently without this delay. Found via
// Kirby's Pinball Land, whose interrupt-driven display update logic
// never re-triggered after its first run in this emulator, while an
// independent reference emulator (Peanut-GB) ran it correctly - this
// was the actual root cause once traced through several other ruled-out
// hypotheses (VBlank/Timer request-vs-service rates, IER toggling,
// MBC2 RAM correctness) via a long series of side-by-side CPU trace
// comparisons against that reference core.
// EI_PENDING counts down 2->1->0 across the two loop iterations after
// EI dispatches; IME only becomes 1 once it reaches 0, in the main
// dispatch loop below (not here) - see runEmu().
int EI_PENDING = 0;
int EMULATING;
BYTE *VRAM, *EXTRNRAM, *RAM, *OAMRAM, *HIRAM;
int temp;
int illegalOpcodes = 0, totalOpcodes = 0, devOpcodes = 0;
int t, n,z, i, j;
int pauseMnuPos = 0;
double CLOCKSPEED = 4.123;

//char buff[200];
int screenBuffer[160*144];
// Parallel to screenBuffer, only ever populated when GBC_MODE is set:
// the actual 15-bit RGB555 color (little-endian, matching how it's
// stored in BGPALRAM/OBJPALRAM) for each pixel, rather than a 0-3 DMG
// shade index. The platform layer picks whichever of the two buffers
// is relevant based on GBC_MODE - a DMG cart never touches this at all.
WORD screenBufferColor[160*144];
// Also parallel to screenBuffer: whether the BG/window tile drawn at
// this pixel had its CGB attribute byte's priority bit (bit 7) set -
// only ever populated (and only ever consulted, from DrawOBJline) when
// GBC_MODE is set. See the priority resolution comment in DrawOBJline
// for what this means and how it combines with the sprite's own OAM
// priority bit and LCDC bit 0's CGB-specific reinterpretation.
BYTE bgAttrPriority[160*144];
int runto = 0;

int Voff;
int cyclesLeft = 0;
// Video
int VideoCyclesLeft;
BYTE SCRX, SCRY;
BYTE WNDX, WNDY;
BYTE LCDCONTROL;
BYTE LCDSTATUS;
BYTE LCDY, LYC;
BYTE OBJPAL0, OBJPAL1;
BYTE BGPAL;
BYTE P1;
BYTE TIMEMOD, TIMCONT;
int TIMECNT;
// Real hardware quirk (Mooneye's tima_reload.gb test documents this
// precisely): when TIMA overflows, it doesn't reload to TMA
// immediately - it reads as $00 for exactly 4 T-cycles first, and only
// then takes on TMA's value. timaReloadPending counts that delay down;
// TIMECNT itself is already the visible/readable value throughout (set
// to 0 immediately on overflow, then to TMA once this reaches 0), so
// the $FF05 read handler needs no changes of its own.
int timaReloadPending = 0;

// Real hardware bit positions of the 16-bit internal counter each TAC
// clock-select value watches for a falling edge (Pan Docs "Timer
// Obscure Behaviour") - indexed by TAC bits 1-0. These exactly match
// this project's existing MAXTIME periods (1024/16/64/256 T-cycles),
// since a bit at position N has a full toggle period of 2^(N+1).
static const int TIMER_BIT_POS[4] = {9, 3, 5, 7};

// Checks the current watched-bit-AND-enabled value against the last
// one seen (TIMER_BIT_ANDED) and increments TIMA on a falling edge -
// call this any time either input could have changed: every T-cycle as
// internalDivCounter16 advances, and immediately after any DIV or TAC
// write (both of which can themselves cause a falling edge, which is
// the whole point of this real hardware quirk - see the comment above
// internalDivCounter16's declaration).
extern int internalDivCounter16;
extern int TIMER_BIT_ANDED;
void CheckTimerEdge(void) {
	int bitPos = TIMER_BIT_POS[TIMCONT & 0x03];
	int enabled = (TIMCONT >> 2) & 0x01;
	int currentBit = ((internalDivCounter16 >> bitPos) & 0x01) & enabled;
	if (TIMER_BIT_ANDED == 1 && currentBit == 0) {
		TIMECNT += 1;
		if (TIMECNT > 255) {
			IFLAG |= 0x04;
			TIMECNT = 0;
			timaReloadPending = 4;
		}
	}
	TIMER_BIT_ANDED = currentBit;
}
int MAXTIME, TIMECOUNTER;

// ---- GBC (Game Boy Color) support ----------------------------------
// GBC_MODE is set once at ROM load time from the cartridge header's CGB
// flag ($0143): $80 (CGB-enhanced, still DMG-compatible) or $C0
// (CGB-only) both mean this cart expects to run in CGB mode; anything
// else means plain DMG. Every CGB-specific register/buffer below is
// only ever touched when GBC_MODE is set - a DMG cart's behavior is
// completely unaffected by any of this.
int GBC_MODE = 0;
BYTE VBK;   // $FF4F - VRAM bank select (bit 0 only; bits 1-7 read as 1)
BYTE SVBK;  // $FF70 - WRAM bank select for $D000-$DFFF (bits 0-2; 0 behaves as 1)
BYTE KEY1;  // $FF4D - speed switch (prepare/current speed flags)
// HDMA1-4 ($FF51-$FF54): source/destination address high/low bytes for
// VRAM DMA. HDMA5 ($FF55) both starts a transfer on write (bit 7 picks
// General-Purpose, immediate, vs H-Blank-paced mode; bits 0-6 encode
// transfer length as (n+1)*16 bytes) and reports status on read.
BYTE HDMA1, HDMA2, HDMA3, HDMA4;
// Tracks an in-progress H-Blank DMA: -1 means none active. Real H-Blank
// DMA transfers 16 bytes per H-Blank rather than all at once - see the
// HDMA5 write handler and the hblank()-driven continuation for why this
// needs to persist across calls instead of finishing in one shot like
// General-Purpose DMA does.
int HDMA_REMAINING = -1;
int HDMA_SRC, HDMA_DST;
// BCPS/OCPS ($FF68/$FF6A): bit0-5 = palette RAM byte index (0-63),
// bit7 = auto-increment the index after each BCPD/OCPD write.
BYTE BCPS, OCPS;
// 8 palettes x 4 colors x 2 bytes (little-endian RGB555) each = 64
// bytes, for BG/window and OBJ respectively - real hardware's actual
// CGB palette RAM, read/written through the BCPS/OCPS index above via
// the BCPD/OCPD data ports ($FF69/$FF6B).
BYTE BGPALRAM[64];
BYTE OBJPALRAM[64];

// ---- APU (sound) support --------------------------------------------
// APUChannel is declared in emu.h (shared with psx.c's UpdateAudio()).
// One struct per channel holds both the raw, CPU-visible NRxx register
// bytes and the internal state real hardware keeps that software never
// reads back directly - the running length/envelope/sweep counters,
// waveform position, and (for channel 4) the noise LFSR. Grouped into
// a struct despite the rest of this codebase using flat globals
// throughout, since the sheer number of interdependent per-channel
// values here is much more manageable this way.
APUChannel apuCh1, apuCh2, apuCh3, apuCh4;
BYTE WAVERAM[16]; // $FF30-$FF3F - channel 3's 32 4-bit samples, 2 per byte
BYTE NR50, NR51, NR52;
// Frame sequencer: real hardware steps this 512Hz/8-step sequencer from
// falling edges of a specific DIV bit, which means DIV-resetting writes
// can shift its timing - a real, documented hardware quirk. This
// project instead free-runs a dedicated cycle counter that overflows at
// the same 512Hz rate, which is simpler and correct for the vast
// majority of real game behavior, at the cost of not reproducing that
// one specific DIV-interaction quirk exactly.
int apuFrameSeqCounter = 0;
int apuFrameSeqStep = 0;
// Runs the actual sample-generation clock - see GenerateAudioSample().
int apuSampleCycleAccumulator = 0;

int ROMBANKNUMBER = 1;// Bank register powers on selecting bank 1 (MBC1/2/3/5)
int RAMBANKNUMBER = 0;
int MBCMODE = 0;
int RAMENABLED = 0; // Cart RAM $A000-$BFFF gate: enabled by writing 0x0A to $0000-$1FFF
int RAM_DIRTY = 0; // Set on any cart RAM write; cleared once a save completes.
// BUG FIX: DIV ($FF04) was a complete no-op stub on both read and write.
// Real hardware free-runs an internal 16-bit divider at the base clock
// rate regardless of anything else (TAC/TIMA included) and exposes its
// upper 8 bits as DIV, incrementing every 256 cycles; any write to DIV
// resets it to 0 regardless of the value written. Software commonly reads
// it as a simple hardware counter/pseudo-random source, so a stuck-at-0
// (or whatever garbage the stub happened to return) DIV can cause
// spurious failures far removed from anything DIV-related on its face.
int DIVCOUNTER = 0;
// Real hardware quirk (Mooneye's div_write.gb/rapid_toggle.gb document
// this precisely): DIV and TIMA are actually driven by the same single,
// free-running 16-bit hardware counter - DIV is just that counter's
// upper 8 bits, and the timer increments on a falling edge of one
// specific bit of it (which bit depends on TAC's clock-select field),
// ANDed with the timer's own enable bit. Writing DIV resets the whole
// 16-bit counter to 0, and disabling the timer (or, on real hardware,
// changing which bit is watched) can each cause a *spurious* extra
// timer increment if the watched-bit-AND-enabled value happens to be 1
// right before the change, since that's a 1->0 transition too. This
// project's DIVCOUNTER/MAXTIME pair above is a simpler, independent
// model that doesn't reproduce any of this - internalDivCounter16 is
// the real, unified counter needed to do so; TIMER_BIT_ANDED tracks the
// last computed watched-bit-AND-enabled value so a falling edge can
// actually be detected when either input changes.
int internalDivCounter16 = 0;
int TIMER_BIT_ANDED = 0;
int FRAMECOUNT = 0; // Incremented once per vblank(); used by debug tracing below.
BYTE DIVREG = 0;
BYTE SERIALDATA = 0xFF;   // $FF01 SB - Serial transfer data
BYTE SERIALCONTROL = 0;  // $FF02 SC - Serial transfer control
// MBC3 Real-Time Clock
BYTE RTCSELECT = 0;      // Which RTC register (0x08-0x0C) is mapped at $A000-$BFFF, 0 = none/RAM
BYTE RTCLATCH = 0xFF;    // Tracks the 0x00->0x01 latch write sequence
BYTE RTC_S, RTC_M, RTC_H, RTC_DL, RTC_DH;          // Live RTC registers
BYTE RTCL_S, RTCL_M, RTCL_H, RTCL_DL, RTCL_DH;     // Latched (readable) copies
//char pauseData[6][10]={ "Continue", "Save", "Load", "Reset", "Options", "Exit" };

BYTE ROMSIZE, CARTTYPE, RAMSIZE, VERSIONNUMBER;
int iROMSIZE, iRAMSIZE;
BYTE CARTTITLE[16];
BYTE MANUCODE[2];


// functions ////////////////////////////////////////////////////
void reset_Z80() {
		#if defined(DEBUG)
		printf("Reseting Z80 Core..\n");
		#endif
		Allocate_Memory();
		EMULATING =  1;
		ROMBANKNUMBER = 1;
		RAMBANKNUMBER = 0;
		RAMENABLED = 0;
		// Set Registers
		reg_HL= 0x014D;
		reg_SP= 0xFFFE;
		reg_PC= 0x0100;
		reg_A = 0x01;
		reg_B = 0x00;
		reg_C = 0x13;
		reg_D = 0x00;
		reg_E = 0xD8;
		reg_F = 0xB0;
		IFLAG = 0x01;
		IER = 0x00;
		IME = 0x00;
		SCRX = 0x00;
		SCRY = 0x00;
		LCDY = 0x00;
		LYC = 0x00;
		// BUG FIX: LCDCONTROL/LCDSTATUS/videoMode were never (re-)initialized
		// here at all, leaving LCDC at 0 (display OFF) instead of the real,
		// well-documented DMG post-boot-ROM power-up snapshot (LCDC=$91,
		// STAT=$85 i.e. mode 1/VBlank with the LYC=LY coincidence flag set,
		// since LY=LYC=0 at that point) - this project skips boot ROM
		// emulation entirely and starts straight at $0100, so matching that
        // snapshot exactly is what real cartridge code expects to see.
		LCDCONTROL = 0x91;
		LCDSTATUS = 0x85;
		// BUG FIX: BGPAL/OBJPAL0/OBJPAL1 (the BGP/OBP0/OBP1 palette
		// registers) were never initialized here either, defaulting to 0
		// unless the ROM happened to write to them first. A palette value
		// of 0 makes every one of the 4 possible tile pixel values map to
		// shade 0 (white) - the real post-boot-ROM default is BGP=$FC
		// (the "identity" mapping: color 0->shade0, 1->shade1, 2->shade2,
		// 3->shade3) and OBP0=OBP1=$FF. Without this, background/window
		// tiles render as a blank white screen regardless of their actual
		// pixel data until a game explicitly sets its own palette - which
		// many simple programs (this project's own Blargg-test-derived
		// screenshots included) never bother to do, just like real
		// hardware doesn't require them to, since the boot ROM already
		// set this up.
		BGPAL = 0xFC;
		OBJPAL0 = 0xFF;
		OBJPAL1 = 0xFF;
		// Same idea, for CGB's separate palette RAM: real hardware's CGB
		// boot ROM establishes a reasonable default palette state before
		// handing off to the game (part of its DMG-compatibility-mode
		// setup, though it leaves the hardware in a sane state generally
		// too) - without this, CGB palette RAM starts at all-zero
		// (calloc'd), which maps every possible tile pixel value to
		// black, showing a solid black screen instead of whatever the
		// game's own tile data actually contains until it gets around to
		// setting its own colors. Uses the same white/light-gray/dark-
		// gray/black progression as the DMG default above, replicated
		// across all 8 palettes, as a reasonable "nothing is definitely
		// wrong yet" starting point - a real game sets its own palette
		// data almost immediately regardless.
		if (GBC_MODE) {
			int p;
			WORD defaultColors[4] = {0x7FFF, 0x56B5, 0x2D6B, 0x0000};
			for (p = 0; p < 8; p++) {
				int c;
				for (c = 0; c < 4; c++) {
					BGPALRAM[p * 8 + c * 2] = defaultColors[c] & 0xFF;
					BGPALRAM[p * 8 + c * 2 + 1] = (defaultColors[c] >> 8) & 0xFF;
					OBJPALRAM[p * 8 + c * 2] = defaultColors[c] & 0xFF;
					OBJPALRAM[p * 8 + c * 2 + 1] = (defaultColors[c] >> 8) & 0xFF;
				}
			}
		}
		// Also reset any in-progress H-Blank DMA and speed-switch state
		// - important given this project's ROM-select menu can load a
		// fresh cart without a full process restart, so a previous
		// cart's leftover state must not carry over into the next one.
		HDMA_REMAINING = -1;
		KEY1 = 0;
		// Confirmed against the reference core (Peanut-GB)'s exact reset
		// mechanics: the documented power-up STAT byte ($85, mode=VBlank)
		// is the functionally real starting mode, not just cosmetic - its
		// internal scanline counter runs a full LCD_LINE_CYCLES (456)
		// under that VBlank label before LY ever increments at all, at
		// which point LY jumps straight to 1 in OAM mode (line 0 is never
		// separately numbered at boot). Matching that (videoMode=VBLANK,
		// full VBLANK_CYCLES budget) reproduced the reference's LY
		// progression far more closely than starting fresh in OAMMODE did.
		videoMode = VBLANKMODE;
		VideoCyclesLeft = VBLANK_CYCLES;
		MAXTIME = 1024;
		TIMECOUNTER = 0;
		TIMECNT = 0;
		TIMCONT = 0;
		RTCSELECT = 0;
		RTCLATCH = 0xFF;
		SERIALDATA = 0xFF;
		SERIALCONTROL = 0x00;
		DIVREG = 0;
		DIVCOUNTER = 0;
		MBCMODE = 0;
}

WORD get_rAF(void) {
	return (WORD)((reg_A << 8) | reg_F);
}
WORD get_rBC(void) {
	return (WORD)((reg_B << 8) | reg_C);
}
WORD get_rDE(void) {
	return (WORD)((reg_D << 8) | reg_E);
}
BYTE get_rH(void) {
	return (BYTE)((reg_HL >> 8) & 0xFF);
}
BYTE get_rL(void) {
	return (BYTE)(reg_HL & 0xFF);
}
void put_rAF(WORD r1) {
	reg_A = (r1 >> 8) & 0xFF;
	// BUG FIX: the low nibble of F is hardwired to 0 on real hardware - it can
	// never be set, including by POP AF popping garbage off the stack. Games
	// (and test ROMs) that check flags right after POP AF would see phantom
	// flag bits without this mask.
	reg_F = r1 & 0xF0;
}
void put_rBC(WORD r1) {
	reg_B = (r1 >> 8) & 0xFF;
	reg_C = r1 & 0xFF;
}
void put_rDE(WORD r1) {
	reg_D = (r1 >> 8) & 0xFF;
	reg_E = r1 & 0xFF;
}
void put_rH(BYTE r1) {
	reg_HL = (WORD)((reg_HL & 0x00FF) | ((r1 << 8) & 0xFF00));
}
void put_rL(BYTE r1) {
	reg_HL = (WORD)((reg_HL & 0xFF00) | (r1 & 0x00FF));
}
int getZ(void) {
	return (reg_F & Z_FLAG) == Z_FLAG;
}
int getN(void) {
	return (reg_F & N_FLAG) == N_FLAG;
}
int getH(void) {
	return (reg_F & H_FLAG) == H_FLAG;
}
int getC(void) {
	return (reg_F & C_FLAG) == C_FLAG;
}
void setZ(int flag) {
	reg_F &= ~Z_FLAG;
	if (flag) {
		reg_F |= Z_FLAG;
    }
}
void setN(int flag) {
	reg_F &= ~N_FLAG;
	if (flag) {
		reg_F |= N_FLAG;
	}
}
void setH(int flag) {
	reg_F &= ~H_FLAG;
	if (flag) {
		reg_F |= H_FLAG;
	}
}
void setC(int flag) {
	reg_F &= ~C_FLAG;
	if (flag) {
		reg_F |= C_FLAG;
	}
}
// BUG FIX (sub-instruction timing): previously, every multi-cycle
// instruction performed all of its memory accesses immediately, then
// charged its entire cycle cost in one lump sum afterward - correct for
// the instruction's own final result, but wrong for anything that needs
// to observe state changing *during* the instruction at the right
// sub-instruction (M-cycle) boundary: OAM DMA progress, a Timer/PPU
// event landing exactly between two of an instruction's own memory
// accesses, etc. push() now charges each of its 3 M-cycles (internal
// delay, high-byte write, low-byte write) at the point real hardware
// actually spends that cycle, rather than all 12 T-cycles at the end -
// callers no longer include this in their own cycleLength() call (see
// each PUSH/CALL/RST opcode and the interrupt() dispatcher).
void push(WORD wVal){
	cycleLength(4); // M1: internal delay, no memory access
	WriteMEM(--reg_SP, (wVal >> 8) & 0xFF); // M2: write high byte
	cycleLength(4);
	WriteMEM(--reg_SP, wVal & 0xFF); // M3: write low byte
	cycleLength(4);
}
WORD pop(void){
	// BUG FIX: same idea as push() above, for POP's 2 memory reads.
	BYTE lo = ReadMEM(reg_SP++);
	cycleLength(4); // M1: read low byte
	BYTE hi = ReadMEM(reg_SP++);
	cycleLength(4); // M2: read high byte
	return (WORD)(lo | (hi << 8));
}
void call(void) {
	// BUG FIX (sub-instruction timing): previously pushed reg_PC+2,
	// assuming the caller hadn't yet advanced past the 2 address bytes -
	// now that CALL's own opcodes read those bytes themselves (with
	// individually-charged M-cycles, so DMA/interrupt state observed
	// mid-instruction is correct), reg_PC is already pointing past them
	// by the time this runs, so the return address is just reg_PC as-is.
	push(reg_PC);
}
WORD ret(void){
	return (WORD)pop();
}
WORD rst(WORD addr){
	push(reg_PC);
	return (WORD)addr;
}

void interrupt(void){
	if (IME && (IFLAG & IER)) {
		// BUG FIX: dispatching an interrupt never advanced any cycles at all.
		// Real hardware takes 5 M-cycles (20 T-states) to service an interrupt
		// (2 idle cycles, 2 to push PC like a CALL, 1 to jump to the vector).
		// Since this runs every time ANY interrupt fires - most commonly
		// VBlank, ~60 times a second - missing this caused the emulator's
		// notion of elapsed time to drift further from real hardware the
		// longer a program ran, throwing off anything that reads LY/timers
		// expecting them to line up with a specific instruction.
		//
		// BUG FIX (sub-instruction timing): rst() calls push(), which as
		// of a later commit charges its own 3 M-cycles (1 idle + 2
		// writes = 12T) progressively rather than all at once - this
		// function used to *also* charge the full 20T total afterward,
		// double-counting those 12T and making every interrupt dispatch
		// take 32T instead of 20T. Real hardware's 5 M-cycles break down
		// as idle, idle, write-hi, write-lo, set-PC.
		//
		// BUG FIX (sub-instruction timing, Mooneye's ie_push.gb): this used
		// to push PC via the generic push()/rst() helpers and pick the
		// vector from IF/IE as evaluated *before* the push started. Real
		// hardware re-reads IF & IE for real, live memory locations at
		// each of the two push writes - so if SP happens to alias $FFFF
		// (IE) or $FF0F (IF), a game deliberately doing that (a real,
		// documented hardware quirk, not just a Mooneye curiosity) can
		// have the write to IE/IF itself change or even cancel which
		// interrupt actually gets serviced. The precise rule, confirmed
		// against all 4 of ie_push.gb's rounds by hand: the write of PC's
		// *high* byte (SP-1) happens before the vector is chosen, so it
		// can still affect the outcome; the *low* byte write (SP-2)
		// happens after, so it's always too late to change this
		// dispatch (though the write itself still lands normally). If no
		// enabled+pending interrupt remains once the vector is chosen,
		// the dispatch is cancelled: PC is set to $0000 instead of any
		// vector, and no IF bit is cleared - but IME is still cleared
		// either way, since real hardware commits to that the moment
		// dispatch begins, independent of the outcome.
		WORD returnAddr = reg_PC;
		IME = 0;
		cycleLength(4); // M1: idle
		cycleLength(4); // M2: idle
		WriteMEM(--reg_SP, (returnAddr >> 8) & 0xFF); // M3: write PC high byte
		cycleLength(4);
		int pending = IFLAG & IER;
		WORD vector;
		if 		  (pending & 0x01) { IFLAG &= ~0x01; vector = 0x0040; } // Bit 0: V-Blank
		else if (pending & 0x02) { IFLAG &= ~0x02; vector = 0x0048; } //  Bit 1: LCD
		else if (pending & 0x04) { IFLAG &= ~0x04; vector = 0x0050; } //  Bit 2: Timer Overflow
		else if (pending & 0x08) { IFLAG &= ~0x08; vector = 0x0058; } //  Bit 3: Serial I/O transfer end
		else if (pending & 0x10) { IFLAG &= ~0x10; vector = 0x0060; } //  Bit 4: New Value on Selected Joypad Keyline(s)
		else 					  { vector = 0x0000; } // cancelled: nothing left enabled+pending
		WriteMEM(--reg_SP, returnAddr & 0xFF); // M4: write PC low byte
		cycleLength(4);
		reg_PC = vector;
		cycleLength(4); // M5: idle, set PC
	}
}

// ---- GBC WRAM/VRAM banking helpers ----------------------------------
// CGB has 8 banks of 4KB internal RAM ($C000-$CFFF is always fixed to
// bank 0; $D000-$DFFF switches among banks 1-7 via SVBK, with a value
// of 0 behaving the same as 1 - there's no way to select bank 0 for the
// switchable half, matching real hardware). A plain DMG cart never
// touches SVBK (GBC_MODE gates the write handler), so GetWRAMBank()
// always returns 1 for DMG, giving the exact same fixed 8KB-total WRAM
// layout this project already had before CGB support existed.
// Same idea as GetWRAMBank() above, for CGB's 2-bank VRAM. A DMG cart
// never touches VBK (GBC_MODE gates the write handler), so this always
// returns 0 for DMG, giving the exact same single-8KB-bank VRAM layout
// this project always had.
int GetVRAMBank(void) {
	return GBC_MODE ? (VBK & 0x01) : 0;
}

// Reads directly from a specific VRAM bank regardless of the CPU's own
// current VBK selection - needed because CGB's BG/window tile
// attributes always live in bank 1 at the exact same map address the
// tile number itself occupies in bank 0, accessed "in parallel" by the
// PPU rather than through the CPU's bank-switched view of VRAM. addr
// must be in the normal $8000-$9FFF range.
BYTE ReadVRAMBank(int bank, WORD addr) {
	return VRAM[bank * 0x2000 + (addr - 0x8000)];
}

// Combines a CGB palette RAM entry into the 15-bit RGB555 color it
// represents - palRAM is BGPALRAM or OBJPALRAM, paletteNum is 0-7,
// colorNum is 0-3 (the same tile-pixel 2-bit value DMG rendering uses
// as a direct shade index instead).
WORD GetCGBColor(BYTE *palRAM, int paletteNum, int colorNum) {
	int idx = paletteNum * 8 + colorNum * 2;
	return palRAM[idx] | (palRAM[idx + 1] << 8);
}

int GetWRAMBank(void) {
	if (!GBC_MODE) {
		return 1;
	}
	int bank = SVBK & 0x07;
	return bank ? bank : 1;
}

// Returns a pointer into the 32KB RAM buffer for any address in either
// the real $C000-$DFFF WRAM window or its $E000-$FDFF echo (which
// mirrors it exactly, including which WRAM bank is currently switched
// in for the upper half) - the one place that needs to know about WRAM
// banking, so ReadMEM/WriteMEM's several C000/D000/echo cases all stay
// consistent with each other automatically.
BYTE *WRAMPtr(WORD loc) {
	WORD addr = (loc >= 0xE000) ? (loc - 0x2000) : loc;
	if (addr < 0xD000) {
		return &RAM[addr - 0xC000];
	}
	return &RAM[GetWRAMBank() * 0x1000 + (addr - 0xD000)];
}

// Real size in bytes of the EXTRNRAM buffer for the current cartridge -
// shared by Allocate_Memory (to size the allocation) and the save/load
// calls (to know how much to persist), so the two can never disagree.
// MBC2 has 512x4-bit RAM built into the mapper itself; the cart header's
// RAMSIZE byte is 0 for these carts, which would otherwise size the
// buffer (and any save file) at 0 bytes while WriteMEM/ReadMEM still
// index into it for $A000-$BFFF.
int GetCartRAMSize(void) {
	if ((CARTTYPE == 0x05) || (CARTTYPE == 0x06)) {
		return 512;
	}
	return (iRAMSIZE ? iRAMSIZE : 1) * 1024;
}

// BUG FIX (severe): every cart-RAM bank address calculation used
// RAMBANKNUMBER directly, masked only against its *protocol*-level range
// (2 bits for MBC1, 4 bits for MBC5) - never against how many RAM banks
// this specific cartridge's header actually reports having. A cart with
// only one real 8KB bank but a game (or, as found via Mooneye's
// emulator-only/mbc1/ram_64kb.gb test ROM, a deliberately adversarial
// test of exactly this edge case) selecting a higher bank number still
// within the *protocol's* range would compute an address past the end
// of the real, much smaller EXTRNRAM allocation - confirmed to cause
// real heap corruption (a glibc sysmalloc assertion failure) in the host
// test harness, not just a logic error; on the real console this is
// undefined behaviour writing into whatever happens to sit past the
// buffer instead. Real hardware simply doesn't have the extra physical
// RAM to select in the first place, so the bank number effectively wraps
// within however many banks actually exist.
int GetCartRAMBankCount(void) {
	int banks = GetCartRAMSize() / 0x2000;
	return banks > 0 ? banks : 1;
}

void Allocate_Memory(void){
	// BUG FIX: every buffer here was malloc()'d, never zeroed. malloc does
	// not zero-initialize memory - these came up full of leftover heap
	// garbage on every fresh load, not the consistent "blank" state real
	// hardware's own RAM effectively presents at power-on. Confirmed as a
	// real, visible bug via OAMRAM specifically: garbage sprite-attribute
	// bytes that happened to look "valid" (non-zero X/Y) caused phantom
	// sprites to render (compounded by the separate missing-OBJ-enable-
	// check and pixel-loop bugs fixed alongside this). calloc() zeroes as
	// it allocates, at the same cost as malloc()+memset().
	HIRAM  = (BYTE *)calloc(128, sizeof(BYTE));
	// 16KB (2 banks of 8KB) - CGB's VRAM banking (see VBK/VRAMPtr). A DMG
	// cart never switches banks (VBK stays 0 forever, gated by GBC_MODE
	// in the write handler), so it only ever sees the first 8KB, the
	// exact same buffer size and layout this project always had.
	VRAM   = (BYTE *)calloc(16 * 1024, sizeof(BYTE));
	// 32KB (8 banks of 4KB) - CGB's WRAM banking (see SVBK/WRAMPtr). A
	// DMG cart's GetWRAMBank() always returns 1, so it only ever sees
	// the first 8KB (bank 0 fixed + bank 1 fixed) - the exact same
	// buffer size and layout this project always had.
	RAM    = (BYTE *)calloc(32 * 1024, sizeof(BYTE));
	OAMRAM = (BYTE *)calloc(160, sizeof(BYTE));
	EXTRNRAM = (BYTE *)calloc(GetCartRAMSize(), sizeof(BYTE));
}

void UnAllocate_Memory(void){
	#if defined(DEBUG)
		printf("Unallocating Memory...\n");
	#endif

	//TODO: Check to see if allocated?

	free(HIRAM);
	free(VRAM);
	free(RAM);
	free(ROM);
	free(OAMRAM);
	free(EXTRNRAM);

}

// Called whenever the game writes to SC ($FF02). No real Link Cable / multitap
// support exists yet, so a requested internal-clock transfer completes immediately
// with 0xFF read back (as real hardware would with nothing plugged into the port),
// and the Serial interrupt fires. SerialByteSentHook (if set by the host/harness)
// is given the outgoing byte first -- this is what test ROMs (e.g. Blargg's) use
// to print their pass/fail text, and is also the natural place to eventually hang
// a real PSX-side link cable / debug console feature.
void (*SerialByteSentHook)(BYTE b) = 0;
void onSerialControlWrite(void) {
	if (SERIALCONTROL & 0x80) {
		if (SerialByteSentHook) SerialByteSentHook(SERIALDATA);
		if (SERIALCONTROL & 0x01) { // internal clock: PSX side is the 'master' with nothing attached
			SERIALDATA = 0xFF;
			SERIALCONTROL &= ~0x80;
			IFLAG |= 0x08; // Bit 3: Serial I/O transfer end
		}
	}
}

// OAM DMA: real hardware transfers all 160 bytes over 160 M-cycles (640
// T-cycles), one byte per M-cycle - not instantly, the way this was
// previously implemented. dmaActive/dmaCyclesElapsed/dmaBytesDone track
// an in-progress transfer; DMAClock() (called from cycleLength()
// alongside APUClock() etc.) advances it by however many bytes should
// now be done given the elapsed time, however that time was split
// across instructions.
int dmaActive = 0;
int dmaCyclesElapsed = 0;
int dmaBytesDone = 0;
WORD dmaSourceBase = 0;
// Set to 1 only while DMAClock() performs its own source-data read, so
// ReadMEM's DMA-in-progress restriction (below) doesn't block the DMA
// controller's own bus access - only the CPU's.
int dmaInternalRead = 0;

// BUG FIX (sub-instruction timing): doDMA() is called from inside
// WriteMEM, itself called from inside the triggering LDH (n),A
// instruction's own implementation - *before* that instruction's own
// cycleLength(12) call at its end. Immediately starting DMA's cycle
// count right there effectively let the triggering instruction's own
// remaining 12T count as DMA-elapsed time too, finishing the transfer
// 12T earlier than real hardware (where DMA only starts advancing once
// the triggering instruction has actually finished). dmaPendingStart
// makes DMAClock() consume exactly one cycleLength() call's worth of
// cycles (the remainder of the triggering instruction) before DMA
// actually starts counting toward its own 640T budget.
int dmaPendingStart = 0;

// BUG FIX (sub-instruction timing, Mooneye's oam_dma_start.gb): CPU
// bus-blocking ("reads outside HRAM return $FF") was tied directly to
// dmaActive, which doDMA() sets the instant the trigger write happens -
// but real hardware only actually starts blocking 2 M-cycles *after*
// that write (confirmed against the test's own timing diagram: M=0 the
// write happens, M=1 OAM is still normally accessible, only M=2 is
// where "the new DMA starts" and blocking begins). Tracked with its own
// delay, independent of dmaPendingStart/dmaCyclesElapsed (which govern
// when the *byte transfer itself* starts, a related but different
// question) since they need different answers for a "restarted" DMA
// (a second $FF46 write while a transfer is already active): the test
// confirms the previous transfer's blocking is never interrupted or
// reset by a restart, even though the restart *does* immediately take
// over the actual source address/byte-progress. So dmaBlockPendingStart/
// dmaBlockCyclesElapsed only ever get (re)armed by a genuinely fresh
// start (no transfer was already active) - a restart mid-transfer
// leaves them alone entirely, letting whatever blocking delay was
// already ticking keep ticking on its own original schedule.
int dmaBlockingActive = 0;
int dmaBlockPendingStart = 0;
int dmaBlockCyclesElapsed = 0;

void doDMA(BYTE addr) {
	int wasActive = dmaActive;
	dmaSourceBase = (addr & 0xFF) * 0x0100;
	dmaCyclesElapsed = 0;
	dmaBytesDone = 0;
	dmaActive = 1;
	dmaPendingStart = 1;
	if (!wasActive) {
		dmaBlockPendingStart = 1;
		dmaBlockCyclesElapsed = 0;
		dmaBlockingActive = 0;
	}
}

void DMAClock(int cycles) {
	if (!dmaActive) {
		return;
	}
	if (!dmaBlockingActive) {
		if (dmaBlockPendingStart) {
			dmaBlockPendingStart = 0;
		} else {
			dmaBlockCyclesElapsed += cycles;
			if (dmaBlockCyclesElapsed >= 4) {
				dmaBlockingActive = 1;
			}
		}
	}
	if (dmaPendingStart) {
		// Consume this call's cycles as the remainder of the triggering
		// instruction, not as DMA-elapsed time - see the comment above
		// doDMA().
		dmaPendingStart = 0;
		return;
	}
	dmaCyclesElapsed += cycles;
	int bytesShouldBeDone = dmaCyclesElapsed / 4;
	if (bytesShouldBeDone > 0xA0) {
		bytesShouldBeDone = 0xA0;
	}
	while (dmaBytesDone < bytesShouldBeDone) {
		// dmaInternalRead lets this specific read bypass the "CPU can
		// only see HRAM during active DMA" restriction in ReadMEM below
		// - this is the DMA controller's own read of its source data,
		// not a CPU-initiated one, and real hardware's DMA controller
		// has its own bus access independent of what the CPU can see.
		dmaInternalRead = 1;
		OAMRAM[dmaBytesDone] = ReadMEM((WORD)(dmaSourceBase + dmaBytesDone));
		dmaInternalRead = 0;
		dmaBytesDone++;
	}
	if (dmaBytesDone >= 0xA0) {
		dmaActive = 0;
		dmaBlockingActive = 0;
	}
}

// ---- APU (sound) core -------------------------------------------------
// Implements the 4 real Game Boy sound channels (2 pulse, 1 custom wave,
// 1 noise) at the register/timing level: trigger behavior, length
// counters, volume envelopes, frequency sweep (channel 1 only), and
// per-channel waveform generation. This is the hardware-accurate core;
// see psx.c for how its output actually reaches the PS1's SPU.
//
// Deliberately not chasing every documented edge case (obscure wave-RAM
// corruption-on-retrigger timing, the exact APU-DIV phase a NRx4 write
// needs to land on for an extra length clock, etc.) - the goal is
// correct behavior for the vast majority of real games, not a
// cycle-exact reproduction of every corner of real hardware.

static const int APU_DIVISORS[8] = {8, 16, 32, 48, 64, 80, 96, 112};

int APUDacEnabled12(BYTE nrX2) {
	// Channels 1/2/4 share this NRx2 layout: bits 7-4 = initial volume,
	// bit 3 = envelope direction. The DAC (and so the whole channel) is
	// off whenever both the volume and direction bits are all zero.
	return (nrX2 & 0xF8) != 0;
}

void APUSetNR52Status(void) {
	NR52 = (NR52 & 0x80) | 0x70
		| (apuCh1.enabled ? 0x01 : 0)
		| (apuCh2.enabled ? 0x02 : 0)
		| (apuCh3.enabled ? 0x04 : 0)
		| (apuCh4.enabled ? 0x08 : 0);
}

void APUTriggerPulse(APUChannel *ch, int hasSweep) {
	ch->dacEnabled = APUDacEnabled12(ch->nrX2);
	ch->enabled = ch->dacEnabled;
	if (ch->lengthCounter == 0) {
		ch->lengthCounter = 64;
	}
	int freq = ch->nrX3 | ((ch->nrX4 & 0x07) << 8);
	ch->freqTimer = (2048 - freq) * 4;
	ch->envelopeTimer = (ch->nrX2 & 0x07) ? (ch->nrX2 & 0x07) : 8;
	ch->currentVolume = (ch->nrX2 >> 4) & 0x0F;
	if (hasSweep) {
		ch->shadowFreq = freq;
		int sweepPace = (ch->nrX0 >> 4) & 0x07;
		int sweepShift = ch->nrX0 & 0x07;
		ch->sweepTimer = sweepPace ? sweepPace : 8;
		ch->sweepEnabled = (sweepPace != 0) || (sweepShift != 0);
		if (sweepShift != 0) {
			// Real hardware performs one sweep overflow check
			// immediately on trigger, using the shift but not actually
			// committing the new frequency anywhere - it only matters
			// for whether this immediately disables the channel.
			int newFreq = ch->shadowFreq >> sweepShift;
			newFreq = (ch->nrX0 & 0x08) ? (ch->shadowFreq - newFreq) : (ch->shadowFreq + newFreq);
			if (newFreq > 2047) {
				ch->enabled = 0;
			}
		}
	}
}

void APUTriggerWave(void) {
	apuCh3.dacEnabled = (apuCh3.nrX0 & 0x80) != 0;
	apuCh3.enabled = apuCh3.dacEnabled;
	if (apuCh3.lengthCounter == 0) {
		apuCh3.lengthCounter = 256;
	}
	int freq = apuCh3.nrX3 | ((apuCh3.nrX4 & 0x07) << 8);
	apuCh3.freqTimer = (2048 - freq) * 2;
	apuCh3.wavePos = 0;
}

void APUTriggerNoise(void) {
	apuCh4.dacEnabled = APUDacEnabled12(apuCh4.nrX2);
	apuCh4.enabled = apuCh4.dacEnabled;
	if (apuCh4.lengthCounter == 0) {
		apuCh4.lengthCounter = 64;
	}
	apuCh4.envelopeTimer = (apuCh4.nrX2 & 0x07) ? (apuCh4.nrX2 & 0x07) : 8;
	apuCh4.currentVolume = (apuCh4.nrX2 >> 4) & 0x0F;
	apuCh4.lfsr = 0x7FFF;
	int shift = (apuCh4.nrX3 >> 4) & 0x0F;
	int divisor = APU_DIVISORS[apuCh4.nrX3 & 0x07];
	apuCh4.freqTimer = divisor << shift;
}

void APUStepLength(APUChannel *ch, int lengthEnableBit) {
	if ((ch->nrX4 & lengthEnableBit) && ch->lengthCounter > 0) {
		ch->lengthCounter--;
		if (ch->lengthCounter == 0) {
			ch->enabled = 0;
		}
	}
}

void APUStepSweep(void) {
	if (apuCh1.sweepTimer > 0) {
		apuCh1.sweepTimer--;
	}
	if (apuCh1.sweepTimer == 0) {
		int sweepPace = (apuCh1.nrX0 >> 4) & 0x07;
		apuCh1.sweepTimer = sweepPace ? sweepPace : 8;
		int sweepShift = apuCh1.nrX0 & 0x07;
		if (apuCh1.sweepEnabled && sweepPace != 0) {
			int newFreq = apuCh1.shadowFreq >> sweepShift;
			newFreq = (apuCh1.nrX0 & 0x08) ? (apuCh1.shadowFreq - newFreq) : (apuCh1.shadowFreq + newFreq);
			if (newFreq > 2047) {
				apuCh1.enabled = 0;
			} else if (sweepShift != 0) {
				apuCh1.shadowFreq = newFreq;
				apuCh1.nrX3 = newFreq & 0xFF;
				apuCh1.nrX4 = (apuCh1.nrX4 & 0xF8) | ((newFreq >> 8) & 0x07);
				// Second overflow check with the new value, matching
				// real hardware performing the calculation twice.
				int checkFreq = apuCh1.shadowFreq >> sweepShift;
				checkFreq = (apuCh1.nrX0 & 0x08) ? (apuCh1.shadowFreq - checkFreq) : (apuCh1.shadowFreq + checkFreq);
				if (checkFreq > 2047) {
					apuCh1.enabled = 0;
				}
			}
		}
	}
}

void APUStepEnvelope(APUChannel *ch) {
	int pace = ch->nrX2 & 0x07;
	if (pace == 0) {
		return; // envelope disabled entirely while pace is 0
	}
	if (ch->envelopeTimer > 0) {
		ch->envelopeTimer--;
	}
	if (ch->envelopeTimer == 0) {
		ch->envelopeTimer = pace;
		int increasing = (ch->nrX2 & 0x08) != 0;
		if (increasing && ch->currentVolume < 15) {
			ch->currentVolume++;
		} else if (!increasing && ch->currentVolume > 0) {
			ch->currentVolume--;
		}
	}
}

// Called once every 8192 T-cycles (512Hz) - see APUClock() below for
// where that period comes from.
void APUStepFrameSequencer(void) {
	// Step 0,2,4,6: length (256Hz). Steps 2,6: sweep (128Hz), after
	// length. Step 7: envelope (64Hz).
	if ((apuFrameSeqStep % 2) == 0) {
		APUStepLength(&apuCh1, 0x40);
		APUStepLength(&apuCh2, 0x40);
		APUStepLength(&apuCh3, 0x40);
		APUStepLength(&apuCh4, 0x40);
	}
	if (apuFrameSeqStep == 2 || apuFrameSeqStep == 6) {
		APUStepSweep();
	}
	if (apuFrameSeqStep == 7) {
		APUStepEnvelope(&apuCh1);
		APUStepEnvelope(&apuCh2);
		APUStepEnvelope(&apuCh4);
	}
	apuFrameSeqStep = (apuFrameSeqStep + 1) % 8;
}

// Advances all 4 channels' own waveform-generation timers by `cycles`
// T-cycles, and the 512Hz frame sequencer alongside them. Called once
// per instruction from cycleLength(), the same place DIV/the CPU timer/
// the PPU are all driven from.
void APUClock(int cycles) {
	if (!(NR52 & 0x80)) {
		return; // master sound off - real hardware halts all APU clocking
	}

	apuFrameSeqCounter += cycles;
	while (apuFrameSeqCounter >= 8192) {
		apuFrameSeqCounter -= 8192;
		APUStepFrameSequencer();
	}

	apuCh1.freqTimer -= cycles;
	while (apuCh1.freqTimer <= 0) {
		int freq = apuCh1.nrX3 | ((apuCh1.nrX4 & 0x07) << 8);
		apuCh1.freqTimer += (2048 - freq) * 4;
		apuCh1.dutyPos = (apuCh1.dutyPos + 1) % 8;
	}
	apuCh2.freqTimer -= cycles;
	while (apuCh2.freqTimer <= 0) {
		int freq = apuCh2.nrX3 | ((apuCh2.nrX4 & 0x07) << 8);
		apuCh2.freqTimer += (2048 - freq) * 4;
		apuCh2.dutyPos = (apuCh2.dutyPos + 1) % 8;
	}
	apuCh3.freqTimer -= cycles;
	while (apuCh3.freqTimer <= 0) {
		int freq = apuCh3.nrX3 | ((apuCh3.nrX4 & 0x07) << 8);
		apuCh3.freqTimer += (2048 - freq) * 2;
		apuCh3.wavePos = (apuCh3.wavePos + 1) % 32;
	}
	apuCh4.freqTimer -= cycles;
	while (apuCh4.freqTimer <= 0) {
		int shift = (apuCh4.nrX3 >> 4) & 0x0F;
		int divisor = APU_DIVISORS[apuCh4.nrX3 & 0x07];
		apuCh4.freqTimer += divisor << shift;
		int xorBit = (apuCh4.lfsr & 0x01) ^ ((apuCh4.lfsr >> 1) & 0x01);
		apuCh4.lfsr = (apuCh4.lfsr >> 1) | (xorBit << 14);
		if (apuCh4.nrX3 & 0x08) { // narrow (7-bit) mode
			apuCh4.lfsr = (apuCh4.lfsr & ~0x40) | (xorBit << 6);
		}
	}

	APUSetNR52Status();
}

static const BYTE APU_DUTY_TABLE[4] = {0x01, 0x81, 0x87, 0x7E}; // 12.5/25/50/75%, MSB-first per step

// Returns this channel's current output, 0-15 (before NR50/NR51 mixing),
// or -1 if its DAC is off (silent, contributes nothing - matches real
// hardware's DAC producing no signal at all rather than a "0" sample).
int APUChannelOutput(int channelNum) {
	switch (channelNum) {
		case 1: {
			if (!apuCh1.enabled || !apuCh1.dacEnabled) return -1;
			int bit = (APU_DUTY_TABLE[(apuCh1.nrX1 >> 6) & 0x03] >> (7 - apuCh1.dutyPos)) & 0x01;
			return bit ? apuCh1.currentVolume : 0;
		}
		case 2: {
			if (!apuCh2.enabled || !apuCh2.dacEnabled) return -1;
			int bit = (APU_DUTY_TABLE[(apuCh2.nrX1 >> 6) & 0x03] >> (7 - apuCh2.dutyPos)) & 0x01;
			return bit ? apuCh2.currentVolume : 0;
		}
		case 3: {
			if (!apuCh3.enabled || !apuCh3.dacEnabled) return -1;
			BYTE sampleByte = WAVERAM[apuCh3.wavePos / 2];
			int sample4bit = (apuCh3.wavePos % 2 == 0) ? (sampleByte >> 4) : (sampleByte & 0x0F);
			int shift;
			switch ((apuCh3.nrX2 >> 5) & 0x03) {
				case 0: shift = 4; break; // mute
				case 1: shift = 0; break; // 100%
				case 2: shift = 1; break; // 50%
				default: shift = 2; break; // 25%
			}
			return sample4bit >> shift;
		}
		case 4: {
			if (!apuCh4.enabled || !apuCh4.dacEnabled) return -1;
			int bit = (~apuCh4.lfsr) & 0x01;
			return bit ? apuCh4.currentVolume : 0;
		}
	}
	return -1;
}

long long g_totalSysCycles = 0;
void cycleLength(int cycle) {
	// GBC double-speed mode: the CPU core runs twice as fast, but the
	// PPU/DIV/Timer are driven by the fixed system clock, which does
	// NOT double - so for the same number of CPU cycles just executed,
	// only half as much real system-clock time has actually passed.
	// Every GB instruction's cycle cost is a multiple of 4, so halving
	// here is always exact (no fractional cycles lost to rounding).
	int sysCycle = (GBC_MODE && (KEY1 & 0x80)) ? cycle / 2 : cycle;
	// Lightweight, always-on cumulative system-clock counter - cheap
	// (one 64-bit add) and useful for exactly the kind of investigation
	// that found the VideoCyclesLeft overshoot bug below: measuring the
	// actual elapsed cycles between two events empirically, rather than
	// assuming they match a textbook constant.
	g_totalSysCycles += sysCycle;
	// BUG FIX (real hardware DIV/TIMA coupling): replaced the previous
	// independent DIVCOUNTER/TIMECOUNTER pair with the real, unified
	// 16-bit hardware counter model - see internalDivCounter16's
	// declaration comment for the full explanation of why (div_write.gb/
	// rapid_toggle.gb both specifically test the falling-edge behavior
	// this enables). Iterated one T-cycle at a time rather than in a
	// single bulk step: sysCycle is always small (at most ~24), and
	// per-T-cycle precision avoids any risk of a multi-cycle jump
	// mis-detecting (or double-detecting) an edge, or of the reload-
	// delay countdown being collapsed the way an earlier, coarser
	// attempt at that fix was (see timaReloadPending's own history).
	int divTick;
	for (divTick = 0; divTick < sysCycle; divTick++) {
		internalDivCounter16 = (internalDivCounter16 + 1) & 0xFFFF;
		CheckTimerEdge();
		if (timaReloadPending > 0) {
			timaReloadPending -= 1;
			if (timaReloadPending <= 0) {
				timaReloadPending = 0;
				TIMECNT = TIMEMOD;
			}
		}
	}
	DIVREG = (BYTE)(internalDivCounter16 >> 8);
	APUClock(sysCycle);
	AudioSampleHook(sysCycle);
	DMAClock(sysCycle);
	// BUG FIX: the PPU mode/LY state machine below ran completely
	// unconditionally, even while LCDC bit 7 (LCD/PPU enable) is 0 - real
	// hardware freezes LY, the STAT mode bits, and critically the STAT
	// LYC-coincidence bit (it stops being recomputed, so it keeps
	// whatever value it last had) the instant the display is turned off,
	// and only resumes ticking once it's turned back on. See the LCDC
	// write handler (case 0xFF40) for the actual on/off transition
	// itself (LY/mode reset, coincidence bit left untouched on power-off).
	// Confirmed via Mooneye's stat_lyc_onoff.gb, which checks exactly
	// this freeze-and-resume behavior across several LYC/LCDC sequences.
	if (!(LCDCONTROL & 0x80)) { return; }
	VideoCyclesLeft -= sysCycle;
	if(VideoCyclesLeft <= 0) { // Video
		if((videoMode == HBLANKMODE) || (videoMode == VBLANKMODE)){
			LCDY++;
			// BUG FIX: was wrapping at 0x100 (256) instead of 154 (144 visible
			// lines + 10 VBlank lines) - real hardware's LY never exceeds 153.
			// This alone made VBlank last roughly 10x too long relative to a
			// real frame.
			if (LCDY >= 154){
				LCDY = 0;
			}
			if (LCDY == 0) {
				// BUG FIX: previously, wrapping LCDY to 0 left videoMode
				// stuck at VBLANKMODE - the transition back to OAMMODE for
				// the new frame's line 0 only happened on the *next* call,
				// by which point LCDY had already silently ticked to 1,
				// skipping line 0's OAM-search phase entirely. FURTHER BUG
				// FIX: the fix as first written also skipped calling
				// hblank() for line 0 specifically, reasoning that "we're
				// coming from VBlank, not finishing a rendered scanline" -
				// that reasoning was wrong. hblank() is what actually
				// renders the background/window/sprite pixel data for the
				// line via DrawBGline() etc; line 0 is a completely normal
				// visible line like any other and still needs that call,
				// or its entire row of pixels is silently left blank
				// forever. Confirmed visually: dumping the emulator's own
				// rendered framebuffer (not just serial/register state, as
				// every prior test in this project relied on) showed real
				// text reduced to a few stray pixels until this was fixed.
				hblank();
				videoMode = OAMMODE;
				// BUG FIX: was a plain assignment, discarding whatever
				// this line's HBlank/VBlank countdown had already
				// overshot into negative territory (VideoCyclesLeft -=
				// sysCycle can undershoot past 0 by a few cycles, since
				// instruction costs - 4,8,12,... - rarely divide these
				// mode-length constants evenly) - losing that overshoot
				// at literally hundreds of mode transitions every
				// single frame adds up to a real, measurable amount:
				// this alone made a full frame take roughly 71800+
				// cycles rather than the correct 70224, found by
				// directly measuring the actual elapsed cycles between
				// consecutive VBlank interrupts rather than assuming
				// they matched the textbook constant. Adding onto the
				// (already non-positive) remaining value instead
				// carries the overshoot forward into the new mode's
				// countdown, losing nothing.
				VideoCyclesLeft += OAM_CYCLES;
				if ((LCDSTATUS >> 5) & 0x01) { IFLAG |= 0x02; }
			} else if (LCDY < 0x90) {
				hblank();
				videoMode = OAMMODE;
				// BUG FIX: see the identical fix a few lines up (line 0's
				// case) for the full explanation - same overshoot-losing
				// bug, same fix.
				VideoCyclesLeft += OAM_CYCLES;
				if ((LCDSTATUS >> 3) & 0x01) { IFLAG |= 0x02; } // LCD 3
			} else {
				videoMode = VBLANKMODE;
				VideoCyclesLeft += VBLANK_CYCLES; // BUG FIX: see the overshoot comment above OAM_CYCLES
				if (LCDY == 0x90) {
					vblank();
					// BUG FIX (severe): the dedicated VBlank interrupt (IF
					// bit 0) was incorrectly gated on STAT bit 4 - it was
					// only ever requested if the game had ALSO opted into
					// STAT's separate "fire the LCD STAT interrupt at
					// VBlank too" feature. Real hardware fires the
					// dedicated VBlank interrupt completely unconditionally
					// every single time LY reaches 144; STAT bit 4 only
					// controls whether the *STAT* interrupt (IF bit 1)
					// *additionally* fires at that same moment for
					// programs that prefer to handle everything through
					// one unified STAT interrupt path instead. Since most
					// games (any that just HALT waiting on the ordinary,
					// dedicated VBlank interrupt without touching STAT's
					// interrupt-source-enable bits at all - an extremely
					// common, arguably the single most common wait pattern
					// in the entire GB library) never set STAT bit 4, this
					// meant IF bit 0 could never be set at all, hanging any
					// such game in HALT forever waiting for an interrupt
					// that would never come. Found via real-world testing
					// with an actual commercial ROM (Pokemon Red) hanging
					// completely a fraction of a second into booting -
					// every synthetic/test-ROM check this project had
					// relied on before now happened not to exercise this
					// exact, extremely common pattern.
					IFLAG |= 0x01;
					if ((LCDSTATUS >> 4) & 0x01) { IFLAG |= 0x02; }
				}
			}
			if (LCDY == LYC) { IFLAG |= 0x02; } // 3
			// BUG FIX: STAT's mode bits (0-1) and LYC-coincidence bit (2)
			// were never synced with the actual PPU state anywhere - only
			// ever set by a direct software write to $FF41, which then sat
			// frozen forever after. Any code polling STAT for raster timing
			// (very common in real games, and exactly what stricter timing
			// tests check) would see permanently stale mode/coincidence
			// bits. Bits 3-7 (interrupt-source enables + unused) are left
			// exactly as software last set them.
			LCDSTATUS = (LCDSTATUS & 0xF8) | (videoMode & 0x03) | ((LCDY == LYC) ? 0x04 : 0x00);
			return;
		} else {
			if (videoMode == OAMMODE) {
				videoMode = TRANSFERMODE;
				VideoCyclesLeft += TRANSFER_CYCLES; // BUG FIX: see the overshoot comment above OAM_CYCLES
				if ((LCDSTATUS >> 5) & 0x01) { IFLAG |= 0x02; } //3
				LCDSTATUS = (LCDSTATUS & 0xF8) | (videoMode & 0x03) | ((LCDY == LYC) ? 0x04 : 0x00);
				return;
			}
			if (videoMode == TRANSFERMODE) {
				videoMode = HBLANKMODE;
				VideoCyclesLeft += HBLANK_CYCLES; // BUG FIX: see the overshoot comment above OAM_CYCLES
				LCDSTATUS = (LCDSTATUS & 0xF8) | (videoMode & 0x03) | ((LCDY == LYC) ? 0x04 : 0x00);
				return;
			}
		}
  	}
}

void doCycles(){


}
int CyclesLeft(){
	cyclesLeft = 1000000; // Big Number

	if ((LCDCONTROL >> 7) == 0x01){
		if(VideoCyclesLeft < cyclesLeft) {
			cyclesLeft = VideoCyclesLeft;
		}
	}
	// Timer
	if ((TIMCONT >> 2) & 0x01){
		if ((MAXTIME - TIMECOUNTER) < cyclesLeft){
			cyclesLeft = (MAXTIME - TIMECOUNTER);
		}
	}
	return cyclesLeft;
}

void hblank(){

 	int WINaddr;
 	int BGaddr;
 	int TILEaddr;

	// H-Blank DMA continuation: transfer one 16-byte block per H-Blank
	// (this function is called once per scanline, at the same point a
	// real H-Blank period begins) until the whole transfer completes -
	// see the HDMA5 write handler for how a transfer starts and why
	// General-Purpose DMA doesn't need this (it already finished
	// immediately when it was started).
	if (GBC_MODE && HDMA_REMAINING > 0) {
		int k;
		for (k = 0; k < 16; k++) {
			WriteMEM((WORD)(HDMA_DST + k), ReadMEM((WORD)(HDMA_SRC + k)));
		}
		HDMA_SRC += 16;
		HDMA_DST += 16;
		HDMA_REMAINING -= 16;
	}

 	if ((LCDCONTROL >> 6) & 0x01) { WINaddr = 0x9C00;  } else { WINaddr = 0x9800; }
 	if ((LCDCONTROL >> 3) & 0x01) { BGaddr = 0x9C00;   } else { BGaddr = 0x9800;}
 	if ((LCDCONTROL >> 4) & 0x01) { TILEaddr = 0x8000; } else { TILEaddr = 0x9000; }


	if ((LCDCONTROL >> 7) & 0x01) {
		if(LCDCONTROL & 0x01) {
 			DrawBGline(LCDY, BGaddr, TILEaddr);
		}

 		if((LCDCONTROL >> 5) & 0x01){
			if(!(WNDX > 166 || WNDY > LCDY)) {
				DrawWINline(LCDY, WINaddr, TILEaddr);
			}
		}
		// BUG FIX: sprites were rendered unconditionally, with no check of
		// LCDC bit 1 (OBJ display enable) at all. Real hardware shows no
		// sprites whatsoever when this bit is clear, regardless of what's
		// in OAM.
		if ((LCDCONTROL >> 1) & 0x01) {
			DrawOBJline(LCDY, 0x8000);
		}
	}
}

void DrawBGline(int line, int BGaddr, int TILEaddr) {
	int bx, by;
	int tileNo = 0;
	int colour;
	int oldtileNo = -1;
 	BYTE B1, B2;
	bx = SCRX;
	// BUG FIX: real hardware treats the background as a wrapping 256x256
	// pixel plane - (SCRY + LCDY) must wrap modulo 256 before being used
	// to index the tile map, or scrolling anywhere near the bottom of
	// that range walks off into unrelated memory (the other tile map at
	// $9C00, or beyond it) once the sum exceeds 255.
	by = (SCRY + LCDY) & 0xFF;

	if (GBC_MODE) {
		// CGB adds a per-tile attribute byte (palette, VRAM bank for
		// tile data, X/Y flip, BG-to-OBJ priority) stored in VRAM bank 1
		// at the exact same map address the tile number itself occupies
		// in bank 0. This is a genuinely separate code path from the
		// DMG one below, rather than threading CGB-only branches through
		// it, since the attribute byte changes several things at once
		// (which bank the tile data comes from, which row/column of it
		// to read) that would otherwise need re-deriving per pixel.
		//
		// BG-to-OBJ priority (attribute bit 7) is not yet applied here -
		// a real, separate gap, since resolving it needs coordinating
		// with sprite rendering in a later pass over the same line.
		int mapAddr = 0;
		BYTE attr = 0;
		for (i = 0; i < 160; i++) {
			int newMapAddr = BGaddr + (by / 8) * 32 + (i + bx) / 8;
			if (newMapAddr != mapAddr) {
				mapAddr = newMapAddr;
				tileNo = ReadVRAMBank(0, mapAddr);
				attr = ReadVRAMBank(1, mapAddr);
				int tileBank = (attr >> 3) & 0x01;
				int yflip = (attr >> 6) & 0x01;
				int tileRow = by % 8;
				if (yflip) {
					tileRow = 7 - tileRow;
				}
				// Tile number is signed (range -128..127, relative to
				// TILEaddr=$9000) or unsigned (0..255, relative to
				// TILEaddr=$8000) depending on LCDC bit 4, which is what
				// TILEaddr itself already encodes here.
				int effTileNo = (TILEaddr == 0x8000) ? (unsigned char) tileNo : (signed char) tileNo;
				int tileDataAddr = TILEaddr + effTileNo * 16 + tileRow * 2;
				B1 = ReadVRAMBank(tileBank, tileDataAddr);
				B2 = ReadVRAMBank(tileBank, tileDataAddr + 1);
			}
			int xflip = (attr >> 5) & 0x01;
			int bitPos = xflip ? (i % 8) : (7 - (i % 8));
			int colorNum;
			if (((B1 >> bitPos) & 0x01) == 1) {
				colorNum = ((B2 >> bitPos) & 0x01) == 1 ? 3 : 2;
			} else {
				colorNum = ((B2 >> bitPos) & 0x01) == 1 ? 1 : 0;
			}
			int paletteNum = attr & 0x07;
			screenBufferColor[(LCDY * 160) + i] = GetCGBColor(BGPALRAM, paletteNum, colorNum);
			screenBuffer[(LCDY * 160) + i] = colorNum;
			bgAttrPriority[(LCDY * 160) + i] = (attr >> 7) & 0x01;
		}
		return;
	}

	for (i = 0; i < 160; i++) {
		if (BGaddr == 0x9C00) {//
			tileNo = (signed int)ReadMEM(BGaddr + (by/8) * 32 + (i+ bx)/8);
		} else {
			tileNo = (unsigned char)ReadMEM(BGaddr + (by/8) * 32 + (i+bx)/8);
		}
		if (tileNo != oldtileNo) {
	 		B1 = (unsigned char)ReadMEM(TILEaddr + (tileNo) * 16  + (by%8)*2 );
	 		B2 = (unsigned char)ReadMEM(TILEaddr + (tileNo) * 16  + (by%8)*2 + 1 );
	 		oldtileNo = tileNo;
		}
	 	if (((B1 >> (7-(i%8))) & 0x01) == 1) {
			if (((B2 >> (7-(i%8))) & 0x01) == 1) { colour = (BGPAL >> 6) & 0x3; //3;
			} else { colour = (BGPAL >> 4) & 0x3; //2;
			}
	 	} else if (((B2 >> 7-(i%8)) & 0x01) == 1) { colour = (BGPAL >> 2) & 0x3; //1;
	 	} else { colour = BGPAL & 0x3; //0;
	 	}
 		screenBuffer[(LCDY * 160) + i] = colour;
	}
}



void DrawWINline(int line, int WINaddr, int TILEaddr) {
	int tileNo = 0;
	int colour = 0;
	int bx, by;
	int oldtileNo = -1;
	BYTE B1, B2;
	int Transparency = LCDCONTROL & 0x1;
	by = LCDY - WNDY;

	if (GBC_MODE) {
		// Same treatment as DrawBGline's CGB path - see its comments for
		// why this is a separate code path rather than threading CGB
		// branches through the DMG loop below.
		int mapAddr = 0;
		BYTE attr = 0;
		for (i = 0; i < 160; i++) {
			int newMapAddr = WINaddr + (by / 8) * 32 + (i) / 8;
			if (newMapAddr != mapAddr) {
				mapAddr = newMapAddr;
				tileNo = ReadVRAMBank(0, mapAddr);
				attr = ReadVRAMBank(1, mapAddr);
				int tileBank = (attr >> 3) & 0x01;
				int yflip = (attr >> 6) & 0x01;
				int tileRow = by % 8;
				if (yflip) {
					tileRow = 7 - tileRow;
				}
				int effTileNo = (TILEaddr == 0x8000) ? (unsigned char) tileNo : (signed char) tileNo;
				int tileDataAddr = TILEaddr + effTileNo * 16 + tileRow * 2;
				B1 = ReadVRAMBank(tileBank, tileDataAddr);
				B2 = ReadVRAMBank(tileBank, tileDataAddr + 1);
			}
			int xflip = (attr >> 5) & 0x01;
			int bitPos = xflip ? (i % 8) : (7 - (i % 8));
			int colorNum;
			if (((B1 >> bitPos) & 0x01) == 1) {
				colorNum = ((B2 >> bitPos) & 0x01) == 1 ? 3 : 2;
			} else {
				colorNum = ((B2 >> bitPos) & 0x01) == 1 ? 1 : 0;
			}
			int paletteNum = attr & 0x07;
			screenBufferColor[(LCDY * 160) + i + WNDX - 7] = GetCGBColor(BGPALRAM, paletteNum, colorNum);
			screenBuffer[(LCDY * 160) + i + WNDX - 7] = colorNum;
			bgAttrPriority[(LCDY * 160) + i + WNDX - 7] = (attr >> 7) & 0x01;
		}
		return;
	}

	for (i = 0; i < 160; i++) {
		if (WINaddr == 0x9C00) {
			tileNo = (signed int)ReadMEM(WINaddr + (by/8) * 32 + (i)/8);
		} else {
			tileNo = (unsigned char)ReadMEM(WINaddr + (by/8) * 32 + (i)/8);
		}
		if (tileNo != oldtileNo) {
			 B1 = (unsigned char)ReadMEM(TILEaddr + ((tileNo) * 16)  + (by%8)*2 );
			 B2 = (unsigned char)ReadMEM(TILEaddr + ((tileNo) * 16)  + (by%8)*2 + 1 );
			 oldtileNo = tileNo;
		}
		// BUG FIX: window tiles never went through the BGP palette
		// lookup at all - real hardware uses the same BGP register for
		// both background and window rendering, so a game whose BGP
		// isn't the identity mapping ($E4) would see the window's
		// colors come out wrong (a plain 0-3 shade index instead of
		// whatever BGP actually maps that index to).
		if (((B1 >> (7-(i%8))) & 0x01) == 1) {
			if (((B2 >> (7-(i%8))) &0x01) == 1) { colour = (BGPAL >> 6) & 0x3;
			} else { colour = (BGPAL >> 4) & 0x3;
			}
		} else if (((B2 >> (7-(i%8))) &0x01) == 1) { colour = (BGPAL >> 2) & 0x3;
		} else {
			if(Transparency == 1){
				colour = screenBuffer[(LCDY * 160) + i + WNDX - 7]; // Or Better yet, skip the output.
			} else {
				colour = BGPAL & 0x3;
			}
		}
		screenBuffer[(LCDY * 160) + i + WNDX - 7] = colour;
	}
}

void DrawOBJline(int line, int TILEaddr) {

	BYTE by;  // Byte0  Y position on the screen
	BYTE bx;  // Byte1  X position on the screen
	BYTE tileNo = 0x00; // Byte2  Pattern number 0-255
	BYTE bflag = 0x00; 	/* Byte3  Flags:

	         Bit7  Priority
	               If this bit is set to 0, sprite is displayed
	               on top of background & window. If this bit
	               is set to 1, then sprite will be hidden behind
	               colors 1, 2, and 3 of the background & window.
	               (Sprite only prevails over color 0 of BG & win.)
	         Bit6  Y flip
	               Sprite pattern is flipped vertically if
	               this bit is set to 1.
	         Bit5  X flip
	               Sprite pattern is flipped horizontally if
	               this bit is set to 1.
	         Bit4  Palette number
	               Sprite colors are taken from OBJ1PAL if
	               this bit is set to 1 and from OBJ0PAL
	               otherwise.
*/
	int iflipx = 0;
	int iflipy = 0;
	int ipal   = 0;
	BYTE B1, B2;
	int colour;
	int pos;
	// BUG FIX (severe): the inner per-pixel loop used the OUTER sprite-index
	// variable `i` for its own condition/increment instead of `j` - meaning
	// every iteration of what should have been an independent 0-7 pixel
	// loop was instead corrupting the outer 0-39 sprite loop's counter
	// directly. Combined with the missing OBJ-enable check and uninitialized
	// OAM below, this produced essentially random garbage sprite pixels
	// wherever OAM happened to contain non-zero bytes - confirmed to be the
	// real cause of stray/incoherent pixels found while visually verifying
	// this project's rendering output for the first time this session
	// (every previous test only ever checked serial output or CPU/flag
	// state, never the actual rendered pixels).
	//
	// Also fixed while rewriting this loop: the "color 1" branch checked
	// bit position `i` directly instead of `7-i` like the other three
	// branches - inconsistent with the MSB-first bit ordering every other
	// tile decoder in this file uses, so it was reading the wrong pixel's
	// bit for all but one column position.
	//
	// Sprite X/Y flipping (iflipx/iflipy, computed above but never actually
	// applied to the pixel indexing here) remains a known, separate gap -
	// out of scope for this fix, tracked in STATUS.md.
	int i, j;
	// BUG FIX: 8x16 sprite mode (LCDC bit 2) was never supported - the
	// bounding-box check and row lookup both hardcoded an 8-pixel-tall
	// sprite. In 8x16 mode, real hardware ignores bit 0 of the OAM tile
	// number and uses two consecutive tiles (N with bit0 forced to 0,
	// then N+1) stacked as one 16-pixel-tall sprite; Y-flip mirrors the
	// whole 16-pixel sprite (which also swaps which physical tile ends
	// up "on top"), not each 8-pixel half independently.
	int spriteHeight = ((LCDCONTROL >> 2) & 0x01) ? 16 : 8;
	for ( i = 0; i < 40; i++) {
		pos = i * 4;
		by = OAMRAM[pos] & 0xFF;
		bx = OAMRAM[pos + 1] & 0xFF;
		tileNo = OAMRAM[pos + 2] & 0xFF;
		bflag = OAMRAM[pos + 3] & 0xFF;
		if (( bx != 0x00 && by != 0x00) && (by <=line + 16) && (by > line + (16 - spriteHeight))) {  // 8/16
			// BUG FIX (severe): this always fetched row 0 of the tile
			// (offset +0/+1) no matter which of the sprite's 8 scanlines
			// was actually being drawn - every row of every sprite showed
			// the same top row repeated, instead of each scanline showing
			// its own row of the tile. Real hardware picks the row from
			// how far into the sprite's height the current scanline
			// (`line`) is; Y-flip (iflipy, computed below) just mirrors
			// which row that ends up being.
			iflipy = (bflag & 0x40) == 0x40;
			int spriteRow = line - (by - 16); // 0..(spriteHeight-1)
			if (iflipy) {
				spriteRow = (spriteHeight - 1) - spriteRow;
			}
			int effectiveTileNo = tileNo;
			if (spriteHeight == 16) {
				effectiveTileNo = (tileNo & 0xFE) + (spriteRow >= 8 ? 1 : 0);
				spriteRow = spriteRow % 8;
			}
			B1 = (unsigned char)ReadMEM(TILEaddr + ((effectiveTileNo) * 16) + spriteRow * 2 );
			B2 = (unsigned char)ReadMEM(TILEaddr + ((effectiveTileNo) * 16) + spriteRow * 2 + 1 );

			iflipx = (bflag & 0x20) == 0x20;
			ipal   = (bflag & 0x10) == 0x10;

			// Sprite-vs-background priority (bflag bit 7, "OBJ-to-BG
			// priority"): real hardware never actually hides a sprite
			// pixel outright for this - it only affects whether the
			// sprite draws on top of or behind BG/window colors 1-3 (BG
			// color 0 never hides a sprite, on DMG or CGB, regardless of
			// this bit). On CGB, LCDC bit 0 changes meaning entirely (it
			// stops being "BG/window enable" and becomes "BG/Window
			// Master Priority" instead) - when clear, sprites always
			// draw on top of everything regardless of any other
			// priority bit at all; when set, per-tile CGB BG-priority
			// (bgAttrPriority, set in DrawBGline/DrawWINline) can
			// additionally force specific BG/window tiles to draw over
			// sprites even where the sprite's own priority bit says it
			// should be on top. This was never implemented at all
			// before - the comment previously here just said "Hidden
			// (Priority Bit 7)" with no actual code reading that bit
			// anywhere, so every sprite always drew on top of
			// everything unconditionally.

			if (GBC_MODE) {
				// CGB sprites use a 3-bit palette index (bflag bits 0-2,
				// selecting one of OBJPALRAM's 8 palettes) instead of
				// DMG's 1-bit OBJPAL0/OBJPAL1 selector, and bit3 selects
				// which VRAM bank the tile data itself comes from - same
				// idea as the BG/window attribute byte in DrawBGline/
				// DrawWINline, just packed into the sprite's own OAM
				// flags byte instead of a separate per-tile attribute.
				int tileBank = (bflag >> 3) & 0x01;
				B1 = ReadVRAMBank(tileBank, TILEaddr + effectiveTileNo * 16 + spriteRow * 2);
				B2 = ReadVRAMBank(tileBank, TILEaddr + effectiveTileNo * 16 + spriteRow * 2 + 1);
				int paletteNum = bflag & 0x07;
				for (j = 0; j < 8; j++) {
					int bit = iflipx ? j : (7 - j);
					int colorNum;
					if (((B1 >> bit) & 0x01) == 1) {
						colorNum = ((B2 >> bit) & 0x01) == 1 ? 3 : 2;
					} else {
						colorNum = ((B2 >> bit) & 0x01) == 1 ? 1 : 0;
					}
					// BUG FIX: sprite color 0 is always transparent on
					// real hardware (shows whatever's underneath) - this
					// was never actually skipped, just drawn like any
					// other color. Much more visually obvious for CGB,
					// where OBJPALRAM's color 0 has no reason to happen
					// to match the background the way DMG's usual
					// white-ish color 0 often visually does.
					if (colorNum == 0) {
						continue;
					}
					// BUG FIX: real hardware's OAM X coordinate is the
					// sprite's screen column plus 8 (X=8 means the
					// sprite's left edge sits at screen column 0) - this
					// was subtracting 7 instead of 8, shifting every
					// single sprite one pixel to the right of where it
					// should be, in every game, the whole time. Found
					// via a synthetic priority test whose sprite and
					// background pixels landed at different screen
					// columns than intended, which made two deliberately
					// different test cases produce identical output
					// until this was found and fixed.
					if ((bx - 8 + j < 160) && (bx - 8 + j >= 0)) {
						int px = (line * 160) + bx - 8 + j;
						int bgColorNum = screenBuffer[px];
						// LCDC bit 0 as CGB Master Priority: sprites
						// always win when it's clear, full stop.
						if (LCDCONTROL & 0x01) {
							if (bgAttrPriority[px] && bgColorNum != 0) {
								continue; // BG tile's own priority wins
							}
							if ((bflag & 0x80) && bgColorNum != 0) {
								continue; // sprite's own priority bit: behind BG 1-3
							}
						}
						screenBufferColor[px] = GetCGBColor(OBJPALRAM, paletteNum, colorNum);
						screenBuffer[px] = colorNum;
					}
				}
				continue;
			}

			for (j = 0; j < 8; j++) {

				// BUG FIX: X-flip was computed above but never actually
				// applied anywhere - reading bit (7-j) unconditionally
				// always drew sprites in their normal orientation
				// regardless of the flip flag. Flipping horizontally
				// just means reading bit j itself instead of its mirror.
				int bit = iflipx ? j : (7 - j);
				int colorNum;
				if (((B1 >> bit) & 0x01) == 0x01) {
					colorNum = ((B2 >> bit) & 0x01) == 0x01 ? 3 : 2;
				} else {
					colorNum = ((B2 >> bit) & 0x01) == 0x01 ? 1 : 0;
				}
				// BUG FIX: see the CGB branch above - color 0 is always
				// transparent on real hardware, never drawn at all.
				if (colorNum == 0) {
					continue;
				}
				if (ipal) { // Use OBJPAL1
					colour = (OBJPAL1 >> (colorNum * 2)) & 0x3;
				} else {
					colour = (OBJPAL0 >> (colorNum * 2)) & 0x3;
				}
				// BUG FIX: see the CGB branch above - same X-coordinate
				// off-by-one (subtracting 7 instead of 8).
				if ((bx - 8 + j < 160) && (bx - 8 + j >= 0)) {
					int px = (line * 160) + bx - 8 + j;
					// DMG sprite-vs-BG priority (bflag bit 7): see the
					// comment above the CGB branch - this hides the
					// sprite behind BG/window colors 1-3 specifically,
					// never behind BG color 0.
					if ((bflag & 0x80) && screenBuffer[px] != 0) {
						continue;
					}
					screenBuffer[px] = colour;
				}
			}
		}
	}
}



void vblank(void){

//	curframe--;
//	if(curframe < 0) {
//		curframe = frameskip;
		FRAMECOUNT++;
		// Audio update runs every frame regardless of LCD state - sound
		// is independent of the display, and a game with the LCD off
		// but sound playing (rare but real) should still be heard.
		UpdateAudio();
		if ((LCDCONTROL >> 7) == 0x01) { // LCD ON

			// BUG FIX: Draw_Buffer (via DrawBG()) was only ever called
			// when BG rendering was ALSO enabled (LCDCONTROL bit 0) -
			// meaning a game that turns off BG but leaves sprites on
			// (a real, legal combination - DMG software occasionally
			// does this deliberately) would have hblank() correctly draw
			// sprite pixels into screenBuffer all frame, then never
			// actually present that frame at all, every single VBlank.
			// The frame should be presented whenever the LCD itself is
			// on, regardless of which specific layers are enabled.
			PrepScreen();
			DrawBG();
			RenderWorld(0,0,0);

				//if(((LCDCONTROL >> 5) & 0x01) == 0x01) {
				//	DrawWindow();
				//}

				//sprintf(buff, "PC: %04X inst:%02X A:%02X", reg_PC, ROM[reg_PC], reg_A); //writeFont(buff, 0, -170, 60);

		}
//	}
}
void runEmu(){

	loadRom();
	reset_Z80();

	// Load any existing battery-backed save for this cartridge now that
	// loadRom()/reset_Z80() have parsed the header and sized EXTRNRAM.
	// A missing save (first time playing this game) is not an error -
	// EXTRNRAM is already zeroed by Allocate_Memory(), the same blank
	// state a fresh, never-saved-to real cartridge would present.
	if (CartHasBattery()) {
		LoadCartRAMAndRTC((const char *)CARTTITLE);
		RAM_DIRTY = 0;
	}

    temp = 0;
    Voff = 2048;
	runto = 0;

	#if defined(DEBUG)
		printf("Emulation START...\n");
	#endif

	while (EMULATING) {
		if ( IME && (IFLAG & IER) != 0) {
			interrupt();
		}
		//if (reg_PC == 0x3E6) { runto = 1; }
		pad = PadRead(0);
		if ((pad & Pad1R1) && (pad & Pad1R2) && (pad  & Pad1L1) && (pad  & Pad1L2) && (pad  & Pad1Select) && (pad  & Pad1Start)){
		//	// User wants to QUIT Emulation
			EMULATING=0;
		}
		#if defined(DEBUG)
		if (pad & Pad1R2){
			printf("PC:%04X | OP:%02X | HL:%04X | F:%02X | A:%02X | B:%02X | C:%02X | D:%02X | E:%02X | SP:%04X\n", reg_PC, (BYTE)ROM[reg_PC], reg_HL, reg_F, reg_A, reg_B, reg_C, reg_D, reg_E, reg_SP);
		}
		#endif

		//if (reg_PC > 0x0273) {
		//	printf("PC:%04X OP:%02X HL:%04X A:%02X B:%02X \n", reg_PC, (BYTE)ROM[reg_PC], reg_HL, reg_A, reg_B);
		//}
		//printf("PC:%04X OP:%02X A:%02X B:%02X C:%02X D:%02X E:%02X F:%02X\n",reg_PC, (BYTE)ROM[reg_PC], reg_A, reg_B, reg_C, reg_D, reg_E, reg_F);
		//printf("HL:%04X SP:%04X Z:%d N:%d H:%d C:%d\n", reg_HL, reg_SP, getZ(), getN(), getH(), getC());

		instructions[ReadMEM(reg_PC++)]();

		// BUG FIX: apply EI's delayed effect here, after the instruction
		// following EI has fully executed - see the EI_PENDING comment
		// above its declaration for why this delay matters and how it
		// was found.
		if (EI_PENDING > 0) {
			EI_PENDING--;
			if (EI_PENDING == 0) {
				IME = 1;
			}
		}

		//if (cyclesLeft <= 0){
		//	doCycles();
		//}

		//temp ++;
		/*if ((runto == 1) && (temp > 1000)) {
			PrepScreen();
			temp = 0;
			DrawBG();
			//ROM[reg_PC] = 0xC9;
			sprintf(buff, "X%02X Y%02X LCDY%02X", SCRX, SCRY, LCDY);
			writeFont(buff, 0, -170, 0);
			sprintf(buff, "%04X %02X%02X Z:%dN:%dH:%dC:%d", (ReadMEM(reg_SP) << 8 | ReadMEM(reg_SP +1)), ReadMEM(reg_SP +2), ReadMEM(reg_SP +3), getZ(), getN(), getH(), getC()); //*romHeader.type);
			writeFont(buff, 0, -170, 20);
			sprintf(buff, "IER: %04X IF: %02X IME %d", IER, IFLAG, IME);
			writeFont(buff, 0, -170, 40);
			sprintf(buff, "PC: %04X inst:%02X (LBO %02X)", reg_PC, ROM[reg_PC], illegalOpcodes);
			writeFont(buff, 0, -170, 60);
        	sprintf(buff, "A: %02X B: %02X C: %02X D: %02X ", reg_A, reg_B, reg_C, reg_D);
			writeFont(buff, 0, -170, 80);
			sprintf(buff, "E: %02X HL: %04X SP: %04X", reg_E, reg_HL, reg_SP);
			writeFont(buff, 0, -170, 100);

			pad = PadRead(0);
    	    RenderWorld(0,0,0);
    	    while (!(pad & Pad1x)){
    	  		pad = PadRead(0);
    	   		if ((pad & Pad1L2) && (pad & Pad1R2)) {
			   		while (!((pad & Pad1x) && (pauseMnuPos == 0))) {
						if (pad & Pad1Up) if (pauseMnuPos != 0) pauseMnuPos--;
						if (pad & Pad1Down) if (pauseMnuPos != 5) pauseMnuPos++;
						if (pad & Pad1x) {
							if (pauseMnuPos == 1) { /// Save
							}
							if (pauseMnuPos == 2) { /// Load
							}
							if (pauseMnuPos == 3) { /// Reset
							}
							if (pauseMnuPos == 4) { /// Options
							}
							if (pauseMnuPos == 5) { /// Exit
							//free memory..
							return;
							}
						}

						PrepScreen();
						for (i = 0; i < 6; i++) {
							writeFont(pauseData[i], 0, -70, 20 * i -30);
							if (i == pauseMnuPos) writeFont("[-         -]", 0, -100, 20 * pauseMnuPos - 30);
						}
						RenderWorld(0x14,0x21, 0x76);
						pad = PadRead(0);
			  		}
		   		}
		    }
		}*/
	}
	//TODO:
	//Unload Rom
	UnAllocate_Memory();
}

// Whether the currently loaded cartridge has battery-backed RAM worth
// saving/loading to a memory card. Matches this project's actual
// supported MBC types (MBC1/2/3/5) plus the plain ROM+RAM+BATTERY case;
// cart types this project doesn't implement bank switching for at all
// (MMM01, MBC4, HuC1, HuC3, Pocket Camera, TAMA5) are deliberately
// excluded even though some of them are nominally battery-backed too.
int CartHasBattery(void) {
	switch (CARTTYPE) {
		case 0x03: // MBC1+RAM+BATTERY
		case 0x06: // MBC2+BATTERY
		case 0x09: // ROM+RAM+BATTERY
		case 0x0F: // MBC3+TIMER+BATTERY
		case 0x10: // MBC3+TIMER+RAM+BATTERY
		case 0x13: // MBC3+RAM+BATTERY
		case 0x1B: // MBC5+RAM+BATTERY
		case 0x1E: // MBC5+RUMBLE+RAM+BATTERY
			return 1;
		default:
			return 0;
	}
}

// Whether this cartridge has the MBC3 real-time-clock chip - only the
// two MBC3+TIMER variants (games like Pokemon Gold/Silver/Crystal use
// this for their day-night cycle and breeding features), not the
// RAM-only MBC3 types (0x11-0x13, e.g. Pokemon Red/Blue's actual cart
// type, 0x13, has no RTC at all despite being "close" in the type list).
int CartHasRTC(void) {
	return (CARTTYPE == 0x0F) || (CARTTYPE == 0x10);
}

// Number of extra bytes CartHasRTC() carts need alongside their normal
// cart RAM in a save file - the five live RTC registers. This project's
// RTC is software-controlled only (games set it directly; there's no
// real-time-driven auto-increment simulated), so persisting it is just
// carrying these five bytes across a save/load round trip like any other
// piece of save state - no wall-clock/elapsed-time math involved.
#define RTC_SAVE_BYTES 5

int GetCartSaveSize(void) {
	return GetCartRAMSize() + (CartHasRTC() ? RTC_SAVE_BYTES : 0);
}

// Combines EXTRNRAM with the five RTC registers (if this cart has an
// RTC) into one buffer and hands it to the platform's SaveCartRAM, so
// carts with a real-time clock (Pokemon Gold/Silver/Crystal and similar)
// don't lose their in-game clock every time the console is turned off -
// previously only plain cart RAM was ever saved, silently resetting the
// RTC to power-on defaults on every single boot regardless of whether
// the cartridge actually has a battery-backed clock chip.
int SaveCartRAMAndRTC(const char *saveId) {
	int ramSize = GetCartRAMSize();
	int totalSize = GetCartSaveSize();
	// static, not stack-local: 32KB covers the largest real MBC3 RAM size,
	// but that's a substantial chunk to put on the stack on a platform
	// with as little RAM as the PS1, especially since this can be called
	// from inside WriteMEM (itself potentially called from fairly deep
	// interrupt-handling contexts) - a static buffer costs the same BSS
	// space either way but carries no stack-depth risk.
	static BYTE buf[32 * 1024 + RTC_SAVE_BYTES];
	memcpy(buf, EXTRNRAM, ramSize);
	if (CartHasRTC()) {
		buf[ramSize + 0] = RTC_S;
		buf[ramSize + 1] = RTC_M;
		buf[ramSize + 2] = RTC_H;
		buf[ramSize + 3] = RTC_DL;
		buf[ramSize + 4] = RTC_DH;
	}
	return SaveCartRAM(saveId, buf, totalSize);
}

int LoadCartRAMAndRTC(const char *saveId) {
	int ramSize = GetCartRAMSize();
	int totalSize = GetCartSaveSize();
	static BYTE buf[32 * 1024 + RTC_SAVE_BYTES]; // see SaveCartRAMAndRTC above
	int ok = LoadCartRAM(saveId, buf, totalSize);
	if (!ok) {
		return 0;
	}
	memcpy(EXTRNRAM, buf, ramSize);
	if (CartHasRTC()) {
		RTC_S  = buf[ramSize + 0];
		RTC_M  = buf[ramSize + 1];
		RTC_H  = buf[ramSize + 2];
		RTC_DL = buf[ramSize + 3];
		RTC_DH = buf[ramSize + 4];
	}
	return 1;
}

void loadRom(void){
	int i;


	for (i = 0; i < 16; i++){
		CARTTITLE[i] = ROM[0x134+i];
	}
	// GBC detection: $80 = CGB-enhanced but still DMG-compatible, $C0 =
	// CGB-only. Anything else (including the common case of this byte
	// simply being part of an older-style 16-byte title with no CGB
	// flag at all) means a plain DMG cart.
	GBC_MODE = (ROM[0x143] == 0x80) || (ROM[0x143] == 0xC0);
	CARTTYPE = ROM[0x147];
	ROMSIZE  = ROM[0x148];
	RAMSIZE  = ROM[0x149];
	MANUCODE[0]=ROM[0x14A];
	MANUCODE[1]=ROM[0x14B];
	VERSIONNUMBER=ROM[0x14C];

		if(RAMSIZE == 0x00) {  iRAMSIZE = 0; }
		if(RAMSIZE == 0x01) {  iRAMSIZE = 2; }
		if(RAMSIZE == 0x02) {  iRAMSIZE = 8; }
		if(RAMSIZE == 0x03) {  iRAMSIZE = 32; }

		if(ROMSIZE == 0x00) { iROMSIZE = 2; }
		if(ROMSIZE == 0x01) { iROMSIZE = 4; }
		if(ROMSIZE == 0x02) { iROMSIZE = 8; }
		if(ROMSIZE == 0x03) { iROMSIZE = 16; }
		if(ROMSIZE == 0x04) { iROMSIZE = 32; }
		if(ROMSIZE == 0x05) { iROMSIZE = 64; }
		if(ROMSIZE == 0x06) { iROMSIZE = 128; }
		// BUG FIX: the standard ROMSIZE encoding continues to 0x08 (8MB/
		// 512 banks), but this table stopped at 0x06 (2MB/128 banks) -
		// leaving iROMSIZE at 0 for any larger cart, which then caused a
		// real division-by-zero crash in the ROMBANKNUMBER masking fix
		// added alongside this (found via Mooneye's emulator-only/mbc5/
		// rom_32Mb.gb and rom_64Mb.gb tests, which specifically exercise
		// these two largest standard sizes).
		if(ROMSIZE == 0x07) { iROMSIZE = 256; }
		if(ROMSIZE == 0x08) { iROMSIZE = 512; }

	#if defined(DEBUG)
		printf("Name:      %s\n", CARTTITLE);

		printf("Cart Type: %X [", CARTTYPE);
		if(CARTTYPE == 0x00) printf("ROM");
		if(CARTTYPE == 0x01) printf("MBC1");
		if(CARTTYPE == 0x02) printf("MBC1+RAM");
		if(CARTTYPE == 0x03) printf("MBC1+RAM+BATTERY");
		if(CARTTYPE == 0x05) printf("MBC2");
		if(CARTTYPE == 0x06) printf("MBC2+BATTERY");
		if(CARTTYPE == 0x08) printf("ROM+RAM");
		if(CARTTYPE == 0x09) printf("ROM+RAM+BATTERY");
		if(CARTTYPE == 0x0B) printf("MMM01");
		if(CARTTYPE == 0x0C) printf("MMM01+RAM");
		if(CARTTYPE == 0x0D) printf("MMM01+RAM+BATTERY");
		if(CARTTYPE == 0x0F) printf("MBC3+TIMER+BATTERY");
		if(CARTTYPE == 0x10) printf("MBC3+TIMER+RAM+BATTERY");
		if(CARTTYPE == 0x11) printf("MBC3");
		if(CARTTYPE == 0x12) printf("MBC3+RAM");
		if(CARTTYPE == 0x13) printf("MBC3+RAM+BATTERY");
		if(CARTTYPE == 0x15) printf("MBC4");
		if(CARTTYPE == 0x16) printf("MBC4+RAM");
		if(CARTTYPE == 0x17) printf("MBC4+RAM+BATTERY");
		if(CARTTYPE == 0x19) printf("MBC5");
		if(CARTTYPE == 0x1A) printf("MBC5+RAM");
		if(CARTTYPE == 0x1B) printf("MBC5+RAM+BATTERY");
		if(CARTTYPE == 0x1C) printf("MBC5+RUMBLE");
		if(CARTTYPE == 0x1D) printf("MBC5+RUMBLE+RAM");
		if(CARTTYPE == 0x1E) printf("MBC5+RUMBLE+RAM+BATTERY");
		if(CARTTYPE == 0xFC) printf("POCKET CAMERA");
		if(CARTTYPE == 0xFD) printf("Bandai TAMA5");
		if(CARTTYPE == 0xFE) printf("HuC3");
		if(CARTTYPE == 0xFF) printf("HuC1+RAM+BATTERY");
		printf("]\n");


		printf("Rom Size: %X [", ROMSIZE);
		if(ROMSIZE == 0x00) printf("32k");
		if(ROMSIZE == 0x01) printf("64k");
		if(ROMSIZE == 0x02) printf("128k");
		if(ROMSIZE == 0x03) printf("256k");
		if(ROMSIZE == 0x04) printf("512k");
		if(ROMSIZE == 0x05) printf("1024k");
		if(ROMSIZE == 0x06) printf("2048k");
		if(ROMSIZE == 0x07) printf("4096k");
		printf( "]\n");

		printf("Ram Size: %X\n", RAMSIZE);
		if(RAMSIZE == 0x00) { printf("0k"); }
		if(RAMSIZE == 0x01) { printf("2k"); }
		if(RAMSIZE == 0x02) { printf("8k"); }
		if(RAMSIZE == 0x03) { printf("32k");  }
		printf( "\n");

		printf("Manu. Code: %X %X\n", MANUCODE[0], MANUCODE[1]);
		printf("Version: %X\n", VERSIONNUMBER);
	#endif


}

BYTE ReadMEM(WORD loc) {
    	// BUG FIX (sub-instruction timing): real hardware's OAM DMA
    	// controller has exclusive bus access to everything except HRAM
    	// while a transfer is active - the CPU reads back $FF for
    	// anything outside $FF80-$FFFE during that window (a real,
    	// documented restriction some games' precise DMA-timing code
    	// depends on, and exactly what Mooneye's push_timing/pop_timing
    	// tests exercise by running code with SP pointing into OAM while
    	// a transfer is in progress). Gated on dmaBlockingActive, not
    	// dmaActive - see its declaration comment for why blocking starts
    	// 2 M-cycles after the trigger write, not immediately.
    	if (dmaBlockingActive && !dmaInternalRead && (loc < 0xFF80 || loc > 0xFFFE)) {
    		return 0xFF;
    	}
    	if (loc < 0x4000) {  // ROM Bank 0
			return ROM[loc];
		}
		if (loc < 0x8000) { // $4000-$7FFF - ROM Bank n
			// BUG FIX (severe): ROMBANKNUMBER was used completely
			// unmasked against how many banks the loaded ROM file
			// actually contains (iROMSIZE) - a game/test selecting a
			// bank number beyond that (real hardware would just wrap/
			// mirror, having no more physical ROM to address) read
			// straight past the end of the ROM buffer. Confirmed to
			// segfault outright in the host test harness on several of
			// Mooneye's emulator-only/mbc1|mbc5/rom_*.gb tests, which
			// deliberately probe exactly this edge case; on the real
			// console this is undefined behaviour reading whatever
			// happens to sit past the buffer instead of crashing
			// outright, but no less wrong.
        	return ROM[loc + ((ROMBANKNUMBER % (iROMSIZE ? iROMSIZE : 1)) - 1 ) * 0x4000];
		}
		if ( loc < 0xA000 ) { // $8000-$9FFF - VRAM
			return VRAM[GetVRAMBank() * 0x2000 + (loc - 0x8000)];
		}
		if ( loc < 0xC000 ) { // $A000-$BFFF - External (cartridge) RAM / MBC3 RTC
			if (( CARTTYPE >= 0x0F ) && ( CARTTYPE <= 0x13 ) && ( RTCSELECT >= 0x08 )) {
				switch (RTCSELECT) {
					case 0x08: return RTCL_S;  break;
					case 0x09: return RTCL_M;  break;
					case 0x0A: return RTCL_H;  break;
					case 0x0B: return RTCL_DL; break;
					case 0x0C: return RTCL_DH; break;
					default: return 0xFF; break;
				}
			}
			if (!RAMENABLED) { return 0xFF; } // Real hardware reads open bus (~0xFF) while RAM is disabled
			if (( CARTTYPE == 0x01 ) || ( CARTTYPE == 0x02) || ( CARTTYPE == 0x03 )) {
				if (MBCMODE) { // 4/32 mode
					return EXTRNRAM[loc - 0xA000 + (WORD)((RAMBANKNUMBER % GetCartRAMBankCount()) * 0x2000)];
				} else {
					return EXTRNRAM[loc - 0xA000 ];
				}
			}
			// BUG FIX (severe): MBC2's built-in RAM is only 512 bytes,
			// but the generic "always bank via RAMBANKNUMBER" path below
			// computes (loc - 0xA000) directly - up to 8191 for the full
			// $A000-$BFFF window - reading (and, on the write side,
			// writing) far past the end of the real 512-byte buffer.
			// Real MBC2 hardware only has that much RAM physically
			// present, wired so it mirrors (repeats) across the entire
			// window rather than treating it as 8KB of distinct storage.
			// Found by testing a real MBC2 game (Kirby's Pinball Land)
			// that never progressed past a blank screen - most likely
			// reading back garbage from past the buffer where real
			// hardware would read its own mirrored data instead.
			if (( CARTTYPE == 0x05 ) || ( CARTTYPE == 0x06 )) {
				return EXTRNRAM[(loc - 0xA000) % 512];
			}
			// MBC2/3/5 - always bank via RAMBANKNUMBER
			return EXTRNRAM[loc - 0xA000 + (WORD)((RAMBANKNUMBER % GetCartRAMBankCount()) * 0x2000)];
		}
		if ( loc < 0xE000 ) { // $C000-$DFFF - Internal RAM
			return *WRAMPtr(loc);
		}
		if ( loc < 0xFE00  ) { // $E000-$FDFF - Reserved Area/Echo RAM
	        return *WRAMPtr(loc);
		}
		if ( loc < 0xFEA0  ) { // $FE00-$FE9F - Object Attribute Memory (OAM)
			return OAMRAM[loc - 0xFE00];

		}
		if ( ( loc >= 0xFF00 ) &&  ( loc <= 0xFF7F ) ) { // $FF00-$FF7F - Hardware I/O Registers

			if (loc >= 0xFF30 && loc <= 0xFF3F) { // Wave RAM
				return WAVERAM[loc - 0xFF30];
			}

			switch (loc) {
				case 0xFF00: return (BYTE)P1; break; // P1 (R/W)
				case 0xFF01: return (BYTE)SERIALDATA; break; // Serial transfer data (R/W)
				case 0xFF02: return (BYTE)(SERIALCONTROL | 0x7E); break; // SIO control (R/W), unused bits read as 1
				case 0xFF04: return DIVREG; break; // Divider Register (R/W)
				case 0xFF05: return (BYTE)TIMECNT; break;// Timer counter (R/W)
				case 0xFF06: return (BYTE)TIMEMOD; break;// Timer Modulo (R/W)
				case 0xFF07: return (BYTE)TIMCONT; break; // Timer Control
				case 0xFF0F: return (BYTE)(IFLAG | 0xE0); break; // Interrupt Flag (R/W), unused bits read as 1

				// SOUND - many bits across these registers are
				// write-only on real hardware and read back as 1
				// regardless of what was last written; the OR masks
				// below reproduce that exactly (Pan Docs' documented
				// per-register read masks).
				case 0xFF10: return apuCh1.nrX0 | 0x80; break; // NR10 sweep
				case 0xFF11: return apuCh1.nrX1 | 0x3F; break; // NR11 duty/length
				case 0xFF12: return apuCh1.nrX2; break;        // NR12 envelope
				case 0xFF13: return 0xFF; break;                // NR13 freq lo (write-only)
				case 0xFF14: return apuCh1.nrX4 | 0xBF; break; // NR14 freq hi/trigger/length-enable

				case 0xFF16: return apuCh2.nrX1 | 0x3F; break; // NR21
				case 0xFF17: return apuCh2.nrX2; break;        // NR22
				case 0xFF18: return 0xFF; break;                // NR23 (write-only)
				case 0xFF19: return apuCh2.nrX4 | 0xBF; break; // NR24

				case 0xFF1A: return apuCh3.nrX0 | 0x7F; break; // NR30 DAC on/off
				case 0xFF1B: return 0xFF; break;                // NR31 (write-only)
				case 0xFF1C: return apuCh3.nrX2 | 0x9F; break; // NR32 output level
				case 0xFF1D: return 0xFF; break;                // NR33 (write-only)
				case 0xFF1E: return apuCh3.nrX4 | 0xBF; break; // NR34

				case 0xFF20: return 0xFF; break;                // NR41 (write-only)
				case 0xFF21: return apuCh4.nrX2; break;        // NR42
				case 0xFF22: return apuCh4.nrX3; break;        // NR43
				case 0xFF23: return apuCh4.nrX4 | 0xBF; break; // NR44

				case 0xFF24: return NR50; break;
				case 0xFF25: return NR51; break;
				case 0xFF26: APUSetNR52Status(); return NR52 | 0x70; break;



			// VIDEO
				case 0xFF40: return (BYTE)LCDCONTROL; break; // LCD Control (R/W)
				case 0xFF41: return (BYTE)(LCDSTATUS | 0x80); break; // LCDC Status (R/W), unused bit 7 reads as 1
				case 0xFF42: return (BYTE)SCRY; break; // Scroll Y   (R/W)
				case 0xFF43: return (BYTE)SCRX; break; // Scroll X   (R/W)
				case 0xFF44: return (BYTE)LCDY; break; // LCDC Y-Coordinate (R)
				case 0xFF45: return (BYTE)LYC; break; // LY Compare  (R/W)
				case 0xFF47: return (BYTE)BGPAL; break;// BG Palette Data  (W)
				case 0xFF48: return (BYTE)OBJPAL0; break; // Object Palette 0 Data (W)
				case 0xFF49: return (BYTE)OBJPAL1; break; // Object Palette 1 Data (W)
				case 0xFF4A: return (BYTE)WNDY; break; // Window Y Position  (R/W)
				case 0xFF4B: return (BYTE)WNDX; break; // Window X Position  (R/W)
				// GBC registers - real hardware returns these with their
				// unused upper bits read back as 1, which some games'
				// hardware-detection code checks for.
				case 0xFF4D: return (BYTE)(KEY1 | 0x7E); break; // KEY1 - speed switch (R/W)
				case 0xFF4F: return (BYTE)(VBK | 0xFE); break; // VBK - VRAM bank (R/W)
				case 0xFF55: // HDMA5 - VRAM DMA status (R/W)
					// Bit 7 clear = no H-Blank DMA in progress (General-
					// Purpose transfers always finish immediately, so
					// this only ever reflects H-Blank DMA state); bits
					// 0-6 = remaining length in 16-byte blocks minus 1.
					return (BYTE)(HDMA_REMAINING < 0 ? 0xFF : (((HDMA_REMAINING / 16) - 1) & 0x7F));
					break;
				case 0xFF68: return (BYTE)(BCPS | 0x40); break; // BCPS/BGPI - BG palette index (R/W)
				case 0xFF69: return BGPALRAM[BCPS & 0x3F]; break; // BCPD/BGPD - BG palette data (R/W)
				case 0xFF6A: return (BYTE)(OCPS | 0x40); break; // OCPS/OBPI - OBJ palette index (R/W)
				case 0xFF6B: return OBJPALRAM[OCPS & 0x3F]; break; // OCPD/OBPD - OBJ palette data (R/W)
				case 0xFF70: return (BYTE)(SVBK | 0xF8); break; // SVBK - WRAM bank (R/W)

				default: return 0x00; break;
			}

		} else if ( ( loc >= 0xFF80 ) &&  ( loc <= 0xFFFE ) ) { // $FF80-$FFFE - High RAM Area
			return HIRAM[loc - 0xFF80];
		} else if  ( loc == 0xFFFF ) { // $FFFF - Interrupt Enable Register
	        return (BYTE)IER;
		}
         else { return 0x00; }

}

void WriteMEM(WORD loc, BYTE b){
	// BUG FIX (sub-instruction timing): same restriction as ReadMEM
	// above - the CPU can't write anywhere but HRAM while OAM DMA is
	// actively transferring, since the DMA controller owns the bus.
	// Real hardware simply ignores such writes rather than redirecting
	// or erroring. $FF46 itself (the DMA trigger register) is always
	// writable regardless - it has to be, since that's how a game
	// re-triggers the very next transfer, typically once per frame,
	// and real hardware never blocks this register's own trigger.
	// (Investigated as a candidate cause for a real Pokemon Red
	// regression during this same work - it turned out not to be the
	// actual cause that time, but keeping the exception regardless
	// since it's independently correct real-hardware behavior either
	// way.)
	if (dmaBlockingActive && loc != 0xFF46 && (loc < 0xFF80 || loc > 0xFFFE)) {
		return;
	}
	if ( loc <= 0x1FFF ) { // $0000-$1FFF - RAM Enable (MBC1/2/3/5)
		if (CARTTYPE != 0x00) {
			int wasEnabled = RAMENABLED;
			RAMENABLED = ((b & 0x0F) == 0x0A);
			// Real MBC1/3/5 cartridges commit their save to the battery-
			// backed RAM chip around the point the game disables RAM
			// access again (having enabled it briefly to read/write save
			// data is the normal access pattern) - mirroring that same
			// transition is a natural, low-overhead point to persist to
			// the memory card, rather than saving on every single write
			// or trying to guess some other "the game is done" moment.
			if (wasEnabled && !RAMENABLED && RAM_DIRTY && CartHasBattery()) {
				if (SaveCartRAMAndRTC((const char *)CARTTITLE)) {
					RAM_DIRTY = 0;
				}
			}
		}
	} else if ( loc <= 0x3FFF ) { // $2000-$3FFF - ROM Bank number (low bits)
		// MBC1
		if (( CARTTYPE == 0x01 ) || ( CARTTYPE == 0x02) || ( CARTTYPE == 0x03 )) {
			// BUG FIX (severe): the bank-0-becomes-1 quirk checked the RAW
			// unmasked byte for zero, not the masked 5-bit value that
			// actually becomes the new bank number. Real hardware only
			// has 5 physical bits to look at; software setting the upper
			// 3 bits to garbage (which is legal - those lines simply
			// aren't connected to anything on this register) while
			// writing an otherwise-zero bank number must still trigger
			// the quirk. Found via Mooneye's emulator-only/mbc1 ROM-size
			// tests, whose own switch_bank routine deliberately does
			// exactly this ("or %11100000 ; set high bits to expose
			// bugs") - and did.
			BYTE lower5 = b & 0x1F;
			if (!lower5) lower5 = 1;
			ROMBANKNUMBER = (ROMBANKNUMBER & ~0x1F) | lower5;
			#if defined(DEBUG)
			printf("Switching MBC1 to %d. [PC: %04X | LOC: %04X]\n", ROMBANKNUMBER, reg_PC, loc);
			#endif
		}
		// MBC2
		if (( CARTTYPE == 0x05 ) || ( CARTTYPE == 0x06 )) {
			// BUG FIX: same class of masked-vs-raw quirk-check bug as
			// MBC1 above, for MBC2's 4-bit bank number.
			BYTE lower4 = b & 0x0F;
			if (!lower4) lower4 = 1;
			ROMBANKNUMBER = lower4;
			#if defined(DEBUG)
			printf("Switching MBC2 to %d. [PC: %04X | LOC: %04X]\n", ROMBANKNUMBER, reg_PC, loc);
			#endif
		}
		// MBC3 - full 7-bit bank number in one write, bank 0 -> bank 1 quirk (same as MBC1)
		if (( CARTTYPE >= 0x0F ) && ( CARTTYPE <= 0x13 )) {
			// BUG FIX: same class of masked-vs-raw quirk-check bug as
			// MBC1 above, for MBC3's 7-bit bank number.
			BYTE lower7 = b & 0x7F;
			if (!lower7) lower7 = 1;
			ROMBANKNUMBER = lower7;
			#if defined(DEBUG)
			printf("Switching MBC3 to %d. [PC: %04X | LOC: %04X]\n", ROMBANKNUMBER, reg_PC, loc);
			#endif
		}
		// MBC5 - 9-bit bank number split across two write windows, NO bank-0 quirk
		if (( CARTTYPE >= 0x19 ) && ( CARTTYPE <= 0x1E )) {
			if (loc <= 0x2FFF) { // low 8 bits
				ROMBANKNUMBER = (ROMBANKNUMBER & 0x100) | b;
			} else { // $3000-$3FFF - bit 8
				ROMBANKNUMBER = (ROMBANKNUMBER & 0x0FF) | ((b & 0x01) << 8);
			}
			#if defined(DEBUG)
			printf("Switching MBC5 to %d. [PC: %04X | LOC: %04X]\n", ROMBANKNUMBER, reg_PC, loc);
			#endif
		}
	} else if ( ( loc >= 0x4000 ) && ( loc <= 0x7FFF ) ) { // $4000-$7FFF - RAM Bank / upper ROM bits / RTC select
		if ( loc <= 0x5FFF ) {
			// MBC1 - in 4/32 mode, these 2 bits pick the RAM bank; in 16/8 mode they're the
			// two high bits of a >512KB ROM's bank number instead (rare; not yet modelled).
			if (( CARTTYPE == 0x01 ) || ( CARTTYPE == 0x02) || ( CARTTYPE == 0x03 )) {
				if (MBCMODE) {
					RAMBANKNUMBER = (b & 0x03);
				} else {
					ROMBANKNUMBER = (ROMBANKNUMBER & 0x1F) | ((b & 0x03) << 5);
				}
			}
			// MBC3 - $00-$03 selects a RAM bank; $08-$0C selects an RTC register to map instead
			if (( CARTTYPE >= 0x0F ) && ( CARTTYPE <= 0x13 )) {
				if (b <= 0x03) {
					RAMBANKNUMBER = b;
					RTCSELECT = 0;
				} else if ((b >= 0x08) && (b <= 0x0C)) {
					RTCSELECT = b;
				}
			}
			// MBC5 - full 4-bit RAM bank number (bit 3 doubles as the rumble motor on +RUMBLE carts;
			// rumble output isn't emulated, so it's harmlessly ignored here)
			if (( CARTTYPE >= 0x19 ) && ( CARTTYPE <= 0x1E )) {
				RAMBANKNUMBER = (b & 0x0F);
			}
			#if defined(DEBUG)
			printf("RAM/RTC select %d. [PC: %04X | LOC: %04X]\n", b, reg_PC, loc);
			#endif
		} else { // $6000-$7FFF
			// MBC1 mode select
			if (( CARTTYPE == 0x01 ) || ( CARTTYPE == 0x02) || ( CARTTYPE == 0x03 )) {
				MBCMODE = (b & 0x01);
				#if defined(DEBUG)
				printf(MBCMODE ? "4/32 Memory mode selected\n" : "16/8 Memory mode selected\n");
				#endif
			}
			// MBC3 RTC latch: a 0x00 write followed by a 0x01 write copies the live
			// RTC_* registers into the RTCL_* latched copies that games actually read.
			if (( CARTTYPE >= 0x0F ) && ( CARTTYPE <= 0x10 )) {
				if ((RTCLATCH == 0x00) && (b == 0x01)) {
					RTCL_S = RTC_S; RTCL_M = RTC_M; RTCL_H = RTC_H;
					RTCL_DL = RTC_DL; RTCL_DH = RTC_DH;
				}
				RTCLATCH = b;
			}
		}
	} else if ( ( loc >= 0x8000 ) &&  ( loc <= 0x9FFF ) ) { // $8000-$9FFF VRAM
		VRAM[GetVRAMBank() * 0x2000 + (loc - 0x8000)] = b;
	} else if ( ( loc >= 0xA000 ) &&  ( loc <= 0xBFFF ) ) { // $A000-$BFFF - External (cartridge) RAM / MBC3 RTC
		if (( CARTTYPE >= 0x0F ) && ( CARTTYPE <= 0x13 ) && ( RTCSELECT >= 0x08 )) {
			// BUG FIX: writes to the RTC registers never marked the save
			// state dirty, unlike plain cart RAM writes just below - a
			// game that only ever touches the RTC (setting the time once
			// at first boot, say) without ever separately writing cart
			// RAM would never trigger a save at all, even though there
			// is now real RTC state worth persisting (see
			// SaveCartRAMAndRTC/CartHasRTC above).
			RAM_DIRTY = 1;
			switch (RTCSELECT) {
				case 0x08: RTC_S  = b; break;
				case 0x09: RTC_M  = b; break;
				case 0x0A: RTC_H  = b; break;
				case 0x0B: RTC_DL = b; break;
				case 0x0C: RTC_DH = b; break;
			}
		} else if (RAMENABLED) {
			RAM_DIRTY = 1;
			if (( CARTTYPE == 0x01 ) || ( CARTTYPE == 0x02) || ( CARTTYPE == 0x03 )) {
				if (MBCMODE) { EXTRNRAM[loc - 0xA000 + (WORD)((RAMBANKNUMBER % GetCartRAMBankCount()) * 0x2000)] = b; }
				else { EXTRNRAM[loc - 0xA000] = b; }
			} else if (( CARTTYPE == 0x05 ) || ( CARTTYPE == 0x06 )) {
				// BUG FIX: see the matching read-side fix above - same
				// missing mirroring/masking for MBC2's real 512-byte RAM.
				// Real MBC2 hardware also only has 4 data lines wired to
				// this RAM, so only the low nibble of any written byte is
				// actually meaningful - the high nibble reads back as
				// all 1s on real hardware. Masking writes to the low
				// nibble (rather than just masking on read) keeps
				// GetCartRAMSize()-based save files byte-for-byte
				// consistent with what a save/load round trip should
				// produce, rather than saving whatever's in the unused
				// high nibble.
				EXTRNRAM[(loc - 0xA000) % 512] = (b & 0x0F) | 0xF0;
			} else {
				EXTRNRAM[loc - 0xA000 + (WORD)((RAMBANKNUMBER % GetCartRAMBankCount()) * 0x2000)] = b;
			}
		}
	} else if ( ( loc >= 0xC000 ) &&  ( loc <= 0xDFFF ) ) { // $C000-$DFFF - Internal RAM
		*WRAMPtr(loc) = b;
	} else if ( ( loc >= 0xE000 ) &&  ( loc <= 0xFDFF ) ) { // $E000-$FDFF - Reserved Area/Echo RAM
		*WRAMPtr(loc) = b;
	} else if ( ( loc >= 0xFE00 ) &&  ( loc <= 0xFE9F ) ) { // $FE00-$FE9F - Object Attribute Memory (OAM)
		OAMRAM[loc - 0xFE00] = b;
	} else if ( ( loc >= 0xFF00 ) &&  ( loc <= 0xFF7F ) ) { // $FF00-$FF7F - Hardware I/O Registers
		if (loc >= 0xFF30 && loc <= 0xFF3F) { // Wave RAM - writable regardless of APU power state
			WAVERAM[loc - 0xFF30] = b;
			return;
		}
		switch (loc) {
			case 0xFF00: if (b == 0x03) { P1 = 0xF1; // Register for reading joy pad info and determining system type.    (R/W)
					} else {
						 pad = PadRead(0);

						 if(b  == 0x10) {
							 P1 = 0x00;
							 if (pad & Pad1x) { P1 |= 0x01; }
							 if (pad & Pad1crc) { P1 |= 0x02; }
							 if (pad & Pad1Select) { P1 |= 0x04; }
							 if (pad & Pad1Start) { P1 |= 0x08; }
							 if (pad & Pad1R1) { P1 |= 0x03; } // Press both a + b
							 P1 = ~P1;
							 P1 &= 0x0F;
						 	 break;
						 }
						 if(b == 0x20) {
							 P1 = 0x00;
							 if (pad & Pad1Right) { P1 |= 0x01; }
							 if (pad & Pad1Left) { P1 |= 0x02; }
							 if (pad & Pad1Up) { P1 |= 0x04; }
							 if (pad & Pad1Down) { P1 |= 0x08; }
							 P1 = ~P1;
							 P1 &= 0x0F;
							 break;
						 }
					}
					break;
			case 0xFF01: SERIALDATA = b; break; // Serial transfer data (R/W)
			case 0xFF02: SERIALCONTROL = b; onSerialControlWrite(); break; // SIO control (R/W)
			case 0xFF04:
				// BUG FIX: any write resets the real, unified 16-bit
				// counter to 0 (not just the visible DIVREG byte) - and
				// since resetting it can itself cause the watched timer
				// bit to fall from 1 to 0, that's a real falling edge
				// too, checked immediately rather than only on the next
				// natural tick (this is exactly what div_write.gb tests:
				// repeatedly resetting DIV while the timer runs can
				// trigger real, if easy to miss, spurious TIMA
				// increments this way).
				internalDivCounter16 = 0;
				DIVREG = 0;
				DIVCOUNTER = 0;
				CheckTimerEdge();
				break;
			case 0xFF05:
				// BUG FIX: a write to TIMA while a reload from the
				// previous overflow is still pending cancels that
				// reload - the written value sticks instead of TMA's
				// (Mooneye's tima_write_reloading.gb documents this
				// precisely). Ordinary writes (no reload pending) are
				// unaffected.
				TIMECNT = b;
				timaReloadPending = 0;
				break; // Timer counter (R/W)
			case 0xFF06: TIMEMOD = b; break; // Timer Modulo (R/W)
			case 0xFF07: TIMCONT = b;
							// BUG FIX: MAXTIME (the old, independent timer-period
							// model) is no longer used - the real, unified-counter
							// model (see CheckTimerEdge()) derives the watched bit
							// directly from TIMCONT itself. Checking for an edge
							// immediately with the new TIMCONT value (against the
							// counter's current, unchanged state) catches the real
							// hardware quirk where changing the enable bit (or,
							// less commonly tested, the clock-select bits) can
							// itself be a falling edge if the watched-bit-AND-
							// enabled value was 1 right before the write - exactly
							// what rapid_toggle.gb exercises by rapidly enabling/
							// disabling the timer.
							CheckTimerEdge();
							break; // Timer Control
			case 0xFF0F: IFLAG = b; break; // Interrupt Flag (R/W)

			// SOUND - $FF10-$FF25 writes are ignored while the master
			// APU switch (NR52 bit 7) is off, matching real hardware;
			// NR52 itself and Wave RAM remain writable regardless.
			case 0xFF10: if (NR52 & 0x80) { apuCh1.nrX0 = b; } break; // NR10 sweep
			case 0xFF11: if (NR52 & 0x80) { apuCh1.nrX1 = b; apuCh1.lengthCounter = 64 - (b & 0x3F); } break; // NR11
			case 0xFF12: // NR12 envelope - writing the DAC-off pattern immediately silences the channel
				if (NR52 & 0x80) {
					apuCh1.nrX2 = b;
					if (!APUDacEnabled12(b)) { apuCh1.enabled = 0; apuCh1.dacEnabled = 0; }
				}
				break;
			case 0xFF13: if (NR52 & 0x80) { apuCh1.nrX3 = b; } break; // NR13 freq lo
			case 0xFF14: // NR14 freq hi/trigger/length-enable
				if (NR52 & 0x80) {
					apuCh1.nrX4 = b;
					if (b & 0x80) { APUTriggerPulse(&apuCh1, 1); }
				}
				break;

			case 0xFF16: if (NR52 & 0x80) { apuCh2.nrX1 = b; apuCh2.lengthCounter = 64 - (b & 0x3F); } break; // NR21
			case 0xFF17:
				if (NR52 & 0x80) {
					apuCh2.nrX2 = b;
					if (!APUDacEnabled12(b)) { apuCh2.enabled = 0; apuCh2.dacEnabled = 0; }
				}
				break;
			case 0xFF18: if (NR52 & 0x80) { apuCh2.nrX3 = b; } break; // NR23
			case 0xFF19:
				if (NR52 & 0x80) {
					apuCh2.nrX4 = b;
					if (b & 0x80) { APUTriggerPulse(&apuCh2, 0); }
				}
				break;

			case 0xFF1A: // NR30 DAC on/off
				if (NR52 & 0x80) {
					apuCh3.nrX0 = b;
					apuCh3.dacEnabled = (b & 0x80) != 0;
					if (!apuCh3.dacEnabled) { apuCh3.enabled = 0; }
				}
				break;
			case 0xFF1B: if (NR52 & 0x80) { apuCh3.nrX1 = b; apuCh3.lengthCounter = 256 - b; } break; // NR31 (full 8-bit length)
			case 0xFF1C: if (NR52 & 0x80) { apuCh3.nrX2 = b; } break; // NR32 output level
			case 0xFF1D: if (NR52 & 0x80) { apuCh3.nrX3 = b; } break; // NR33
			case 0xFF1E:
				if (NR52 & 0x80) {
					apuCh3.nrX4 = b;
					if (b & 0x80) { APUTriggerWave(); }
				}
				break;

			case 0xFF20: if (NR52 & 0x80) { apuCh4.nrX1 = b; apuCh4.lengthCounter = 64 - (b & 0x3F); } break; // NR41
			case 0xFF21:
				if (NR52 & 0x80) {
					apuCh4.nrX2 = b;
					if (!APUDacEnabled12(b)) { apuCh4.enabled = 0; apuCh4.dacEnabled = 0; }
				}
				break;
			case 0xFF22: if (NR52 & 0x80) { apuCh4.nrX3 = b; } break; // NR43
			case 0xFF23:
				if (NR52 & 0x80) {
					apuCh4.nrX4 = b;
					if (b & 0x80) { APUTriggerNoise(); }
				}
				break;

			case 0xFF24: if (NR52 & 0x80) { NR50 = b; } break;
			case 0xFF25: if (NR52 & 0x80) { NR51 = b; } break;
			case 0xFF26: // NR52 - only the master power bit is actually writable
				NR52 = (NR52 & 0x0F) | (b & 0x80);
				if (!(b & 0x80)) {
					// Real hardware clears every sound register (but not
					// Wave RAM) when powered off this way, and channels
					// immediately stop.
					apuCh1 = (APUChannel){0};
					apuCh2 = (APUChannel){0};
					apuCh3 = (APUChannel){0};
					apuCh4 = (APUChannel){0};
					NR50 = 0; NR51 = 0;
				}
				break;

			// VIDEO
			case 0xFF40: {
				BYTE oldLCDC = LCDCONTROL;
				LCDCONTROL = b;
				// BUG FIX: turning the LCD off/on (bit 7) needs its own
				// explicit transition - see the freeze/resume comment in
				// cycleLength() for why the state machine alone isn't
				// enough. Powering off resets LY and the STAT mode bits to
				// 0 immediately but deliberately leaves the STAT
				// LYC-coincidence bit (bit 2) untouched - real hardware
				// stops recomputing it the instant the comparison clock
				// stops, so it just keeps whatever value it last had.
				// Powering back on resets LY to 0 and mode to 0 (the real
				// first-line state, before OAM search actually begins a
				// little later) and immediately recomputes the
				// coincidence bit against the fresh LY=0. Confirmed via
				// Mooneye's stat_lyc_onoff.gb.
				if ((oldLCDC & 0x80) && !(b & 0x80)) {
					LCDY = 0;
					videoMode = HBLANKMODE;
					LCDSTATUS = (LCDSTATUS & 0xFC) | (videoMode & 0x03); // mode bits only - bit 2 (coincidence) frozen
				} else if (!(oldLCDC & 0x80) && (b & 0x80)) {
					int wasCoincident = (LCDSTATUS >> 2) & 0x01; // frozen value from while powered off
					LCDY = 0;
					videoMode = HBLANKMODE;
					VideoCyclesLeft = OAM_CYCLES;
					int nowCoincident = (LCDY == LYC);
					LCDSTATUS = (LCDSTATUS & 0xF8) | (videoMode & 0x03) | (nowCoincident ? 0x04 : 0x00);
					// Powering on restarts the comparison at LY=0 - if
					// that's a genuine 0->1 transition of the coincidence
					// bit (not just "still 1, same as it was frozen at"),
					// and the LYC-coincidence STAT interrupt source is
					// enabled (bit 6), that's a real coincidence event and
					// requests the STAT interrupt right here, same as any
					// other live LY==LYC transition.
					if (nowCoincident && !wasCoincident && ((LCDSTATUS >> 6) & 0x01)) { IFLAG |= 0x02; }
				}
				break; // LCD Control (R/W)
			}
			case 0xFF41:
				// BUG FIX: bits 0-2 (mode + LYC-coincidence) are read-only,
				// hardware-maintained status bits on real hardware - only
				// bits 3-6 (interrupt-source enables) are actually
				// writable. This previously let software overwrite the
				// mode/coincidence bits directly, which then never got
				// corrected by the PPU state machine (see cycleLength).
				LCDSTATUS = (LCDSTATUS & 0x07) | (b & 0xF8);
				break; // LCDC Status   (R/W)
			case 0xFF42: SCRY = b; break; // Scroll Y   (R/W)
			case 0xFF43: SCRX = b; break; // Scroll X   (R/W)
			case 0xFF44: LCDY = 0x00; break; // LCDC Y-Coordinate (R)
			case 0xFF45:
				// BUG FIX: the STAT LYC-coincidence bit (bit 2) was only
				// ever recomputed once per PPU mode transition (every
				// 80-456 cycles, inside cycleLength()) - real hardware's
				// comparator is effectively live, so a software write to
				// LYC while the LCD is on needs to update the coincidence
				// bit immediately, not wait for the next mode boundary.
				// Only while the LCD is actually on - the comparison
				// clock is frozen otherwise (see the LCDC/$FF40 handler).
				// Confirmed via Mooneye's stat_lyc_onoff.gb, which sets
				// LYC immediately before turning the LCD off and expects
				// the freshly-recomputed bit to already be in effect.
				LYC = b;
				if (LCDCONTROL & 0x80) {
					LCDSTATUS = (LCDSTATUS & 0xFB) | ((LCDY == LYC) ? 0x04 : 0x00);
				}
				break; // LY Compare  (R/W)
			case 0xFF46: doDMA(b); break; // DMA Transfer and Start Address (W)
			case 0xFF47: BGPAL = b; break; // BG Palette Data  (W)
			case 0xFF48: OBJPAL0 = b; break; // Object Palette 0 Data (W)
			case 0xFF49: OBJPAL1 = b; break; // Object Palette 1 Data (W)
			case 0xFF4A: WNDY = b; break; // Window Y Position  (R/W)
			case 0xFF4B: WNDX = b; break; // Window X Position  (R/W)
			// GBC registers - all gated on GBC_MODE, so a DMG cart
			// writing to one of these addresses (which real DMG
			// hardware would just ignore, since they're CGB-only) has
			// no effect at all, same as real hardware.
			case 0xFF4D: if (GBC_MODE) { KEY1 = (KEY1 & 0x80) | (b & 0x01); } break; // KEY1 - speed switch prepare bit (R/W)
			case 0xFF4F: if (GBC_MODE) { VBK = b & 0x01; } break; // VBK - VRAM bank (R/W)
			case 0xFF51: if (GBC_MODE) { HDMA1 = b; } break; // HDMA1 - transfer source high (W)
			case 0xFF52: if (GBC_MODE) { HDMA2 = b & 0xF0; } break; // HDMA2 - transfer source low (W)
			case 0xFF53: if (GBC_MODE) { HDMA3 = b & 0x1F; } break; // HDMA3 - transfer dest high (W)
			case 0xFF54: if (GBC_MODE) { HDMA4 = b & 0xF0; } break; // HDMA4 - transfer dest low (W)
			case 0xFF55: // HDMA5 - VRAM DMA start (R/W)
				if (GBC_MODE) {
					int length = ((b & 0x7F) + 1) * 16;
					int src = (HDMA1 << 8) | HDMA2;
					int dst = 0x8000 | (((HDMA3 << 8) | HDMA4) & 0x1FFF);
					if (b & 0x80) {
						// H-Blank DMA: 16 bytes transfer per H-Blank: see
						// the continuation in hblank() below. Real
						// hardware also lets writing HDMA5 with bit 7
						// clear, while an H-Blank DMA is already active,
						// cancel it early instead of starting a new
						// General-Purpose transfer - not implemented
						// here since no ROM exercising that specific
						// case has come up yet.
						HDMA_REMAINING = length;
						HDMA_SRC = src;
						HDMA_DST = dst;
					} else {
						// General-Purpose DMA: real hardware transfers
						// the whole block immediately (blocking the CPU
						// for a proportional time), unlike H-Blank DMA -
						// so, unlike that mode, this genuinely can just
						// happen all at once here too.
						int k;
						for (k = 0; k < length; k++) {
							WriteMEM((WORD)(dst + k), ReadMEM((WORD)(src + k)));
						}
						HDMA_REMAINING = -1;
					}
				}
				break;
			case 0xFF68: if (GBC_MODE) { BCPS = b & 0xBF; } break; // BCPS/BGPI - BG palette index (R/W)
			case 0xFF69: // BCPD/BGPD - BG palette data (R/W)
				if (GBC_MODE) {
					BGPALRAM[BCPS & 0x3F] = b;
					if (BCPS & 0x80) {
						BCPS = (BCPS & 0x80) | ((BCPS + 1) & 0x3F);
					}
				}
				break;
			case 0xFF6A: if (GBC_MODE) { OCPS = b & 0xBF; } break; // OCPS/OBPI - OBJ palette index (R/W)
			case 0xFF6B: // OCPD/OBPD - OBJ palette data (R/W)
				if (GBC_MODE) {
					OBJPALRAM[OCPS & 0x3F] = b;
					if (OCPS & 0x80) {
						OCPS = (OCPS & 0x80) | ((OCPS + 1) & 0x3F);
					}
				}
				break;
			case 0xFF70: if (GBC_MODE) { SVBK = b & 0x07; } break; // SVBK - WRAM bank (R/W)
			default: break;
		}

	} else if ( ( loc >= 0xFF80 ) &&  ( loc <= 0xFFFE ) ) { // $FF80-$FFFE - High RAM Area
		HIRAM[loc - 0xFF80] = b;

	} else if  ( loc == 0xFFFF ) { // $FFFF - Interrupt Enable Register
          IER = (int)b;
	}
}

WORD ReadWord(WORD reg){
	WORD aW = ReadMEM(reg) | (ReadMEM(reg + 1) << 8);
	return aW & 0xFFFF;
}

void DrawBG(){
	//////////////here
	//printf("DrawBG\n");
	Draw_Buffer(screenBuffer, screenBufferColor, GBC_MODE);
}
void DrawWindow(){

}

void ShowTiles(int tileNo) {
	//setWH(&pix, 2, 2);
	BYTE B1, B2;
	for (t = 0; t < 16; t+=2) {
		B1 = ROM[(WORD)((tileNo * 16) + t + Voff)];
		B2 = ROM[(WORD)((tileNo * 16) + t + 1 + Voff)];

		for (n = 0; n < 8; n++) {
			//SetTile(&pix);
			//pix.x0 = (((0) * 8) + n) * pix.w;
			//pix.y0 = ((int)(t/2)) * pix.h ;
			//pix.r0 = pix.g0 = pix.b0 = (u_char)((int)((B1 >> n) & (0x00000001)) + (int)((B2 >> n) & (0x00000001))) * 100;
			//DrawPrim(&pix);
		}
	}
}

void DrawTile(int tileNo, int Xpos, int Ypos){

	//setWH(&pix, 2, 2);
	BYTE B1, B2;
	for (t = 0; t < 16; t+=2) {
		B1 = ROM[(WORD)((tileNo * 16 * 2) + t + 0x1800)];
		B2 = ROM[(WORD)((tileNo * 16 * 2) + t + 1 + 0x1800)];

		for (n = 0; n < 8; n++) {
			//SetTile(&pix);
			//pix.x0 = (Xpos + n) * pix.w;
			//pix.y0 = (Ypos + t/2) * pix.h ;
			////pix.r0 = pix.g0 = pix.b0 = (u_char)((int)((B1 >> n) & (0x00000001)) + (int)((B2 >> n) & (0x00000001))) * 100;
			//DrawPrim(&pix);
		}
	}
}


/*
Not Mapped yet:
--------------

27    DAA
76    HALT

*/

void OP00(void){  // 00	NOP
	cycleLength(4);
}

void OP01(void){  // 01	LD	BC,nnnn
	put_rBC(ReadWord(reg_PC));
	reg_PC += 2;
	cycleLength(12); // BUG FIX: was 20, real hardware is 12 (3 M-cycles)
}

void OP02(void){
	WriteMEM(get_rBC(), reg_A);
	cycleLength(8); // BUG FIX: was 7 (not even a multiple of 4 - impossible on real hardware), correct is 8
} // 02	LD	(BC),A

void OP03(void){ //  case 0x03:
	put_rBC(INCWreg(get_rBC()));
	cycleLength(8); // BUG FIX: was 6 (not a multiple of 4), correct is 8
} // 03	INC	BC

void OP04(void){ // case 0x04:
	reg_B = INCreg(reg_B);
	cycleLength(4);
} // 04	INC	B

void OP05(void){ //		case 0x05:
	reg_B = DECreg(reg_B);
	cycleLength(4);
} // 05	DEC B

void OP06(void){ //		case 0x06:
	reg_B = ReadMEM(reg_PC++);
	cycleLength(8);
} // 06	LD	B,nn

void OP07(void){ // case 0x07:
	// BUG FIX: RLCA/RLA/RRCA/RRA (the non-CB accumulator rotates) always
	// clear Z on real hardware regardless of the result - RLA/RRA already
	// did this via their own wrappers below, but RLCA/RRCA were calling the
	// CB-style RLC/RRC directly, which set Z from the result like the CB
	// versions correctly do for a general register, but not for A here.
	reg_A = RLC(reg_A);
	setZ(0);
	cycleLength(4);
} // 07    RLCA

void OP08(void){ //		case 0x08:
	WORD tmp;
	tmp = ReadWord(reg_PC);
	WriteMEM(tmp, reg_SP & 0xFF);
	WriteMEM(tmp+1, reg_SP >> 8);
	reg_PC += 2;

	//BYTE tmp1, tmp2;
	//tmp1 = ReadMEM(reg_PC++);
    //tmp2 = ReadMEM(reg_PC++);
    //WriteMEM(((tmp2 << 8)| tmp1), reg_SP & 0xFF); // 08    LD   (nnnn),SP
    //WriteMEM((((tmp2 << 8) | tmp1)+1), reg_SP >> 8);
	cycleLength(20);
}

void OP09(void){ //		case 0x09:
	reg_HL = ADDWreg(reg_HL, get_rBC());
	cycleLength(8); // BUG FIX: was 12, real hardware is 8
} // 09    ADD  HL,BC

void OP0A(void){ //		case 0x0A:
	reg_A = ReadMEM(get_rBC());
cycleLength(8); } // 0A    LD   A,(BC)

void OP0B(void){ //		case 0x0B:
	put_rBC(DECWreg(get_rBC()));
cycleLength(8); } // 0B    DEC  BC

void OP0C(void){ //		case 0x0C:
	reg_C = INCreg(reg_C);
	cycleLength(4);
} // 0C    INC  C

void OP0D(void){ //		case 0x0D:
	reg_C = DECreg(reg_C);
	cycleLength(4);
} // 0D    DEC  C

void OP0E(void){ //		case 0x0E:
	reg_C = ReadMEM(reg_PC++);
	cycleLength(8);
}  // 0E    LD   C,nn

void OP0F(void){ //		case 0x0F:
	// BUG FIX: see OP07 above - RRCA must also force Z=0.
	reg_A = RRC(reg_A);
	setZ(0);
cycleLength(4); } // 0F    RRCA

void OP10(void){ //  0x10:
	ReadMEM(reg_PC++);
	// GBC double-speed switch: real hardware only actually switches
	// speed when STOP executes with KEY1 bit 0 ("prepare speed switch")
	// set - the game arms it by writing 1 to KEY1 before executing
	// STOP. Toggles KEY1 bit 7 (current speed: 0=normal, 1=double,
	// which cycleLength() checks directly) and clears the prepare bit.
	// If bit 0 isn't set, STOP is meant to behave as a deep low-power
	// halt until a button press instead - not implemented as an actual
	// CPU freeze here, matching this project's existing behavior of
	// just consuming the instruction either way.
	if (GBC_MODE && (KEY1 & 0x01)) {
		KEY1 = (KEY1 & 0x80) ^ 0x80; // toggle bit 7, clear bit 0
	}
cycleLength(4); } // 10 00 STOP      ???

void OP11(void){ //  0x11:
	put_rDE(ReadWord(reg_PC));
	reg_PC += 2;
	cycleLength(12);
} // 11    LD   DE,nnnn

void OP12(void){ //  0x12:
	WriteMEM(get_rDE(), reg_A);
	cycleLength(8);
} // 12    LD   (DE),A

void OP13(void){ //  0x13:
	put_rDE(INCWreg(get_rDE()));
	cycleLength(8);
} // 13    INC  DE

void OP14(void){ // case  0x14:
	reg_D = INCreg(reg_D);
 cycleLength(4); } // 14    INC  D

void OP15(void){ // case  0x15:
reg_D = DECreg(reg_D);
cycleLength(4); } // 15    DEC  D

void OP16(void){ // case  0x16:
		reg_D = ReadMEM(reg_PC++); cycleLength(8); } // 16    LD   D,nn

void OP17(void){ // case  0x17:
reg_A = RLA(reg_A);cycleLength(4); } // 17    RLA

void OP18(void){ // case  0x18:
	reg_PC = jr(reg_PC, ReadMEM(reg_PC++));
	cycleLength(12);
} // 18    JR   disp

void OP19(void){ // case  0x19:
		reg_HL = ADDWreg(reg_HL, get_rDE());
		cycleLength(8); // BUG FIX: was 16, real hardware is 8
} //19    ADD  HL,DE

void OP1A(void){ // case  0x1A:
	reg_A = ReadMEM(get_rDE());
	cycleLength(8);
} // 1A    LD   A,(DE)

void OP1B(void){ // case  0x1B:
	put_rDE(DECWreg(get_rDE()));
	cycleLength(8); // BUG FIX: was 4, real hardware is 8
} // 1B    DEC  DE

void OP1C(void){
	reg_E = INCreg(reg_E);
	cycleLength(4);
} // 1C    INC  E

void OP1D(void){ // case  0x1D:
	reg_E = DECreg(reg_E);
	cycleLength(4);
} // 1D    DEC  E

void OP1E(void){ // case  0x1E:
	reg_E = ReadMEM(reg_PC++);
	cycleLength(8);
} // 1E    LD   E,nn

void OP1F(void){ // case  0x1F:
	reg_A = RRA(reg_A);
	cycleLength(4);
}  // 1F    RRA

void OP20(void){ // case  0x20:
	if (!getZ()) {
		reg_PC = jr(reg_PC, ReadMEM(reg_PC++));
		cycleLength(12);
	} else {
		ReadMEM(reg_PC++);
		cycleLength(8);
	} // 20    JR   NZ,disp
}

void OP21(void){ // case  0x21:
	reg_HL = ReadWord(reg_PC);
	reg_PC += 2;
	cycleLength(12);
} // 21    LD   HL,nnnn
void OP22(void){ // case  0x22:
	WriteMEM(reg_HL, reg_A);
	reg_HL = INCWreg(reg_HL);
	cycleLength(8); // BUG FIX: was 16, real hardware is 8
} // 22    LDI  (HL),A

void OP23(void){ // case  0x23:
	reg_HL = INCWreg(reg_HL);
	cycleLength(8);
} // 23    INC  HL

void OP24(void){ // case  0x24:
	put_rH(INCreg(get_rH()));
	cycleLength(4);
} // 24    INC  H

void OP25(void){ // case  0x25:
	put_rH(DECreg(get_rH()));
	cycleLength(4);
} // 25    DEC  H

void OP26(void){ // case  0x26:
	put_rH(ReadMEM(reg_PC++));
	cycleLength(8);
} // 26    LD   H,nn

void OP27(void){ // DAA - decimal-adjust A after a BCD add/subtract.
	// This was a total no-op stub before (never adjusted A, never touched any
	// flag) - any game or test doing BCD math (score counters, some timers/
	// currency logic) using ADD/SUB followed by DAA would get a plain binary
	// result instead of the corrected BCD one.
	int a = reg_A;
	if (!getN()) {
		if (getC() || a > 0x99) { a += 0x60; setC(1); }
		if (getH() || (a & 0x0F) > 0x09) { a += 0x06; }
	} else {
		if (getC()) { a -= 0x60; }
		if (getH()) { a -= 0x06; }
	}
	reg_A = (BYTE)(a & 0xFF);
	setZ(reg_A == 0);
	setH(0);
	cycleLength(4);
} // 27    DAA

void OP28(void){ // case  0x28:
	if (getZ()) {
		reg_PC = jr(reg_PC, ReadMEM(reg_PC++));
		cycleLength(12);
	} else {
		ReadMEM(reg_PC++);
		cycleLength(8);
	}
}// 28    JR   Z,disp

void OP29(void){ // case  0x29:
	reg_HL = ADDWreg(reg_HL, reg_HL);
	cycleLength(8); // BUG FIX: was 12, real hardware is 8
} // 29    ADD  HL,HL

void OP2A(void){ // case  0x2A:
	reg_A = ReadMEM(reg_HL);
	reg_HL = INCWreg(reg_HL);
	cycleLength(8); // BUG FIX: was 16, real hardware is 8
} // 2A    LDI  A,(HL)

void OP2B(void){ // case  0x2B:
	reg_HL = DECWreg(reg_HL);
	cycleLength(8);
} // 2B    DEC  HL

void OP2C(void){ // case  0x2C:
	put_rL(INCreg(get_rL()));
	cycleLength(4);
} // 2C    INC  L

void OP2D(void){ // case  0x2D:
	put_rL(DECreg(get_rL()));
	cycleLength(4);
} // 2D    DEC  L

void OP2E(void){ // case  0x2E:
	put_rL(ReadMEM(reg_PC++));
	cycleLength(8);
} // 2E    LD   L,nn

void OP2F(void){ // case  0x2F:
	reg_A = CPLreg(reg_A);
	cycleLength(4);
} // 2F    CPL

void OP30(void){ // case  0x30:
	if (getC() != 1) {
		reg_PC = jr(reg_PC, ReadMEM(reg_PC++));
		cycleLength(12);
	} else { ReadMEM(reg_PC++);
		cycleLength(8);
	}
} // 30    JR   NC,disp

void OP31(void){ // case  0x31:
	reg_SP = ReadWord(reg_PC);
	reg_PC += 2;
	cycleLength(12);
} //31    LD   SP,nnnn

void OP32(void){ // case  0x32:
	WriteMEM(reg_HL, reg_A);
	reg_HL = DECWreg(reg_HL);
	cycleLength(8); // BUG FIX: was 16, real hardware is 8
} // 32    LDD  (HL),A

void OP33(void){ // case  0x33:
reg_SP = INCWreg(reg_SP); cycleLength(8); }  // 33    INC  SP

void OP34(void){ // case  0x34:
	WriteMEM(reg_HL, INCreg(ReadMEM(reg_HL)));
	cycleLength(12);
} // 34    INC  (HL)

void OP35(void){ // case  0x35:
	WriteMEM(reg_HL, DECreg(ReadMEM(reg_HL)));
	cycleLength(12);
} // 35    DEC  (HL)

void OP36(void){ // case  0x36:
	WriteMEM(reg_HL, ReadMEM(reg_PC++));
	cycleLength(12);
} // 36    LD   (HL),nn

void OP37(void){ // case  0x37:
	// BUG FIX: SCF must also clear N and H (Z is left alone) - this only
	// ever set C, leaking whatever N/H a prior instruction left behind.
	setC(1);
	setN(0);
	setH(0);
	cycleLength(4);
} // 37    SCF

void OP38(void){ // case  0x38:
	if (getC() != 0) {
		reg_PC = jr(reg_PC, ReadMEM(reg_PC++));
		cycleLength(12);
	} else {
		ReadMEM(reg_PC++);
		cycleLength(8);
	}
} // 38    JR   C,disp

void OP39(void){ // case  0x39:
	reg_HL = ADDWreg(reg_HL, reg_SP);
	cycleLength(8); // BUG FIX: was 12, real hardware is 8
} // 39    ADD  HL,SP

void OP3A(void){ // case  0x3A:
	reg_A = ReadMEM(reg_HL);
	reg_HL = DECWreg(reg_HL);
	cycleLength(8); // BUG FIX: was 16, real hardware is 8
} // 3A    LDD  A,(HL)

void OP3B(void){ // case  0x3B:
reg_SP = DECWreg(reg_SP); cycleLength(8); }	// 3B    DEC  SP
void OP3C(void){ // case  0x3C:
reg_A = INCreg(reg_A); cycleLength(4); } // 3C    INC  A

void OP3D(void){ // case  0x3D:
	reg_A = DECreg(reg_A);
	cycleLength(4);
} // 3D    DEC  A

void OP3E(void){ // case  0x3E:
	reg_A = ReadMEM(reg_PC++);
	cycleLength(8);
} // 3E    LD   A,nn

void OP3F(void){ // case  0x3F:
// BUG FIX: CCF must also clear N and H (Z is left alone), same leak as SCF.
setC(!getC()); setN(0); setH(0); cycleLength(4); }// 3F    CCF
void OP40(void){ // case  0x40:
cycleLength(4); } // 40    LD   B,B
void OP41(void){ // case  0x41:
reg_B = reg_C; cycleLength(4); } // 41    LD   B,C
void OP42(void){ // case  0x42:
reg_B = reg_D; cycleLength(4); } // 42    LD   B,D
void OP43(void){ // case  0x43:
reg_B = reg_E; cycleLength(4); } // 43    LD   B,E
void OP44(void){ // case  0x44:
reg_B = get_rH(); cycleLength(4);} // 44    LD   B,H
void OP45(void){ // case  0x45:
reg_B = get_rL(); cycleLength(4);}// 45    LD   B,L
void OP46(void){ // case  0x46:
reg_B = ReadMEM(reg_HL); cycleLength(8); } // 46    LD   B,(HL)
void OP47(void){ // case  0x47:
reg_B = reg_A; cycleLength(4); } // 47    LD   B,A
void OP48(void){ // case  0x48:
reg_C = reg_B; cycleLength(4); } // 48    LD   C,B
void OP49(void){ // case  0x49:
cycleLength(4); } // 49    LD   C,C

void OP4A(void){ // case  0x4A:
	reg_C = reg_D;
	cycleLength(4);
} // 4A    LD   C,D

void OP4B(void){ // case  0x4B:
reg_C = reg_E; cycleLength(4); } // 4B    LD   C,E
void OP4C(void){ // case  0x4C:
reg_C = get_rH(); cycleLength(4); } // 4C    LD   C,H
void OP4D(void){ // case  0x4D:
reg_C = get_rL(); cycleLength(4); } // 4D    LD   C,L
void OP4E(void){ // case  0x4E:
reg_C = ReadMEM(reg_HL); cycleLength(8); } // 4E    LD   C,(HL)
void OP4F(void){ // case  0x4F:
reg_C = reg_A; cycleLength(4); } // 4F    LD   C,A
void OP50(void){ // case  0x50:
reg_D = reg_B; cycleLength(4); } // 50    LD   D,B
void OP51(void){ // case  0x51:
reg_D = reg_C; cycleLength(4); } // 51    LD   D,C
void OP52(void){ // case  0x52:
cycleLength(4); } // 52    LD   D,D
void OP53(void){ // case  0x53:
reg_D = reg_E; cycleLength(4); } // 53    LD   D,E
void OP54(void){ // case  0x54:
reg_D = get_rH(); cycleLength(4); } // 54    LD   D,H
void OP55(void){ // case  0x55:
reg_D = get_rL(); cycleLength(4); } // 55    LD   D,L
void OP56(void){ // case  0x56:
reg_D = ReadMEM(reg_HL); cycleLength(8); } // 56    LD   D,(HL)

void OP57(void){ // case  0x57:
	reg_D = reg_A;
	cycleLength(4);
} // 57    LD   D,A

void OP58(void){ // case  0x58:
reg_E = reg_B; cycleLength(4); } // 58    LD   E,B
void OP59(void){ // case  0x59:
reg_E = reg_C; cycleLength(4); } // 59    LD   E,C
void OP5A(void){ // case  0x5A:
reg_E = reg_D; cycleLength(4); } // 5A    LD   E,D
void OP5B(void){ // case  0x5B:
cycleLength(4); } // 5B    LD   E,E
void OP5C(void){ // case  0x5C:
reg_E = get_rH(); cycleLength(4); } // 5C    LD   E,H
void OP5D(void){ // case  0x5D:
reg_E = get_rL(); cycleLength(4); } // 5D    LD   E,L
void OP5E(void){ // case  0x5E:
reg_E = ReadMEM(reg_HL); cycleLength(8); } // 5E    LD   E,(HL)

void OP5F(void){ // case  0x5F:
	reg_E = reg_A;
	cycleLength(4);
} // 5F    LD   E,A

void OP60(void){ // case  0x60:
put_rH(reg_B); cycleLength(4); } // 60    LD   H,B
void OP61(void){ // case  0x61:
put_rH(reg_C); cycleLength(4); } // 61    LD   H,C
void OP62(void){ // case  0x62:
put_rH(reg_D); cycleLength(4); } // 62    LD   H,D
void OP63(void){ // case  0x63:
put_rH(reg_E); cycleLength(4); } // 63    LD   H,E
void OP64(void){ // case  0x64:
put_rH(get_rH()); cycleLength(4); } // 64    LD   H,H
void OP65(void){ // case  0x65:
put_rH(get_rL()); cycleLength(4); } // 65    LD   H,L

void OP66(void){ // case  0x66:
	put_rH((BYTE)ReadMEM(reg_HL));
	cycleLength(8);
} // 66    LD   H,(HL)

void OP67(void){ // case  0x67:
put_rH(reg_A); cycleLength(4); } // 67    LD   H,A
void OP68(void){ // case  0x68:
put_rL(reg_B); cycleLength(4); } // 68    LD   L,B
void OP69(void){ // case  0x69:
put_rL(reg_C); cycleLength(4); } // 69    LD   L,C
void OP6A(void){ // case  0x6A:
put_rL(reg_D); cycleLength(4); } // 6A    LD   L,D

void OP6B(void){ // case  0x6B:
	put_rL(reg_E);
	cycleLength(4);
} // 6B    LD   L,E

void OP6C(void){ // case  0x6C:
put_rL(get_rH()); cycleLength(4); } // 6C    LD   L,H
void OP6D(void){ // case  0x6D:
put_rL(get_rL()); cycleLength(4); } // 6D    LD   L,L
void OP6E(void){ // case  0x6E:
put_rL(ReadMEM(reg_HL)); cycleLength(8); } // 6E    LD   L,(HL)

void OP6F(void){ // case  0x6F:
	put_rL(reg_A);
	cycleLength(4);
} // 6F    LD   L,A

void OP70(void){ // case  0x70:
WriteMEM(reg_HL, reg_B); cycleLength(8); } // 70    LD   (HL),B
void OP71(void){ // case  0x71:
WriteMEM(reg_HL, reg_C); cycleLength(8); } // 71    LD   (HL),C
void OP72(void){ // case  0x72:
WriteMEM(reg_HL, reg_D); cycleLength(8); } // 72    LD   (HL),D
void OP73(void){ // case  0x73:
WriteMEM(reg_HL, reg_E); cycleLength(8); } // 73    LD   (HL),E
void OP74(void){ // case  0x74:
WriteMEM(reg_HL, get_rH()); cycleLength(8); } // 74    LD   (HL),H
void OP75(void){ // case  0x75:
WriteMEM(reg_HL, get_rL()); cycleLength(8); } // 75    LD   (HL),L

void OP76(void){// 76 HALT
	// BUG FIX: this never actually halted anything. It inserted one delay of
	// CyclesLeft() cycles (an odd, essentially arbitrary amount - "however
	// many cycles until the next PPU/timer event", not "until an interrupt
	// is pending") and then just fell through to the next instruction
	// regardless of interrupt state. Real HALT freezes the CPU - no further
	// instructions fetched - until (IF & IE) becomes non-zero, then resumes
	// (servicing the interrupt if IME is set, or just falling through to the
	// next opcode if not). Since essentially every commercial game's main
	// loop is "HALT; <do per-frame work after VBlank wakes us up>", this
	// bug alone would have broken game timing/pacing broadly, independent
	// of the CPU-correctness bugs already fixed.
	//
	// Not yet modelled: the "HALT bug" quirk, where entering HALT with
	// IME=0 while an interrupt is already pending causes real hardware to
	// fail to increment PC afterwards (the following opcode byte gets
	// executed twice). A handful of commercial games and test ROMs rely on
	// this exact quirk; left as a known follow-up.
	// BUG FIX: HALT's own decode M-cycle was being charged *after* the
	// wait loop, unconditionally, on top of whatever the loop itself
	// already charged waiting for IF&IE to become true - every HALT that
	// actually waited (i.e. almost every one) cost 4 T-cycles more than
	// real hardware. Charge that one M-cycle up front instead (matching
	// every other opcode's own decode cost), then wait in 4-cycle steps
	// only for as long as the condition is still false, resuming exactly
	// on the boundary where it becomes true with no extra delay tacked
	// on. Confirmed via Mooneye's halt_ime1_timing2-GS.gb, which checks
	// HALT's total elapsed time against a NOP-based wait down to the
	// exact DIV value afterward.
	cycleLength(4);
	while (!(IFLAG & IER)) {
		cycleLength(4);
	}
}// 76 HALT

void OP77(void){ // case  0x77:
WriteMEM(reg_HL, reg_A); cycleLength(8); } // 77    LD   (HL),A
void OP78(void){ // case  0x78:
reg_A = reg_B; cycleLength(4); } //	78    LD   A,B
void OP79(void){ // case  0x79:
reg_A = reg_C; cycleLength(4); } //	79    LD   A,C
void OP7A(void){ // case  0x7A:
reg_A = reg_D; cycleLength(4); } //	7A    LD   A,D
void OP7B(void){ // case  0x7B:
reg_A = reg_E; cycleLength(4); } //	7B    LD   A,E

void OP7C(void){ // case  0x7C:
	reg_A = get_rH();
	cycleLength(4);
} //	7C    LD   A,H

void OP7D(void){ // case  0x7D:
	reg_A = get_rL();
	cycleLength(4);
} //	7D    LD   A,L

void OP7E(void){ // case  0x7E:
	reg_A = ReadMEM(reg_HL);
	cycleLength(8);
} // 7E    LD   A,(HL)

void OP7F(void){ // case  0x7F:
	cycleLength(4);
} //	7F    LD   A,A

void OP80(void){ // case  0x80:
	reg_A = ADDreg(reg_A, reg_B);
	cycleLength(4);
} // 80    ADD  A,B

void OP81(void){ // case  0x81:
	reg_A = ADDreg(reg_A, reg_C);
	cycleLength(4);
} // 81    ADD  A,C

void OP82(void){ // case  0x82:
	reg_A = ADDreg(reg_A, reg_D);
	cycleLength(4);
} // 82    ADD  A,D

void OP83(void){ // case  0x83:
	reg_A = ADDreg(reg_A, reg_E);
	cycleLength(4);
} // 83    ADD  A,E

void OP84(void){ // case  0x84:
	reg_A = ADDreg(reg_A, get_rH());
	cycleLength(4);
} // 84    ADD  A,H

void OP85(void){ // case  0x85:
	reg_A = ADDreg(reg_A, get_rL());
	cycleLength(4);
} // 85    ADD  A,L

void OP86(void){ // case  0x86:
	reg_A = ADDreg(reg_A, ReadMEM(reg_HL));
	cycleLength(8);
} // 86    ADD  A,(HL)

void OP87(void){ // case  0x87:
	reg_A = ADDreg(reg_A, reg_A);
	cycleLength(4);
} // 87    ADD  A,A

void OP88(void){ // case  0x88:
	reg_A = ADCreg(reg_A, reg_B);
	cycleLength(4);
} // 88    ADC  A,B

void OP89(void){ // case  0x89:
	reg_A = ADCreg(reg_A, reg_C);
	cycleLength(4);
} // 89    ADC  A,C

void OP8A(void){ // case  0x8A:
	reg_A = ADCreg(reg_A, reg_D);
	cycleLength(4);
} // 8A    ADC  A,D

void OP8B(void){ // case  0x8B:
	reg_A = ADCreg(reg_A, reg_E);
	cycleLength(4);
} // 8B    ADC  A,E

void OP8C(void){ // case  0x8C:
	reg_A = ADCreg(reg_A, get_rH());
	cycleLength(4);
} // 8C    ADC  A,H

void OP8D(void){ // case  0x8D:
	reg_A = ADCreg(reg_A, get_rL());
	cycleLength(4);
} // 8D    ADC  A,L

void OP8E(void){ // case  0x8E:
	reg_A = ADCreg(reg_A, ReadMEM(reg_HL));
	cycleLength(8);
} // 8E    ADC  A,(HL)

void OP8F(void){ // case  0x8F:
	reg_A = ADCreg(reg_A, reg_A);
	cycleLength(4);
} // 8F    ADC  A,A

void OP90(void){ // case  0x90:
	reg_A = SUBreg(reg_A, reg_B);
	cycleLength(4);
} // 90    SUB  B

void OP91(void){ // case  0x91:
	reg_A = SUBreg(reg_A, reg_C);
	cycleLength(4);
} // 91    SUB  C

void OP92(void){ // case  0x92:
	reg_A = SUBreg(reg_A, reg_D);
	cycleLength(4);
} // 92    SUB  D

void OP93(void){ // case  0x93:
	reg_A = SUBreg(reg_A, reg_E);
	cycleLength(4);
} // 93    SUB  E

void OP94(void){ // case  0x94:
	reg_A = SUBreg(reg_A, get_rH());
	cycleLength(4);
} // 94    SUB  H

void OP95(void){ // case  0x95:
	reg_A = SUBreg(reg_A, get_rL());
	cycleLength(4);
} // 95    SUB  L

void OP96(void){ // case  0x96:
	reg_A = SUBreg(reg_A, ReadMEM(reg_HL));
	cycleLength(8);
} // 96    SUB  (HL)

void OP97(void){ // case  0x97:
reg_A = SUBreg(reg_A, reg_A); cycleLength(4); } // 97    SUB  A
void OP98(void){ // case  0x98:
reg_A = SBCreg(reg_A, reg_B); cycleLength(4); } // 98    SBC  A,B
void OP99(void){ // case  0x99:
reg_A = SBCreg(reg_A, reg_C); cycleLength(4); } // 99    SBC  A,C
void OP9A(void){ // case  0x9A:
reg_A = SBCreg(reg_A, reg_D); cycleLength(4); } // 9A    SBC  A,D
void OP9B(void){ // case  0x9B:
reg_A = SBCreg(reg_A, reg_E); cycleLength(4); } // 9B    SBC  A,E
void OP9C(void){ // case  0x9C:
reg_A = SBCreg(reg_A, get_rH()); cycleLength(4); } // 9C    SBC  A,H
void OP9D(void){ // case  0x9D:
reg_A = SBCreg(reg_A, get_rL()); cycleLength(4); } // 9D    SBC  A,L
void OP9E(void){ // case  0x9E:
reg_A = SBCreg(reg_A, ReadMEM(reg_HL)); cycleLength(8); } // 9E    SBC  A,(HL)
void OP9F(void){ // case  0x9F:
reg_A = SBCreg(reg_A, reg_A); cycleLength(4); } // 9F    SBC  A,A
void OPA0(void){ // case  0xA0:
reg_A = ANDreg(reg_A, reg_B); cycleLength(4); } // A0    AND  B
void OPA1(void){ // case  0xA1:
reg_A = ANDreg(reg_A, reg_C); cycleLength(4); } // A1    AND  C
void OPA2(void){ // case  0xA2:
reg_A = ANDreg(reg_A, reg_D); cycleLength(4); } // A2    AND  D
void OPA3(void){ // case  0xA3:
reg_A = ANDreg(reg_A, reg_E); cycleLength(4); } // A3    AND  E
void OPA4(void){ // case  0xA4:
reg_A = ANDreg(reg_A, get_rH()); cycleLength(4); } // A4    AND  H
void OPA5(void){ // case  0xA5:
reg_A = ANDreg(reg_A, get_rL()); cycleLength(4); } // A5    AND  L
void OPA6(void){ // case  0xA6:
reg_A = ANDreg(reg_A, ReadMEM(reg_HL)); cycleLength(8); } // A6    AND  (HL)
void OPA7(void){ // case  0xA7:
reg_A = ANDreg(reg_A, reg_A); cycleLength(4); } // A7    AND  A
void OPA8(void){ // case  0xA8:
reg_A = XORreg(reg_A, reg_B); cycleLength(4); } // A8    XOR  B
void OPA9(void){ // case  0xA9:
reg_A = XORreg(reg_A, reg_C); cycleLength(4); } // A9    XOR  C
void OPAA(void){ // case  0xAA:
reg_A = XORreg(reg_A, reg_D); cycleLength(4); } // AA    XOR  D
void OPAB(void){ // case  0xAB:
reg_A = XORreg(reg_A, reg_E); cycleLength(4); } // AB    XOR  E
void OPAC(void){ // case  0xAC:
reg_A = XORreg(reg_A, get_rH()); cycleLength(4); } // AC    XOR  H
void OPAD(void){ // case  0xAD:
reg_A = XORreg(reg_A, get_rL()); cycleLength(4); } // AD    XOR  L
void OPAE(void){ // case  0xAE:
reg_A = XORreg(reg_A, ReadMEM(reg_HL)); cycleLength(8); } // AE    XOR  (HL)

void OPAF(void){ // case  0xAF:
	reg_A = XORreg(reg_A, reg_A);
	cycleLength(4);
} // AF    XOR  A

void OPB0(void){ // case  0xB0:
reg_A = ORreg(reg_A, reg_B);  cycleLength(4); } // B0    OR   B
void OPB1(void){ // case  0xB1:
reg_A = ORreg(reg_A, reg_C);  cycleLength(4); } // B1    OR   C
void OPB2(void){ // case  0xB2:
reg_A = ORreg(reg_A, reg_D);  cycleLength(4); } // B2    OR   D
void OPB3(void){ // case  0xB3:
reg_A = ORreg(reg_A, reg_E);  cycleLength(4); } // B3    OR   E
void OPB4(void){ // case  0xB4:
reg_A = ORreg(reg_A, get_rH()); cycleLength(4); } // B4    OR   H
void OPB5(void){ // case  0xB5:
reg_A = ORreg(reg_A, get_rL()); cycleLength(4); } // B5    OR   L
void OPB6(void){ // case  0xB6:
reg_A = ORreg(reg_A, ReadMEM(reg_HL)); cycleLength(8); } // B6    OR   (HL)
void OPB7(void){ // case  0xB7:
reg_A = ORreg(reg_A, reg_A); cycleLength(4); } // B7    OR   A
void OPB8(void){ // case  0xB8:
CPreg(reg_A,reg_B); cycleLength(4); } // B8    CP   B
void OPB9(void){ // case  0xB9:
CPreg(reg_A,reg_C); cycleLength(4); } // B9    CP   C
void OPBA(void){ // case  0xBA:
CPreg(reg_A,reg_D); cycleLength(4); } // BA    CP   D
void OPBB(void){ // case  0xBB:
CPreg(reg_A,reg_E); cycleLength(4); } // BB    CP   E
void OPBC(void){ // case  0xBC:
CPreg(reg_A,get_rH()); cycleLength(4); } // BC    CP   H
void OPBD(void){ // case  0xBD:
CPreg(reg_A,get_rL()); cycleLength(4); } // BD    CP   L
void OPBE(void){ // case  0xBE:
CPreg(reg_A,ReadMEM(reg_HL)); cycleLength(8); } // BE    CP   (HL)
void OPBF(void){ // case  0xBF:
CPreg(reg_A,reg_A); cycleLength(4); } // BF    CP   A
void OPC0(void){ // case  0xC0:
	if (!getZ()) {
		cycleLength(8); // BUG FIX (sub-instruction timing): fetch + condition check M-cycles
		reg_PC = ret();
		cycleLength(4); // final M-cycle (set PC)
	} else {
		cycleLength(8);
	}
} // C0    RET  NZ

void OPC1(void){ // case  0xC1:
	// BUG FIX (sub-instruction timing): fetch's own M-cycle (4T) is now
	// charged explicitly here, since pop() only accounts for its own 2
	// M-cycles (8T) - see the comment above push()/pop() for why.
	cycleLength(4);
	put_rBC(pop());
} // C1    POP  BC

void OPC2(void){ // case  0xC2:
	if (!getZ()) {
		reg_PC = jp(ReadWord(reg_PC));
		cycleLength(16);
	} else {
		reg_PC += 2;
		cycleLength(12);
	}
} // C2    JP   NZ,nnnn

void OPC3(void){ // case  0xC3:
	reg_PC = jp(ReadWord(reg_PC));
	cycleLength(16);
} // C3    JP   nnnn

void OPC4(void){ // case  0xC4:
	if (!getZ()) {
		// BUG FIX (sub-instruction timing): see OPCD (CALL nnnn) for the
		// full explanation - same double-charge fix applied here.
		cycleLength(4); // M1: fetch
		BYTE lo = ReadMEM(reg_PC++);
		cycleLength(4); // M2
		BYTE hi = ReadMEM(reg_PC++);
		cycleLength(4); // M3
		call();
		reg_PC = (WORD)(lo | (hi << 8));
	} else {
		reg_PC += 2;
		cycleLength(12);
	}
} // C4    CALL NZ,nnnn

void OPC5(void){ // case  0xC5:
cycleLength(4); push(get_rBC()); } // C5    PUSH BC
void OPC6(void){ // case  0xC6:
reg_A = ADDreg(reg_A, ReadMEM(reg_PC++)); cycleLength(8); } // C6    ADD  A,nn
void OPC7(void){ // case  0xC7:
cycleLength(4); reg_PC = rst(0x0000); } // C7    RST  00H

void OPC8(void){ // case  0xC8:
	if (getZ()) {
		cycleLength(8); // BUG FIX (sub-instruction timing): fetch + condition check M-cycles
		reg_PC = ret();
		cycleLength(4); // final M-cycle (set PC)
	} else {
		cycleLength(8);
	}
} // C8    RET  Z

void OPC9(void){ // case  0xC9:
	// BUG FIX (sub-instruction timing): pop() (via ret()) now charges its
	// own 8T progressively; this used to *also* charge the full 16T
	// afterward, double-counting (24T instead of the correct 16T). Real
	// RET is 4 M-cycles: fetch, read PC-lo, read PC-hi, then an internal
	// delay to actually set PC - pop() supplies the middle 2 (the reads).
	cycleLength(4); // M1: fetch
	reg_PC = ret();
	cycleLength(4); // M4: internal delay (set PC)
} // C9    RET

void OPCA(void){ // case  0xCA:
	if (getZ()) {
		reg_PC = jp(ReadWord(reg_PC));
		cycleLength(16);
	} else {
		//ReadWord(reg_PC);
		reg_PC += 2;
		cycleLength(12);
	}
}	// CA    JP   Z,nnnn


void OPCB(void){ // case 0xCB:
	currOp = ReadMEM(reg_PC++);
	CBinst[currOp]();
	cycleLength(4);
}

void OPCC(void){ // case  0xCC:

	if (getZ() != 0) {
		// BUG FIX (sub-instruction timing): see OPCD (CALL nnnn).
		cycleLength(4); // M1: fetch
		BYTE lo = ReadMEM(reg_PC++);
		cycleLength(4); // M2
		BYTE hi = ReadMEM(reg_PC++);
		cycleLength(4); // M3
		call();
		reg_PC = (WORD)(lo | (hi << 8));
	} else {
		//ReadWord(reg_PC);
		reg_PC += 2;
		cycleLength(12);
	}
} // CC    CALL Z,nnnn

void OPCD(void){ // case  0xCD:
	// BUG FIX (sub-instruction timing): was call(); reg_PC=ReadWord(...);
	// cycleLength(24) - call() pushes via push(), which now charges its
	// own 3 M-cycles (12T) progressively, so a further lump cycleLength(24)
	// here double-counted them (36T total instead of the correct 24T) -
	// same class of bug as the earlier interrupt()/RST double-charges.
	// Real CALL is 6 M-cycles: fetch, read addr-lo, read addr-hi, then
	// the same idle+write+write push() already supplies.
	cycleLength(4); // M1: fetch
	BYTE lo = ReadMEM(reg_PC++);
	cycleLength(4); // M2: read address low byte
	BYTE hi = ReadMEM(reg_PC++);
	cycleLength(4); // M3: read address high byte
	call();
	reg_PC = (WORD)(lo | (hi << 8));
} // CD    CALL nnnn

void OPCE(void){ // case  0xCE:
reg_A = ADCreg(reg_A, ReadMEM(reg_PC++)); cycleLength(8); } // CE    ADC  A,nn
void OPCF(void){ // case  0xCF:
cycleLength(4); reg_PC = rst(0x0008); } // CF    RST  8

void OPD0(void){ // case  0xD0:
	if (getC() != 1) {
		cycleLength(8); // BUG FIX (sub-instruction timing): fetch + condition check M-cycles
		reg_PC = ret();
		cycleLength(4); // final M-cycle (set PC)
	} else {
		cycleLength(8);
	}
} // D0    RET  NC

void OPD1(void){ // case  0xD1:
	cycleLength(4); // BUG FIX (sub-instruction timing): see OPC1 (POP BC)
	put_rDE(pop());
} // D1    POP  DE

void OPD2(void){ // case  0xD2:
	if (getC() != 1) {
		reg_PC = jp(ReadWord(reg_PC));
		//reg_PC += 2; // TODO: TEST
		cycleLength(16); // BUG FIX: was 20, real hardware is 16 when taken
	} else {
		reg_PC += 2;
		cycleLength(12); // BUG FIX: was 8, real hardware is 12 when not taken
	}
}// D2    JP   NC,nnnn

void OPD3(void){
	printf("Incomplete Opcode! D3\n");
	}

void OPD4(void){ // case  0xD4:
	if (getC() != 1) {
		// BUG FIX (sub-instruction timing): see OPCD (CALL nnnn).
		cycleLength(4); // M1: fetch
		BYTE lo = ReadMEM(reg_PC++);
		cycleLength(4); // M2
		BYTE hi = ReadMEM(reg_PC++);
		cycleLength(4); // M3
		call();
		reg_PC = (WORD)(lo | (hi << 8));
	} else {
		reg_PC += 2;
		cycleLength(12);
	}
} // D4    CALL NC,nnnn

void OPD5(void){ // case  0xD5:
cycleLength(4); push(get_rDE()); } // D5    PUSH DE
void OPD6(void){ // case  0xD6:
reg_A = SUBreg(reg_A, ReadMEM(reg_PC++)); cycleLength(8); } // D6    SUB  nn
void OPD7(void){ // case  0xD7:
cycleLength(4); reg_PC = rst(0x0010); } // D7    RST  10H
void OPD8(void){ // case  0xD8:
	if (getC() != 0) {
		cycleLength(8); // BUG FIX (sub-instruction timing): fetch + condition check M-cycles
		reg_PC = ret();
		cycleLength(4); // final M-cycle (set PC)
	} else {
		cycleLength(8);
	}
}// D8    RET  C
void OPD9(void){ // case  0xD9:
// BUG FIX (sub-instruction timing): same fix and reasoning as OPC9 (RET) -
// IME=1 still happens in the same relative position (right after ret()'s
// own reads, before the final delay cycle) as before, preserving the
// timing reti_intr_timing.gb already depends on.
cycleLength(4); reg_PC = ret(); IME = 1; cycleLength(4); } // D9    RETI
void OPDA(void){ // case  0xDA:
	if (getC() != 0) {
		reg_PC = jp(ReadWord(reg_PC));
		//reg_PC += 2; // TODO: TEST
		cycleLength(16);
	} else {
		reg_PC += 2;
		cycleLength(12);
	}
}// DA    JP   C,nnnn
void OPDB(void){
	printf("Incomplete Opcode! DB\n");
	}

void OPDC(void){ // case  0xDC:
	if (getC() != 0) {
		// BUG FIX (sub-instruction timing): see OPCD (CALL nnnn).
		cycleLength(4); // M1: fetch
		BYTE lo = ReadMEM(reg_PC++);
		cycleLength(4); // M2
		BYTE hi = ReadMEM(reg_PC++);
		cycleLength(4); // M3
		call();
		reg_PC = (WORD)(lo | (hi << 8));
		} else {
			reg_PC += 2;
			cycleLength(12);
		}
}// DC    CALL C,nnnn

void OPDD(void){
	printf("Incomplete Opcode! DD\n");
	}

void OPDE(void){ // case  0xDE:
reg_A = SBCreg(reg_A, ReadMEM(reg_PC++)); cycleLength(8); } // DE    SBC  A,nn
void OPDF(void){ // case  0xDF:
cycleLength(4); reg_PC = rst(0x0018); } // DF    RST  18H

void OPE0(void){ // case  0xE0:
	WriteMEM((0xFF00 + ReadMEM(reg_PC++)), reg_A);
	cycleLength(12);
} // E0    LD   ($FF00+nn),A

void OPE1(void){ // case  0xE1:
cycleLength(4); reg_HL = pop(); } // E1    POP  HL

void OPE2(void){ // case  0xE2:
	WriteMEM((0xFF00 + reg_C), reg_A);
	cycleLength(8);
} // E2    LD   ($FF00+C),A

void OPE3(void){
	printf("Incomplete Opcode! E3\n");
	}
void OPE4(void){
	printf("Incomplete Opcode! E4\n");
	}

void OPE5(void){ // case  0xE5:
	cycleLength(4);
	push(reg_HL);
} // E5    PUSH HL

void OPE6(void){ // case  0xE6:

reg_A = ANDreg(reg_A, ReadMEM(reg_PC++)); cycleLength(8); } // E6    AND  nn
void OPE7(void){ // case  0xE7:
cycleLength(4); reg_PC = rst(0x0020); } // E7    RST  20H

void OPE8(void){ // case  0xE8:
	// BUG FIX: only ever adjusted SP - never set any flag except a stray
	// N reset. Real hardware always clears Z and N, and computes H/C from
	// an *unsigned* 8-bit add of SP's low byte with the immediate (even
	// though the actual addition to SP is sign-extended) - order matters:
	// compute the flags from the pre-update SP before overwriting it.
	BYTE e = ReadMEM(reg_PC++);
	int result = reg_SP + (signed char)e;
	setH(((reg_SP & 0x0F) + (e & 0x0F)) > 0x0F);
	setC(((reg_SP & 0xFF) + (e & 0xFF)) > 0xFF);
	reg_SP = (WORD)(result & 0xFFFF);
	setZ(0);
	setN(0);
	cycleLength(16);
} // E8    ADD  SP,dd

void OPE9(void){ // case  0xE9:
reg_PC = reg_HL; cycleLength(4); } // E9    JP   (HL)

void OPEA(void){ // case  0xEA:
	WriteMEM(ReadWord(reg_PC), reg_A);
	reg_PC += 2;
	cycleLength(16);
} // EA    LD   (nnnn),A

void OPEB(void){printf("Incomplete Opcode! EB\n");}
void OPEC(void){printf("Incomplete Opcode! EC\n");}
void OPED(void){printf("Incomplete Opcode! ED\n");}

void OPEE(void){ // case  0xEE:
// BUG FIX: was calling XORreg with only one argument (missing reg_A) - only
// ever "worked" because implicit function declarations let the compiler
// silently accept any argument count, feeding XORreg whatever happened to
// be in the register the ABI would have used for a second argument. XOR n
// (immediate) has been undefined ever since.
reg_A = XORreg(reg_A, ReadMEM(reg_PC++)); cycleLength(8); } // EE    XOR  nn
void OPEF(void){ // case  0xEF:
cycleLength(4);
reg_PC = rst(0x0028);
} // EF    RST  28H

void OPF0(void){ // case  0xF0:
	reg_A = ReadMEM( 0xFF00 | ReadMEM(reg_PC++));
	cycleLength(12); // BUG FIX: was 8, real hardware is 12
} // F0    LD   A,($FF00+nn)

void OPF1(void){ // case  0xF1:
	cycleLength(4); // BUG FIX (sub-instruction timing): see OPC1 (POP BC)
	put_rAF(pop());
} // F1    POP  AF

void OPF2(void){ // case  0xF2:
	reg_A = ReadMEM(0xFF00+reg_C);
	cycleLength(8);
}// F2    LD   A,(C)

void OPF3(void){ // case  0xF3:
	// BUG FIX: DI must also cancel a still-pending EI delay, not just
	// clear IME - otherwise a DI executed one instruction after EI (before
	// EI_PENDING's countdown reaches 0) doesn't actually prevent EI's
	// effect from landing a moment later. Real hardware's DI is
	// immediate and unconditional (Mooneye's rapid_di_ei.gb tests
	// exactly this: "ei; di; ei; di" and "ei; di; nop; nop" must both
	// produce zero interrupts).
	IME = 0;
	EI_PENDING = 0;
	cycleLength(4);
} // F3    DI

void OPF4(void){
	printf("Incomplete Opcode! F4\n");
}// 0xF4

void OPF5(void){ // case  0xF5:
	cycleLength(4);
	push(get_rAF());
} // F5    PUSH AF

void OPF6(void){ // case  0xF6:
	reg_A = ORreg(reg_A, ReadMEM(reg_PC++));
	cycleLength(8);
} // F6    OR   nn

void OPF7(void){ // case  0xF7:
	cycleLength(4);
	reg_PC = rst(0x0030);
} // F7    RST  30H

void OPF8(void){ // case  0xF8:
	// BUG FIX: previously set no flags at all. Same H/C computation as
	// ADD SP,e8 above (this is really the same ALU operation, just written
	// to HL instead of back to SP) - Z and N always clear.
	BYTE e = ReadMEM(reg_PC++);
	int result = reg_SP + (signed char)e;
	setH(((reg_SP & 0x0F) + (e & 0x0F)) > 0x0F);
	setC(((reg_SP & 0xFF) + (e & 0xFF)) > 0xFF);
	reg_HL = (WORD)(result & 0xFFFF);
	setZ(0);
	setN(0);
	cycleLength(12);
} // F8    LD   HL,SP+dd

void OPF9(void){ // case  0xF9:
	reg_SP = reg_HL;
	cycleLength(8);
} //  F9    LD   SP,HL

void OPFA(void){ // case  0xFA:
	reg_A = ReadMEM(ReadWord(reg_PC));
	reg_PC += 2;
	cycleLength(16);
} // FA    LD   A,(nnnn)

void OPFB(void){ // case  0xFB:
	// BUG FIX: don't restart the delay if one's already armed (EI_PENDING
	// == 1, meaning IME becomes 1 at the end of *this* instruction) - a
	// second EI executing as the very instruction after a first EI must
	// not push IME's activation out by another instruction (Mooneye's
	// ei_sequence.gb tests exactly this: 18 consecutive EIs with IE/IF
	// already set should still fire the interrupt right after the
	// second EI, not after the last one).
	if (EI_PENDING == 0) EI_PENDING = 2;
	cycleLength(4);
} // FB    EI

void OPFC(void){
	#if defined(DEBUG)
	printf("Incomplete Opcode! FC\n");
	#endif
}

void OPFD(void){ // case  0xFD:
	EI_PENDING = 2;
	cycleLength(4);
} // FD    EI?

void OPFE(void){ // case  0xFE:
	CPreg(reg_A, (BYTE)ReadMEM(reg_PC++));
	cycleLength(8);
} // FE    CP   nn

void OPFF(void){ // case  0xFF:
	cycleLength(4);
	reg_PC = rst(0x0038);
} // FF    RST  38H


// Case CB

void CB00(void) {	reg_B = RLC(reg_B); cycleLength(8); }  // CB00		RLC B
void CB01(void) {	reg_C = RLC(reg_C); cycleLength(8); }  // CB01		RLC C
void CB02(void) { 	reg_D = RLC(reg_D); cycleLength(8); }  // CB02		RLC D
void CB03(void) { 	reg_E = RLC(reg_E); cycleLength(8); }  // CB03		RLC E
void CB04(void) { 	put_rH(RLC(get_rH())); cycleLength(8); }  // CB04		RLC H
void CB05(void) {   put_rL(RLC(get_rL())); cycleLength(8); }  // CB05		RLC L
void CB06(void) {   WriteMEM(reg_HL, RLC(ReadMEM(reg_HL))); cycleLength(16); } // CB06		RLC (HL)		15	4	2
void CB07(void) {   reg_A = RLC(reg_A); cycleLength(8); } // CB07		RLC A

void CB08(void) {  reg_B = RRC(reg_B); cycleLength(8); } // CB08	 	RRC 7,B
void CB09(void) {  reg_C = RRC(reg_C); cycleLength(8); } // CB09	 	RRC 7,C
void CB0A(void) {  reg_D = RRC(reg_D); cycleLength(8); } // CB0A	 	RRC 7,D
void CB0B(void) {  reg_E = RRC(reg_E); cycleLength(8); } // CB0B	 	RRC 7,E
void CB0C(void) {  put_rH(RRC(get_rH())); cycleLength(8); } // CB0C	 	RRC 7,H
void CB0D(void) {  put_rL(RRC(get_rL())); cycleLength(8); } // CB0D	 	RRC 7,L
void CB0E(void) {  WriteMEM(reg_HL, RRC(ReadMEM(reg_HL))); cycleLength(16); } // CB0E	 	RRC 7,(HL)
void CB0F(void) {  reg_A = RRC(reg_A); cycleLength(8); } // CB0F	 	RRC 7,A

void CB10(void) {  reg_B = RL(reg_B); cycleLength(8); } // CB10		RL 0,B
void CB11(void) {  reg_C = RL(reg_C); cycleLength(8); } // CB11		RL 0,C
void CB12(void) {  reg_D = RL(reg_D); cycleLength(8); } // CB12		RL 0,D
void CB13(void) {  reg_E = RL(reg_E); cycleLength(8); } // CB13		RL 0,E
void CB14(void) {  put_rH(RL(get_rH())); cycleLength(8); } // CB14	 	RL 0,H
void CB15(void) {  put_rL(RL(get_rL())); cycleLength(8); } // CB15	 	RL 0,L
void CB16(void) {  WriteMEM(reg_HL, RL(ReadMEM(reg_HL))); cycleLength(16); } // CB16	 	RL 0,(HL)
void CB17(void) {  reg_A = RL(reg_A); cycleLength(8); } // CB17	 	RL 0,A

void CB18(void) {  reg_B = RR(reg_B); cycleLength(8); } // CB18	 	RR 1,B
void CB19(void) {  reg_C = RR(reg_C); cycleLength(8); } // CB19	 	RR 1,C
void CB1A(void) {  reg_D = RR(reg_D); cycleLength(8); } // CB1A	 	RR 1,D
void CB1B(void) {  reg_E = RR(reg_E); cycleLength(8); } // CB1B	 	RR 1,E
void CB1C(void) {  put_rH(RR(get_rH())); cycleLength(8); } // CB1C	 	RR 1,H
void CB1D(void) {  put_rL(RR(get_rL())); cycleLength(8); } // CB1D	 	RR 1,L
void CB1E(void) {  WriteMEM(reg_HL, RR(ReadMEM(reg_HL))); cycleLength(16); } // CB1E RR (HL) -- BUG FIX: was passing a stray extra "1" argument RR doesn't take (copy-paste from a BIT/SET/RES-style call)
void CB1F(void) {  reg_A = RR(reg_A); cycleLength(8); } // CB1F	 	RR 1,A

void CB20(void) {  reg_B = SLA(reg_B); cycleLength(8); } // CB20		SLA 0,B
void CB21(void) {  reg_C = SLA(reg_C); cycleLength(8); } // CB21		SLA 0,C
void CB22(void) {  reg_D = SLA(reg_D); cycleLength(8); } // CB22		SLA 0,D
void CB23(void) {  reg_E = SLA(reg_E); cycleLength(8); } // CB23		SLA 0,E
void CB24(void) {  put_rH(SLA(get_rH())); cycleLength(8); } // CB24	 	SLA 0,H
void CB25(void) {  put_rL(SLA(get_rL())); cycleLength(8); } // CB25	 	SLA 0,L
void CB26(void) {  WriteMEM(reg_HL, SLA(ReadMEM(reg_HL))); cycleLength(16); } // CB26	 	SLA 0,(HL)
void CB27(void) {  reg_A = SLA(reg_A); cycleLength(8); } // CB27	 	SLA 0,A

void CB28(void) {  reg_B = SRA(reg_B); cycleLength(8); } // CB28	 	SRA 1,B
void CB29(void) {  reg_C = SRA(reg_C); cycleLength(8); } // CB29	 	SRA 1,C
void CB2A(void) {  reg_D = SRA(reg_D); cycleLength(8); } // CB2A	 	SRA 1,D
void CB2B(void) {  reg_E = SRA(reg_E); cycleLength(8); } // CB2B	 	SRA 1,E
void CB2C(void) {  put_rH(SRA(get_rH())); cycleLength(8); } // CB2C	 	SRA 1,H
void CB2D(void) {  put_rL(SRA(get_rL())); cycleLength(8); } // CB2D	 	SRA 1,L
void CB2E(void) {  WriteMEM(reg_HL, SRA(ReadMEM(reg_HL))); cycleLength(16); } // CB2E	 	SRA 1,(HL)
void CB2F(void) {  reg_A = SRA(reg_A); cycleLength(8); } // CB2F	 	SRA 1,A

void CB30(void) {  reg_B = SLL(reg_B); cycleLength(8); } // CB30		SLL 0,B
void CB31(void) {  reg_C = SLL(reg_C); cycleLength(8); } // CB31		SLL 0,C
void CB32(void) {  reg_D = SLL(reg_D); cycleLength(8); } // CB32		SLL 0,D
void CB33(void) {  reg_E = SLL(reg_E); cycleLength(8); } // CB33		SLL 0,E
void CB34(void) {  put_rH(SLL(get_rH())); cycleLength(8); } // CB34	 	SLL 0,H
void CB35(void) {  put_rL(SLL(get_rL())); cycleLength(8); } // CB35	 	SLL 0,L
void CB36(void) {  WriteMEM(reg_HL, SLL(ReadMEM(reg_HL))); cycleLength(16); } // CB36	 	SLL 0,(HL)
void CB37(void) {  reg_A = SLL(reg_A); cycleLength(8); } // CB37	 	SLL 0,A

void CB38(void) { reg_B = SRL(reg_B); cycleLength(8); } // CB38	 	SRL 1,B
void CB39(void) {  reg_C = SRL(reg_C); cycleLength(8); } // CB39	 	SRL 1,C
void CB3A(void) {  reg_D = SRL(reg_D); cycleLength(8); } // CB3A	 	SRL 1,D
void CB3B(void) {  reg_E = SRL(reg_E); cycleLength(8); } // CB3B	 	SRL 1,E
void CB3C(void) {  put_rH(SRL(get_rH())); cycleLength(8); } // CB3C	 	SRL 1,H
void CB3D(void) {  put_rL(SRL(get_rL())); cycleLength(8); } // CB3D	 	SRL 1,L
void CB3E(void) {  WriteMEM(reg_HL, SRL(ReadMEM(reg_HL))); cycleLength(16); } // CB3E	 	SRL 1,(HL)
void CB3F(void) {  reg_A = SRL(reg_A); cycleLength(8); } // CB3F	 	SRL 1,A


void CB40(void) {  BIT(0, reg_B); cycleLength(8); } // CB40		BIT 0,B
void CB41(void) {  BIT(0, reg_C); cycleLength(8); } // CB41		BIT 0,C
void CB42(void) {  BIT(0, reg_D); cycleLength(8); } // CB42		BIT 0,D
void CB43(void) {  BIT(0, reg_E); cycleLength(8); } // CB43		BIT 0,E
void CB44(void) {  BIT(0, get_rH()); cycleLength(8); } // CB44	 	BIT 0,H
void CB45(void) {  BIT(0, get_rL()); cycleLength(8); } // CB45	 	BIT 0,L
void CB46(void) {  BIT(0, ReadMEM(reg_HL)); cycleLength(12); } // CB46	 	BIT 0,(HL)
void CB47(void) {  BIT(0, reg_A); cycleLength(8); } // CB47	 	BIT 0,A

void CB48(void) {  BIT(1, reg_B); cycleLength(8); } // CB48	 	BIT 1,B
void CB49(void) {  BIT(1, reg_C); cycleLength(8); } // CB49	 	BIT 1,C
void CB4A(void) {  BIT(1, reg_D); cycleLength(8); } // CB4A	 	BIT 1,D
void CB4B(void) {  BIT(1, reg_E); cycleLength(8); } // CB4B	 	BIT 1,E
void CB4C(void) {  BIT(1, get_rH()); cycleLength(8); } // CB4C	 	BIT 1,H
void CB4D(void) {  BIT(1, get_rL()); cycleLength(8); } // CB4D	 	BIT 1,L
void CB4E(void) {  BIT(1, ReadMEM(reg_HL)); cycleLength(12); } // CB4E	 	BIT 1,(HL)
void CB4F(void) {  BIT(1, reg_A); cycleLength(8); } // CB4F	 	BIT 1,A

void CB50(void) {  BIT(2, reg_B); cycleLength(8); } // CB50		BIT 2,B
void CB51(void) {  BIT(2, reg_C); cycleLength(8); } // CB51		BIT 2,C
void CB52(void) {  BIT(2, reg_D); cycleLength(8); } // CB52		BIT 2,D
void CB53(void) {  BIT(2, reg_E); cycleLength(8); } // CB53		BIT 2,E
void CB54(void) {  BIT(2, get_rH()); cycleLength(8); } // CB54	 	BIT 2,H
void CB55(void) {  BIT(2, get_rL()); cycleLength(8); } // CB55	 	BIT 2,L
void CB56(void) {  BIT(2, ReadMEM(reg_HL)); cycleLength(12); } // CB56	 	BIT 2,(HL)
void CB57(void) {  BIT(2, reg_A); cycleLength(8); } // CB57	 	BIT 2,A

void CB58(void) {  BIT(3, reg_B); cycleLength(8); } // CB58	 	BIT 3,B
void CB59(void) {  BIT(3, reg_C); cycleLength(8); } // CB59	 	BIT 3,C
void CB5A(void) {  BIT(3, reg_D); cycleLength(8); } // CB5A	 	BIT 3,D
void CB5B(void) {  BIT(3, reg_E); cycleLength(8); } // CB5B	 	BIT 3,E
void CB5C(void) {  BIT(3, get_rH()); cycleLength(8); } // CB5C	 	BIT 3,H
void CB5D(void) {  BIT(3, get_rL()); cycleLength(8); } // CB5D	 	BIT 3,L
void CB5E(void) {  BIT(3, ReadMEM(reg_HL)); cycleLength(12); } // CB5E	 	BIT 3,(HL)
void CB5F(void) {  BIT(3, reg_A); cycleLength(8); } // CB5F	 	BIT 3,A

void CB60(void) {  BIT(4, reg_B); cycleLength(8); } // CB60		BIT 4,B
void CB61(void) {  BIT(4, reg_C); cycleLength(8); } // CB61		BIT 4,C
void CB62(void) {  BIT(4, reg_D); cycleLength(8); } // CB62		BIT 4,D
void CB63(void) {  BIT(4, reg_E); cycleLength(8); } // CB63		BIT 4,E
void CB64(void) {  BIT(4, get_rH()); cycleLength(8); } // CB64	 	BIT 4,H
void CB65(void) {  BIT(4, get_rL()); cycleLength(8); } // CB65	 	BIT 4,L
void CB66(void) {  BIT(4, ReadMEM(reg_HL)); cycleLength(12); } // CB66	 	BIT 4,(HL)
void CB67(void) {  BIT(4, reg_A); cycleLength(8); } // CB67	 	BIT 4,A

void CB68(void) {  BIT(5, reg_B); cycleLength(8); } // CB68	 	BIT 5,B
void CB69(void) {  BIT(5, reg_C); cycleLength(8); } // CB69	 	BIT 5,C
void CB6A(void) {  BIT(5, reg_D); cycleLength(8); } // CB6A	 	BIT 5,D
void CB6B(void) {  BIT(5, reg_E); cycleLength(8); } // CB6B	 	BIT 5,E
void CB6C(void) {  BIT(5, get_rH()); cycleLength(8); } // CB6C	 	BIT 5,H
void CB6D(void) {  BIT(5, get_rL()); cycleLength(8); } // CB6D	 	BIT 5,L
void CB6E(void) {  BIT(5, ReadMEM(reg_HL)); cycleLength(12); } // CB6E	 	BIT 5,(HL)
void CB6F(void) {  BIT(5, reg_A); cycleLength(8); } // CB6F	 	BIT 5,A

void CB70(void) {  BIT(6, reg_B); cycleLength(8); } // CB70		BIT 6,B
void CB71(void) {  BIT(6, reg_C); cycleLength(8); } // CB71		BIT 6,C
void CB72(void) {  BIT(6, reg_D); cycleLength(8); } // CB72		BIT 6,D
void CB73(void) {  BIT(6, reg_E); cycleLength(8); } // CB73		BIT 6,E
void CB74(void) {  BIT(6, get_rH()); cycleLength(8); } // CB74	 	BIT 6,H
void CB75(void) {  BIT(6, get_rL()); cycleLength(8); } // CB75	 	BIT 6,L
void CB76(void) {  BIT(6, ReadMEM(reg_HL)); cycleLength(12); } // CB76	 	BIT 6,(HL)
void CB77(void) {  BIT(6, reg_A); cycleLength(8); } // CB77	 	BIT 6,A

void CB78(void) {  BIT(7, reg_B); cycleLength(8); } // CB78	 	BIT 7,B
void CB79(void) {  BIT(7, reg_C); cycleLength(8); } // CB79	 	BIT 7,C
void CB7A(void) {  BIT(7, reg_D); cycleLength(8); } // CB7A	 	BIT 7,D
void CB7B(void) {  BIT(7, reg_E); cycleLength(8); } // CB7B	 	BIT 7,E
void CB7C(void) {  BIT(7, get_rH()); cycleLength(8); } // CB7C	 	BIT 7,H
void CB7D(void) {  BIT(7, get_rL()); cycleLength(8); } // CB7D	 	BIT 7,L
void CB7E(void) {  BIT(7, ReadMEM(reg_HL)); cycleLength(12); } // CB7E	 	BIT 7,(HL)
void CB7F(void) {  BIT(7, reg_A); cycleLength(8); } // CB7F	 	BIT 7,A


void CB80(void) {  reg_B = RES(0, reg_B); cycleLength(8); } // CB80		RES 0,B
void CB81(void) {  reg_C = RES(0, reg_C); cycleLength(8); } // CB81		RES 0,C
void CB82(void) {  reg_D = RES(0, reg_D); cycleLength(8); } // CB82		RES 0,D
void CB83(void) {  reg_E = RES(0, reg_E); cycleLength(8); } // CB83		RES 0,E
void CB84(void) {  put_rH(RES(0, get_rH())); cycleLength(8); } // CB84	 	RES 0,H
void CB85(void) {  put_rL(RES(0, get_rL())); cycleLength(8); } // CB85	 	RES 0,L
void CB86(void) {  WriteMEM(reg_HL, RES(0, ReadMEM(reg_HL))); cycleLength(16); } // CB86	 	RES 0,(HL)
void CB87(void) {  reg_A = RES(0, reg_A); cycleLength(8); } // CB87	 	RES 0,A

void CB88(void) {  reg_B = RES(1, reg_B); cycleLength(8); } // CB88	 	RES 1,B
void CB89(void) {  reg_C = RES(1, reg_C); cycleLength(8); } // CB89	 	RES 1,C
void CB8A(void) {  reg_D = RES(1, reg_D); cycleLength(8); } // CB8A	 	RES 1,D
void CB8B(void) {  reg_E = RES(1, reg_E); cycleLength(8); } // CB8B	 	RES 1,E
void CB8C(void) {  put_rH(RES(1, get_rH())); cycleLength(8); } // CB8C	 	RES 1,H
void CB8D(void) {  put_rL(RES(1, get_rL())); cycleLength(8); } // CB8D	 	RES 1,L
void CB8E(void) {  WriteMEM(reg_HL, RES(1, ReadMEM(reg_HL))); cycleLength(16); } // CB8E	 	RES 1,(HL)
void CB8F(void) {  reg_A = RES(1, reg_A); cycleLength(8); } // CB8F	 	RES 1,A

void CB90(void) {  reg_B = RES(2, reg_B); cycleLength(8); } // CB90		RES 2,B
void CB91(void) {  reg_C = RES(2, reg_C); cycleLength(8); } // CB91		RES 2,C
void CB92(void) {  reg_D = RES(2, reg_D); cycleLength(8); } // CB92		RES 2,D
void CB93(void) {  reg_E = RES(2, reg_E); cycleLength(8); } // CB93		RES 2,E
void CB94(void) {  put_rH(RES(2, get_rH())); cycleLength(8); } // CB94	 	RES 2,H
void CB95(void) {  put_rL(RES(2, get_rL())); cycleLength(8); } // CB95	 	RES 2,L
void CB96(void) {  WriteMEM(reg_HL, RES(2, ReadMEM(reg_HL))); cycleLength(16); } // CB96	 	RES 2,(HL)
void CB97(void) {  reg_A = RES(2, reg_A); cycleLength(8); } // CB97	 	RES 2,A

void CB98(void) {  reg_B = RES(3, reg_B); cycleLength(8); } // CB98	 	RES 3,B
void CB99(void) {  reg_C = RES(3, reg_C); cycleLength(8); } // CB99	 	RES 3,C
void CB9A(void) {  reg_D = RES(3, reg_D); cycleLength(8); } // CB9A	 	RES 3,D
void CB9B(void) {  reg_E = RES(3, reg_E); cycleLength(8); } // CB9B	 	RES 3,E
void CB9C(void) {  put_rH(RES(3, get_rH())); cycleLength(8); } // CB9C	 	RES 3,H
void CB9D(void) {  put_rL(RES(3, get_rL())); cycleLength(8); } // CB9D	 	RES 3,L
void CB9E(void) {  WriteMEM(reg_HL, RES(3, ReadMEM(reg_HL))); cycleLength(16); } // CB9E	 	RES 3,(HL)
void CB9F(void) {  reg_A = RES(3, reg_A); cycleLength(8); } // CB9F	 	RES 3,A

void CBA0(void) {  reg_B = RES(4, reg_B); cycleLength(8); } // CBA0		RES 4,B
void CBA1(void) {  reg_C = RES(4, reg_C); cycleLength(8); } // CBA1		RES 4,C
void CBA2(void) {  reg_D = RES(4, reg_D); cycleLength(8); } // CBA2		RES 4,D
void CBA3(void) {  reg_E = RES(4, reg_E); cycleLength(8); } // CBA3		RES 4,E
void CBA4(void) { put_rH(RES(4, get_rH())); cycleLength(8); } // CBA4	 	RES 4,H
void CBA5(void) {  put_rL(RES(4, get_rL())); cycleLength(8); } // CBA5	 	RES 4,L
void CBA6(void) {  WriteMEM(reg_HL, RES(4, ReadMEM(reg_HL))); cycleLength(16); } // CBA6	 	RES 4,(HL)
void CBA7(void) {  reg_A = RES(4, reg_A); cycleLength(8); } // CBA7	 	RES 4,A

void CBA8(void) {  reg_B = RES(5, reg_B); cycleLength(8); } // CBA8	 	RES 5,B
void CBA9(void) {  reg_C = RES(5, reg_C); cycleLength(8); } // CBA9	 	RES 5,C
void CBAA(void) {  reg_D = RES(5, reg_D); cycleLength(8); } // CBAA	 	RES 5,D
void CBAB(void) {  reg_E = RES(5, reg_E); cycleLength(8); } // CBAB	 	RES 5,E
void CBAC(void) {  put_rH(RES(5, get_rH())); cycleLength(8); } // CBAC	 	RES 5,H
void CBAD(void) {  put_rL(RES(5, get_rL())); cycleLength(8); } // CBAD	 	RES 5,L
void CBAE(void) {  WriteMEM(reg_HL, RES(5, ReadMEM(reg_HL))); cycleLength(16); } // CBAE	 	RES 5,(HL)
void CBAF(void) {  reg_A = RES(5, reg_A); cycleLength(8); } // CBAF	 	RES 5,A

void CBB0(void) {  reg_B = RES(6, reg_B); cycleLength(8); } // CBB0		RES 6,B
void CBB1(void) {  reg_C = RES(6, reg_C); cycleLength(8); } // CBB1		RES 6,C
void CBB2(void) {  reg_D = RES(6, reg_D); cycleLength(8); } // CBB2		RES 6,D
void CBB3(void) {  reg_E = RES(6, reg_E); cycleLength(8); } // CBB3		RES 6,E
void CBB4(void) {  put_rH(RES(6, get_rH())); cycleLength(8); } // CBB4	 	RES 6,H
void CBB5(void) {  put_rL(RES(6, get_rL())); cycleLength(8); } // CBB5	 	RES 6,L
void CBB6(void) {  WriteMEM(reg_HL, RES(6, ReadMEM(reg_HL))); cycleLength(16); } // CBB6	 	RES 6,(HL)
void CBB7(void) {  reg_A = RES(6, reg_A); cycleLength(8); } // CBB7	 	RES 6,A

void CBB8(void) {  reg_B = RES(7, reg_B); cycleLength(8); } // CBB8	 	RES 7,B
void CBB9(void) {  reg_C = RES(7, reg_C); cycleLength(8); } // CBB9	 	RES 7,C
void CBBA(void) {  reg_D = RES(7, reg_D); cycleLength(8); } // CBBA	 	RES 7,D
void CBBB(void) {  reg_E = RES(7, reg_E); cycleLength(8); } // CBBB	 	RES 7,E
void CBBC(void) { put_rH(RES(7, get_rH())); cycleLength(8); } // CBBC	 	RES 7,H
void CBBD(void) {  put_rL(RES(7, get_rL())); cycleLength(8); } // CBBD	 	RES 7,L
void CBBE(void) {  WriteMEM(reg_HL, RES(7, ReadMEM(reg_HL))); cycleLength(16); } // CBBE	 	RES 7,(HL)
void CBBF(void) {  reg_A = RES(7, reg_A); cycleLength(8); } // CBBF	 	RES 7,A

void CBC0(void) {  reg_B = SET(0, reg_B); cycleLength(8); } // CBC0		SET 0,B
void CBC1(void) {  reg_C = SET(0, reg_C); cycleLength(8); } // CBC1		SET 0,C
void CBC2(void) {  reg_D = SET(0, reg_D); cycleLength(8); } // CBC2		SET 0,D
void CBC3(void) {  reg_E = SET(0, reg_E); cycleLength(8); } // CBC3		SET 0,E
void CBC4(void) {  put_rH(SET(0, get_rH())); cycleLength(8); } // CBC4	 	SET 0,H
void CBC5(void) {  put_rL(SET(0, get_rL())); cycleLength(8); } // CBC5	 	SET 0,L
void CBC6(void) {  WriteMEM(reg_HL, SET(0, ReadMEM(reg_HL))); cycleLength(16); } // CBC6	 	SET 0,(HL)
void CBC7(void) {  reg_A = SET(0, reg_A); cycleLength(8); } // CBC7	 	SET 0,A

void CBC8(void) { reg_B = SET(1, reg_B); cycleLength(8); } // CBC8	 	SET 1,B
void CBC9(void) {  reg_C = SET(1, reg_C); cycleLength(8); } // CBC9	 	SET 1,C
void CBCA(void) {  reg_D = SET(1, reg_D); cycleLength(8); } // CBCA	 	SET 1,D
void CBCB(void) {  reg_E = SET(1, reg_E); cycleLength(8); } // CBCB	 	SET 1,E
void CBCC(void) {  put_rH(SET(1, get_rH())); cycleLength(8); } // CBCC	 	SET 1,H
void CBCD(void) {  put_rL(SET(1, get_rL())); cycleLength(8); } // CBCD	 	SET 1,L
void CBCE(void) {  WriteMEM(reg_HL, SET(1, ReadMEM(reg_HL))); cycleLength(16); } // CBCE	 	SET 1,(HL)
void CBCF(void) {  reg_A = SET(1, reg_A); cycleLength(8); } // CBCF	 	SET 1,A

void CBD0(void) {  reg_B = SET(2, reg_B); cycleLength(8); } // CBD0		SET 2,B
void CBD1(void) {  reg_C = SET(2, reg_C); cycleLength(8); } // CBD1		SET 2,C
void CBD2(void) {  reg_D = SET(2, reg_D); cycleLength(8); } // CBD2		SET 2,D
void CBD3(void) {  reg_E = SET(2, reg_E); cycleLength(8); } // CBD3		SET 2,E
void CBD4(void) {  put_rH(SET(2, get_rH())); cycleLength(8); } // CBD4	 	SET 2,H
void CBD5(void) {  put_rL(SET(2, get_rL())); cycleLength(8); } // CBD5	 	SET 2,L
void CBD6(void) {  WriteMEM(reg_HL, SET(2, ReadMEM(reg_HL))); cycleLength(16); } // CBD6	 	SET 2,(HL)
void CBD7(void) {  reg_A = SET(2, reg_A); cycleLength(8); } // CBD7	 	SET 2,A

void CBD8(void) {  reg_B = SET(3, reg_B); cycleLength(8); } // CBD8	 	SET 3,B
void CBD9(void) {  reg_C = SET(3, reg_C); cycleLength(8); } // CBD9	 	SET 3,C
void CBDA(void) {  reg_D = SET(3, reg_D); cycleLength(8); } // CBDA	 	SET 3,D
void CBDB(void) {  reg_E = SET(3, reg_E); cycleLength(8); } // CBDB	 	SET 3,E
void CBDC(void) {  put_rH(SET(3, get_rH())); cycleLength(8); } // CBDC	 	SET 3,H
void CBDD(void) {  put_rL(SET(3, get_rL())); cycleLength(8); } // CBDD	 	SET 3,L
void CBDE(void) {  WriteMEM(reg_HL, SET(3, ReadMEM(reg_HL))); cycleLength(16); } // CBDE	 	SET 3,(HL)
void CBDF(void) {  reg_A = SET(3, reg_A); cycleLength(8); } // CBDF	 	SET 3,A

void CBE0(void) { reg_B = SET(4, reg_B); cycleLength(8); } // CBE0		SET 4,B
void CBE1(void) {  reg_C = SET(4, reg_C); cycleLength(8); } // CBE1		SET 4,C
void CBE2(void) {  reg_D = SET(4, reg_D); cycleLength(8); } // CBE2		SET 4,D
void CBE3(void) {  reg_E = SET(4, reg_E); cycleLength(8); } // CBE3		SET 4,E
void CBE4(void) {  put_rH(SET(4, get_rH())); cycleLength(8); } // CBE4	 	SET 4,H
void CBE5(void) {  put_rL(SET(4, get_rL())); cycleLength(8); } // CBE5	 	SET 4,L
void CBE6(void) {  WriteMEM(reg_HL, SET(4, ReadMEM(reg_HL))); cycleLength(16); } // CBE6	 	SET 4,(HL)
void CBE7(void) {  reg_A = SET(4, reg_A); cycleLength(8); } // CBE7	 	SET 4,A

void CBE8(void) {  reg_B = SET(5, reg_B); cycleLength(8); } // CBE8	 	SET 5,B
void CBE9(void) {  reg_C = SET(5, reg_C); cycleLength(8); } // CBE9	 	SET 5,C
void CBEA(void) {  reg_D = SET(5, reg_D); cycleLength(8); } // CBEA	 	SET 5,D
void CBEB(void) {  reg_E = SET(5, reg_E); cycleLength(8); } // CBEB	 	SET 5,E
void CBEC(void) {  put_rH(SET(5, get_rH())); cycleLength(8); } // CBEC	 	SET 5,H
void CBED(void) {  put_rL(SET(5, get_rL())); cycleLength(8); } // CBED	 	SET 5,L
void CBEE(void) {  WriteMEM(reg_HL, SET(5, ReadMEM(reg_HL))); cycleLength(16); } // CBEE	 	SET 5,(HL)
void CBEF(void) {  reg_A = SET(5, reg_A); cycleLength(8); } // CBEF	 	SET 5,A

void CBF0(void) {  reg_B = SET(6, reg_B); cycleLength(8); } // CBF0		SET 6,B
void CBF1(void) {  reg_C = SET(6, reg_C); cycleLength(8); } // CBF1		SET 6,C
void CBF2(void) {  reg_D = SET(6, reg_D); cycleLength(8); } // CBF2		SET 6,D
void CBF3(void) {  reg_E = SET(6, reg_E); cycleLength(8); } // CBF3		SET 6,E
void CBF4(void) {  put_rH(SET(6, get_rH())); cycleLength(8); } // CBF4	 	SET 6,H
void CBF5(void) {  put_rL(SET(6, get_rL())); cycleLength(8); } // CBF5	 	SET 6,L
void CBF6(void) {  WriteMEM(reg_HL, SET(6, ReadMEM(reg_HL))); cycleLength(16); } // CBF6	 	SET 6,(HL)
void CBF7(void) { reg_A = SET(6, reg_A); cycleLength(8); } // CBF7	 	SET 6,A

void CBF8(void) {  reg_B = SET(7, reg_B); cycleLength(8); } // CBF8	 	SET 7,B
void CBF9(void) {  reg_C = SET(7, reg_C); cycleLength(8); } // CBF9	 	SET 7,C
void CBFA(void) {  reg_D = SET(7, reg_D); cycleLength(8); } // CBFA	 	SET 7,D
void CBFB(void) {  reg_E = SET(7, reg_E); cycleLength(8); } // CBFB	 	SET 7,E
void CBFC(void) {  put_rH(SET(7, get_rH())); cycleLength(8); } // CBFC	 	SET 7,H
void CBFD(void) {  put_rL(SET(7, get_rL())); cycleLength(8); } // CBFD	 	SET 7,L
void CBFE(void) {  WriteMEM(reg_HL, SET(7, ReadMEM(reg_HL))); cycleLength(16); } // CBFE	 	SET 7,(HL)
void CBFF(void) {  reg_A = SET(7, reg_A); cycleLength(8); } // CBFF	 	SET 7,A

// Opcode dispatch tables. These are real (initialized) definitions, so they
// live in exactly one translation unit; emu.h only carries the extern
// declarations. (Previously the initialized arrays lived directly in the
// header, which happened to work only because emu.h was never included by
// more than one .c file - the moment a second file includes it, e.g. a
// test harness or a future split of emu.c, the linker sees two definitions
// of the same symbol and fails.)
funcPtr instructions[]= {OP00, OP01, OP02, OP03, OP04, OP05, OP06, OP07, OP08, OP09, OP0A, OP0B, OP0C, OP0D, OP0E, OP0F,
						 OP10, OP11, OP12, OP13, OP14, OP15, OP16, OP17, OP18, OP19, OP1A, OP1B, OP1C, OP1D, OP1E, OP1F,
						 OP20, OP21, OP22, OP23, OP24, OP25, OP26, OP27, OP28, OP29, OP2A, OP2B, OP2C, OP2D, OP2E, OP2F,
						 OP30, OP31, OP32, OP33, OP34, OP35, OP36, OP37, OP38, OP39, OP3A, OP3B, OP3C, OP3D, OP3E, OP3F,
						 OP40, OP41, OP42, OP43, OP44, OP45, OP46, OP47, OP48, OP49, OP4A, OP4B, OP4C, OP4D, OP4E, OP4F,
						 OP50, OP51, OP52, OP53, OP54, OP55, OP56, OP57, OP58, OP59, OP5A, OP5B, OP5C, OP5D, OP5E, OP5F,
						 OP60, OP61, OP62, OP63, OP64, OP65, OP66, OP67, OP68, OP69, OP6A, OP6B, OP6C, OP6D, OP6E, OP6F,
						 OP70, OP71, OP72, OP73, OP74, OP75, OP76, OP77, OP78, OP79, OP7A, OP7B, OP7C, OP7D, OP7E, OP7F,
						 OP80, OP81, OP82, OP83, OP84, OP85, OP86, OP87, OP88, OP89, OP8A, OP8B, OP8C, OP8D, OP8E, OP8F,
						 OP90, OP91, OP92, OP93, OP94, OP95, OP96, OP97, OP98, OP99, OP9A, OP9B, OP9C, OP9D, OP9E, OP9F,
						 OPA0, OPA1, OPA2, OPA3, OPA4, OPA5, OPA6, OPA7, OPA8, OPA9, OPAA, OPAB, OPAC, OPAD, OPAE, OPAF,
						 OPB0, OPB1, OPB2, OPB3, OPB4, OPB5, OPB6, OPB7, OPB8, OPB9, OPBA, OPBB, OPBC, OPBD, OPBE, OPBF,
						 OPC0, OPC1, OPC2, OPC3, OPC4, OPC5, OPC6, OPC7, OPC8, OPC9, OPCA, OPCB, OPCC, OPCD, OPCE, OPCF,
						 OPD0, OPD1, OPD2, OPD3, OPD4, OPD5, OPD6, OPD7, OPD8, OPD9, OPDA, OPDB, OPDC, OPDD, OPDE, OPDF,
						 OPE0, OPE1, OPE2, OPE3, OPE4, OPE5, OPE6, OPE7, OPE8, OPE9, OPEA, OPEB, OPEC, OPED, OPEE, OPEF,
						 OPF0, OPF1, OPF2, OPF3, OPF4, OPF5, OPF6, OPF7, OPF8, OPF9, OPFA, OPFB, OPFC, OPFD, OPFE, OPFF};

funcPtr CBinst[]= { CB00, CB01, CB02, CB03, CB04, CB05, CB06, CB07, CB08, CB09, CB0A, CB0B, CB0C, CB0D, CB0E, CB0F,
						 CB10, CB11, CB12, CB13, CB14, CB15, CB16, CB17, CB18, CB19, CB1A, CB1B, CB1C, CB1D, CB1E, CB1F,
						 CB20, CB21, CB22, CB23, CB24, CB25, CB26, CB27, CB28, CB29, CB2A, CB2B, CB2C, CB2D, CB2E, CB2F,
						 CB30, CB31, CB32, CB33, CB34, CB35, CB36, CB37, CB38, CB39, CB3A, CB3B, CB3C, CB3D, CB3E, CB3F,
						 CB40, CB41, CB42, CB43, CB44, CB45, CB46, CB47, CB48, CB49, CB4A, CB4B, CB4C, CB4D, CB4E, CB4F,
						 CB50, CB51, CB52, CB53, CB54, CB55, CB56, CB57, CB58, CB59, CB5A, CB5B, CB5C, CB5D, CB5E, CB5F,
						 CB60, CB61, CB62, CB63, CB64, CB65, CB66, CB67, CB68, CB69, CB6A, CB6B, CB6C, CB6D, CB6E, CB6F,
						 CB70, CB71, CB72, CB73, CB74, CB75, CB76, CB77, CB78, CB79, CB7A, CB7B, CB7C, CB7D, CB7E, CB7F,
						 CB80, CB81, CB82, CB83, CB84, CB85, CB86, CB87, CB88, CB89, CB8A, CB8B, CB8C, CB8D, CB8E, CB8F,
						 CB90, CB91, CB92, CB93, CB94, CB95, CB96, CB97, CB98, CB99, CB9A, CB9B, CB9C, CB9D, CB9E, CB9F,
						 CBA0, CBA1, CBA2, CBA3, CBA4, CBA5, CBA6, CBA7, CBA8, CBA9, CBAA, CBAB, CBAC, CBAD, CBAE, CBAF,
						 CBB0, CBB1, CBB2, CBB3, CBB4, CBB5, CBB6, CBB7, CBB8, CBB9, CBBA, CBBB, CBBC, CBBD, CBBE, CBBF,
						 CBC0, CBC1, CBC2, CBC3, CBC4, CBC5, CBC6, CBC7, CBC8, CBC9, CBCA, CBCB, CBCC, CBCD, CBCE, CBCF,
						 CBD0, CBD1, CBD2, CBD3, CBD4, CBD5, CBD6, CBD7, CBD8, CBD9, CBDA, CBDB, CBDC, CBDD, CBDE, CBDF,
						 CBE0, CBE1, CBE2, CBE3, CBE4, CBE5, CBE6, CBE7, CBE8, CBE9, CBEA, CBEB, CBEC, CBED, CBEE, CBEF,
						 CBF0, CBF1, CBF2, CBF3, CBF4, CBF5, CBF6, CBF7, CBF8, CBF9, CBFA, CBFB, CBFC, CBFD, CBFE, CBFF};
