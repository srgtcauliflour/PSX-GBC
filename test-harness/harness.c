// Host-native aGBe core test harness.
//
// This links the *unmodified* platform-independent emulator core (emu.c +
// opcodes.c) against a native Linux build so it can be run against real
// Game Boy test ROMs (Blargg's cpu_instrs, etc.) on a dev machine, long
// before a PS1 devkit or console is involved. It stubs out everything
// PSX-specific (graphics, pad, CD) since the core never needs them to run
// the CPU.
//
// Usage: ./harness <path-to-rom.gb> [max_instructions]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <libgte.h>
#include <libgpu.h>
#include <libcd.h>
#include <libapi.h>
#include "main.h"
#include "pad.h"
#include "psx.h"
#include "emu.h"

// ---- Platform hooks (see psx-stubs) ----
unsigned long PadRead(int pad_num) { (void)pad_num; return 0; }
void PadInit(int mode) { (void)mode; }
void Draw_Buffer(int *screenBuffer) { (void)screenBuffer; }
void Draw_Buffer_SPRT(int *screenBuffer) { (void)screenBuffer; }
void Draw_Buffer_Pixel_Blitting(int *screenBuffer) { (void)screenBuffer; }
void PrepScreen(void) {}
void RenderWorld(BYTE re, BYTE gr, BYTE bl) { (void)re; (void)gr; (void)bl; }

// The one real (defining) declaration of ROM/pad/lastpad for this host
// build - psx.h/pad.h only extern-declare them now (see the BUG FIX notes
// in those headers), same as the real psx.c would provide on target.
BYTE *ROM;
u_long pad, lastpad;

// ---- Serial-port capture (this is how Blargg's test ROMs report PASS/FAIL) --
static char serial_log[65536];
static int serial_len = 0;
static void on_serial_byte(BYTE b) {
    if (serial_len < (int)sizeof(serial_log) - 1) {
        serial_log[serial_len++] = (char)b;
        serial_log[serial_len] = 0;
    }
    putchar(b);
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <rom.gb> [max_instructions]\n", argv[0]);
        return 2;
    }

    long max_instr = (argc >= 3) ? atol(argv[2]) : 200000000L;

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    BYTE *buf = (BYTE *)malloc(size);
    if (fread(buf, 1, size, f) != (size_t)size) { fprintf(stderr, "short read\n"); return 2; }
    fclose(f);

    ROM = buf;
    SerialByteSentHook = on_serial_byte;

    loadRom();
    reset_Z80();

    fprintf(stderr, "[harness] loaded %s (%ld bytes), cart type 0x%02X, romsize idx 0x%02X, ramsize idx 0x%02X\n",
            argv[1], size, CARTTYPE, ROMSIZE, RAMSIZE);

    int do_trace = (argc >= 4 && strcmp(argv[3], "trace") == 0);

    long i;
    WORD lastPC = 0xFFFF;
    int stuckCount = 0;
    for (i = 0; i < max_instr && EMULATING; i++) {
        if (IME && (IFLAG & IER)) {
            interrupt();
        }

        if (do_trace) {
            printf("i=%ld PC=%04X SP=%04X A=%02X F=%02X B=%02X C=%02X D=%02X E=%02X H=%02X L=%02X LY=%02X STAT=%02X op=%02X opnd=%02X\n",
                   i, reg_PC, reg_SP, reg_A, reg_F, reg_B, reg_C, reg_D, reg_E, get_rH(), get_rL(), LCDY, LCDSTATUS, ReadMEM(reg_PC), ReadMEM(reg_PC+1));
        }

        // Detect the classic "test finished" spin loop (JR $FE, i.e. jump to self)
        // that Blargg's ROMs drop into once they've printed their result.
        if (reg_PC == lastPC) {
            stuckCount++;
            if (stuckCount > 1000) {
                fprintf(stderr, "\n[harness] CPU parked at PC=%04X after %ld instructions - test complete.\n", reg_PC, i);
                break;
            }
        } else {
            stuckCount = 0;
        }
        lastPC = reg_PC;

        if (getenv("TRACE") && (i % 500000 == 0)) {
            fprintf(stderr, "[trace] i=%ld PC=%04X SP=%04X A=%02X op=%02X IME=%d IE=%02X IF=%02X LY=%02X\n",
                    i, reg_PC, reg_SP, reg_A, ReadMEM(reg_PC), IME, IER, IFLAG, LCDY);
        }

        instructions[ReadMEM(reg_PC++)]();
    }

    if (i >= max_instr) {
        fprintf(stderr, "\n[harness] hit instruction cap (%ld) without the CPU parking - treating as hang/timeout.\n", max_instr);
    }

    printf("\n");
    if (strstr(serial_log, "Passed")) {
        printf("[harness] RESULT: PASS\n");
        return 0;
    } else if (strstr(serial_log, "Failed")) {
        printf("[harness] RESULT: FAIL\n");
        return 1;
    } else {
        printf("[harness] RESULT: UNKNOWN (no Blargg-style Passed/Failed banner seen)\n");
        return 3;
    }
}
