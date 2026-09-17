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
#include <stdint.h>
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
static int draw_buffer_call_count = 0;
void Draw_Buffer(int *screenBuffer, unsigned short *screenBufferColor, int gbcMode) {
    draw_buffer_call_count++;
    if (getenv("TRACE")) fprintf(stderr, "[Draw_Buffer] call #%d\n", draw_buffer_call_count);
    if (getenv("ROWSUMMARY")) {
        int r, c, nonwhite;
        fprintf(stderr, "[Draw_Buffer] call #%d FRAMECOUNT=%d LCDC=%02X SCRX=%d SCRY=%d row summary (non-white pixel count per row):\n",
                draw_buffer_call_count, FRAMECOUNT, LCDCONTROL, SCRX, SCRY);
        for (r = 0; r < 144; r++) {
            nonwhite = 0;
            for (c = 0; c < 160; c++) {
                if ((screenBuffer[r*160+c] & 0x03) != 0) nonwhite++;
            }
            if (nonwhite > 0) fprintf(stderr, "  row %d: %d non-white pixels\n", r, nonwhite);
        }
    }
    const char *dump_path = getenv("DUMP_PPM");
    const char *dump_at_str = getenv("DUMP_AT");
    int dump_at = dump_at_str ? atoi(dump_at_str) : -1;
    if (dump_path && (dump_at < 0 || dump_at == draw_buffer_call_count)) {
        FILE *f = fopen(dump_path, "wb");
        if (f) {
            int i;
            if (gbcMode && screenBufferColor) {
                // Real color dump (P6) - each pixel is a 15-bit RGB555
                // value (5 bits per channel); scale each channel to 8
                // bits by replicating its top bits into the low bits
                // (c5<<3 | c5>>2) rather than a plain multiply, which
                // maps the full 0-31 range onto the full 0-255 range
                // evenly instead of leaving the brightest value short of
                // 255.
                fprintf(f, "P6\n160 144\n255\n");
                for (i = 0; i < 160*144; i++) {
                    unsigned short c = screenBufferColor[i];
                    unsigned char r5 = c & 0x1F;
                    unsigned char g5 = (c >> 5) & 0x1F;
                    unsigned char b5 = (c >> 10) & 0x1F;
                    fputc((r5 << 3) | (r5 >> 2), f);
                    fputc((g5 << 3) | (g5 >> 2), f);
                    fputc((b5 << 3) | (b5 >> 2), f);
                }
            } else {
                fprintf(f, "P5\n160 144\n255\n");
                for (i = 0; i < 160*144; i++) {
                    unsigned char shade = screenBuffer[i] & 0x03;
                    unsigned char gray = 255 - (shade * 85); // 0->255(white),1->170,2->85,3->0(black)
                    fputc(gray, f);
                }
            }
            fclose(f);
        }
    }
}
void Draw_Buffer_SPRT(int *screenBuffer) { (void)screenBuffer; }
void Draw_Buffer_Pixel_Blitting(int *screenBuffer) { (void)screenBuffer; }

// Host stub for the real PS1 SPU-driving UpdateAudio() in psx.c (which
// this harness doesn't build at all - it uses real PSn00bSDK headers
// that don't compile on the host). No actual audio hardware exists here
// to drive, but TRACE_AUDIO=1 dumps each channel's current
// frequency/volume/enabled state once per frame, which is the one way
// to sanity-check the APU core's behavior over time without real
// playback - e.g. confirming a music-playing ROM's channels actually
// change frequency/volume over time instead of sitting static.
// ---- DUMP_WAV: real, listenable PCM export of the APU core's output ----
// Independent of (and doesn't test) the PS1 SPU integration in psx.c -
// this mixes the already-verified APU core's own channel outputs
// directly in software, entirely on the host, so a real audio file can
// be listened to and inspected (pitch, rhythm, envelope shape) as an
// extra layer of confidence beyond the register-level synthetic tests.
#define WAV_SAMPLE_RATE 44100
#define WAV_CPU_CLOCK 4194304
#define WAV_MAX_SECONDS 60
static int16_t *wavBuffer = NULL;
static long wavSampleCount = 0;
static long wavMaxSamples = 0;
static int32_t wavAccum = 0;

