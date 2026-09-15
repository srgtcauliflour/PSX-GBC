// Reference-core instruction tracer, built on Peanut-GB (known-correct,
// widely used portable GB core). Prints one line per instruction in the
// same format as the aGBe harness's TRACE2 mode, so the two can be diffed
// to find the exact point where aGBe's CPU emulation first disagrees with
// a known-correct implementation.
//
// Usage: ./refharness <rom.gb> <num_instructions>

#include <stdio.h>
#include <stdlib.h>
#define ENABLE_LCD 0
#define ENABLE_SOUND 0
#include "peanut_gb.h"

static uint8_t *romdata;
static long romsize;
static uint8_t ram[65536];

uint8_t rb(struct gb_s *gb, const uint_fast32_t addr) {
    (void)gb;
    if (addr < (uint_fast32_t)romsize) return romdata[addr];
    return 0xFF;
}
uint8_t cram_r(struct gb_s *gb, const uint_fast32_t addr) { (void)gb; return ram[addr & 0xFFFF]; }
void cram_w(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) { (void)gb; ram[addr & 0xFFFF] = val; }
void gb_err(struct gb_s *gb, const enum gb_error_e e, const uint16_t val) {
    (void)gb; fprintf(stderr, "[ref] gb_error %d at %04X\n", e, val); 
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s rom.gb num_instr\n", argv[0]); return 2; }
    long maxi = atol(argv[2]);

    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END); romsize = ftell(f); fseek(f, 0, SEEK_SET);
    romdata = malloc(romsize);
    fread(romdata, 1, romsize, f);
    fclose(f);

    struct gb_s gb;
    enum gb_init_error_e ret = gb_init(&gb, rb, cram_r, cram_w, gb_err, NULL);
    if (ret != GB_INIT_NO_ERROR) { fprintf(stderr, "gb_init failed: %d\n", ret); return 2; }

    for (long i = 0; i < maxi; i++) {
        printf("i=%ld PC=%04X SP=%04X A=%02X F=%02X B=%02X C=%02X D=%02X E=%02X H=%02X L=%02X op=%02X\n",
               i, gb.cpu_reg.pc.reg, gb.cpu_reg.sp.reg, gb.cpu_reg.a, gb.cpu_reg.f.reg,
               gb.cpu_reg.bc.bytes.b, gb.cpu_reg.bc.bytes.c, gb.cpu_reg.de.bytes.d, gb.cpu_reg.de.bytes.e,
               gb.cpu_reg.hl.bytes.h, gb.cpu_reg.hl.bytes.l, rb(&gb, gb.cpu_reg.pc.reg));
        __gb_step_cpu(&gb);
    }
    return 0;
}