static void WriteWavFile(void) {
    if (!wavBuffer || wavSampleCount == 0) {
        return;
    }
    const char *path = getenv("DUMP_WAV");
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    uint32_t dataBytes = (uint32_t) (wavSampleCount * 2 * sizeof(int16_t));
    uint32_t riffSize = 36 + dataBytes;
    uint32_t sr = WAV_SAMPLE_RATE, byteRate = WAV_SAMPLE_RATE * 2 * 2;
    uint16_t blockAlign = 4, bitsPerSample = 16, channels = 2, fmt = 1;
    fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&channels, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&byteRate, 4, 1, f); fwrite(&blockAlign, 2, 1, f); fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    fwrite(wavBuffer, sizeof(int16_t), wavSampleCount * 2, f);
    fclose(f);
    fprintf(stderr, "[wav] wrote %ld samples (%.1fs) to %s\n", wavSampleCount, (double) wavSampleCount / WAV_SAMPLE_RATE, path);
}

void AudioSampleHook(int cycles) {
    if (!getenv("DUMP_WAV")) {
        return;
    }
    if (!wavBuffer) {
        wavMaxSamples = WAV_SAMPLE_RATE * WAV_MAX_SECONDS;
        wavBuffer = malloc(wavMaxSamples * 2 * sizeof(int16_t));
        atexit(WriteWavFile);
    }
    wavAccum += cycles * WAV_SAMPLE_RATE;
    while (wavAccum >= WAV_CPU_CLOCK && wavSampleCount < wavMaxSamples) {
        wavAccum -= WAV_CPU_CLOCK;

        int c1 = APUChannelOutput(1);
        int c2 = APUChannelOutput(2);
        int c3 = APUChannelOutput(3);
        int c4 = APUChannelOutput(4);
        // Convert each channel's 0-15 output to a centered -15..+15
        // value before summing, same convention real hardware's DAC
        // uses before analog mixing - a DAC-off channel (-1) contributes
        // nothing at all, not a centered "0".
        int b1 = (c1 >= 0) ? (c1 * 2 - 15) : 0;
        int b2 = (c2 >= 0) ? (c2 * 2 - 15) : 0;
        int b3 = (c3 >= 0) ? (c3 * 2 - 15) : 0;
        int b4 = (c4 >= 0) ? (c4 * 2 - 15) : 0;

        int left = 0, right = 0;
        if (NR51 & 0x10) left += b1;
        if (NR51 & 0x01) right += b1;
        if (NR51 & 0x20) left += b2;
        if (NR51 & 0x02) right += b2;
        if (NR51 & 0x40) left += b3;
        if (NR51 & 0x04) right += b3;
        if (NR51 & 0x80) left += b4;
        if (NR51 & 0x08) right += b4;

        int masterLeft = (NR50 >> 4) & 0x07;
        int masterRight = NR50 & 0x07;
        // left/right are now within roughly -60..+60 (4 channels, each
        // -15..+15) before the master volume's 1-8x scale - x68 brings
        // the loudest possible combination close to using the full
        // 16-bit range without clipping it.
        left = left * (masterLeft + 1) * 68;
        right = right * (masterRight + 1) * 68;
        if (left > 32767) left = 32767; if (left < -32768) left = -32768;
        if (right > 32767) right = 32767; if (right < -32768) right = -32768;

        wavBuffer[wavSampleCount * 2] = (int16_t) left;
        wavBuffer[wavSampleCount * 2 + 1] = (int16_t) right;
        wavSampleCount++;
    }
}

void UpdateAudio(void) {
	static int callCount = 0;
	callCount++;
	if (!getenv("TRACE_AUDIO")) {
		return;
	}
	int freq1 = apuCh1.nrX3 | ((apuCh1.nrX4 & 0x07) << 8);
	int freq2 = apuCh2.nrX3 | ((apuCh2.nrX4 & 0x07) << 8);
	int freq3 = apuCh3.nrX3 | ((apuCh3.nrX4 & 0x07) << 8);
	fprintf(stderr, "[audio] frame=%d NR52=%02X ch1(en=%d f=%d v=%d dt=%d) ch2(en=%d f=%d v=%d dt=%d) ch3(en=%d f=%d) ch4(en=%d v=%d)\n",
		callCount, NR52,
		apuCh1.enabled, freq1, apuCh1.currentVolume, (apuCh1.nrX1 >> 6) & 3,
		apuCh2.enabled, freq2, apuCh2.currentVolume, (apuCh2.nrX1 >> 6) & 3,
		apuCh3.enabled, freq3,
		apuCh4.enabled, apuCh4.currentVolume);
}

// ---- Real save/load for testing purposes ----
// Plain host files (one per sanitized cart title, matching the same
// naming idea the real psx.c memory-card implementation uses) rather
// than a no-op stub, so the actual save-trigger logic in emu.c (dirty
// flag, RAM-enable-transition save, load-on-boot) can be genuinely
// exercised and verified on the host, not just assumed correct.
static void build_save_path(char *out, const char *title) {
    strcpy(out, "/tmp/agbe_save_");
    int outLen = strlen(out);
    int i;
    for (i = 0; i < 10 && title[i] != '\0'; i++) {
        char c = title[i];
        int isAlnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (isAlnum) {
            out[outLen++] = c;
        }
    }
    strcpy(out + outLen, ".sav");
}

int SaveCartRAM(const char *saveId, unsigned char *buf, int size) {
    char path[64];
    build_save_path(path, saveId);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t written = fwrite(buf, 1, size, f);
    fclose(f);
    if (getenv("TRACE")) fprintf(stderr, "[SaveCartRAM] wrote %s (%d bytes)\n", path, size);
    return (int)written == size;
}

int LoadCartRAM(const char *saveId, unsigned char *buf, int size) {
    char path[64];
    build_save_path(path, saveId);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t got = fread(buf, 1, size, f);
    fclose(f);
    if (getenv("TRACE")) fprintf(stderr, "[LoadCartRAM] read %s (%d bytes)\n", path, size);
    return (int)got == size;
}
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

    // Mirrors the load-on-boot call in emu.c's real runEmu() - this
    // harness has its own instruction-dispatch loop (for bounded/traced
    // test runs) instead of calling runEmu() directly, so it needs its
    // own copy of this bit to genuinely exercise save/load in tests.
    if (CartHasBattery()) {
        LoadCartRAMAndRTC((const char *)CARTTITLE);
        RAM_DIRTY = 0;
    }

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
            if (stuckCount > 1000 && !getenv("NOSTUCK")) {
                fprintf(stderr, "\n[harness] CPU parked at PC=%04X after %ld instructions - test complete.\n", reg_PC, i);
                break;
            }
        } else {
            stuckCount = 0;
        }
        lastPC = reg_PC;

        if (getenv("TRACE") && (i % 500000 == 0)) {
            fprintf(stderr, "[trace] i=%ld PC=%04X SP=%04X A=%02X op=%02X IME=%d IE=%02X IF=%02X LY=%02X dmaActive=%d dmaBytesDone=%d\n",
                    i, reg_PC, reg_SP, reg_A, ReadMEM(reg_PC), IME, IER, IFLAG, LCDY, dmaActive, dmaBytesDone);
        }

        instructions[ReadMEM(reg_PC++)]();

        // Mirrors the EI-delay fix in emu.c's real runEmu() - this
        // harness has its own dispatch loop instead of calling runEmu()
        // directly, so it needs its own copy to genuinely test it.
        if (EI_PENDING > 0) {
            EI_PENDING--;
            if (EI_PENDING == 0) {
                IME = 1;
            }
        }
    }

    if (i >= max_instr) {
        fprintf(stderr, "\n[harness] hit instruction cap (%ld) without the CPU parking - treating as hang/timeout.\n", max_instr);
    }

    printf("\n");
    if (getenv("DUMP_VRAM")) {
        int r, c, i;
        fprintf(stderr, "[VRAM] tile map at $9800, all 32 rows x 20 cols:\n");
        for (r = 0; r < 32; r++) {
            fprintf(stderr, "  ");
            for (c = 0; c < 20; c++) {
                fprintf(stderr, "%02X ", ReadMEM(0x9800 + r*32 + c));
            }
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "[VRAM] tile data for tile #0x20 ('space', 16 bytes at $8000+0x20*16):\n  ");
        for (i = 0; i < 16; i++) {
            fprintf(stderr, "%02X ", ReadMEM(0x8000 + 0x20*16 + i));
        }
        fprintf(stderr, "\n");
        fprintf(stderr, "[VRAM] tile data for tile #0x30 ('0', 16 bytes at $8000+0x30*16):\n  ");
        for (i = 0; i < 16; i++) {
            fprintf(stderr, "%02X ", ReadMEM(0x8000 + 0x30*16 + i));
        }
        fprintf(stderr, "\n");
        fprintf(stderr, "[VRAM] LCDC=%02X BGP=%02X\n", LCDCONTROL, BGPAL);
    }
    if (getenv("DUMP_HRAM")) {
        int i;
        fprintf(stderr, "[Mooneye state] bank_number=%d actual=%d expected=%d lower_upper=$%02X mode=%d\n",
                ReadMEM(0xFF80), ReadMEM(0xFF81), ReadMEM(0xFF82), ReadMEM(0xFF83), ReadMEM(0xFF84));
        fprintf(stderr, "[HRAM] $FF80-$FFFE:\n");
        for (i = 0xFF80; i <= 0xFFFE; i++) {
            fprintf(stderr, "%02X ", ReadMEM(i));
            if ((i - 0xFF7F) % 16 == 0) fprintf(stderr, "\n");
        }
        fprintf(stderr, "\n");
        fprintf(stderr, "[WRAM] $C200+256..287 (expected_banks block3):\n");
        for (i = 0; i < 32; i++) {
            fprintf(stderr, "%d ", ReadMEM(0xC200 + 256 + i));
        }
        fprintf(stderr, "\n");
    }
    if (strstr(serial_log, "Passed")) {
        printf("[harness] RESULT: PASS\n");
        return 0;
    } else if (strstr(serial_log, "Failed")) {
        printf("[harness] RESULT: FAIL\n");
        return 1;
    } else if (reg_B == 3 && reg_C == 5 && reg_D == 8 && reg_E == 13 && get_rH() == 21 && get_rL() == 34) {
        // Mooneye test suite convention: on success, parks in an infinite
        // self-loop (caught by the stuck-loop detector above) with
        // B,C,D,E,H,L set to this exact Fibonacci sequence beforehand.
        printf("[harness] RESULT: PASS (Mooneye fibonacci signature)\n");
        return 0;
    } else if (reg_B == 0x42 && reg_C == 0x42 && reg_D == 0x42 && reg_E == 0x42 && get_rH() == 0x42 && get_rL() == 0x42) {
        // Mooneye failure convention: same parked self-loop, but
        // B=C=D=E=H=L=$42 instead of the success Fibonacci sequence.
        printf("[harness] RESULT: FAIL (Mooneye $42 signature)\n");
        return 1;
    } else {
        printf("[harness] RESULT: UNKNOWN (no Blargg-style Passed/Failed banner seen)\n");
        return 3;
    }
}
