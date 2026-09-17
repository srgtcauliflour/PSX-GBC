# PSX-GBC

A Game Boy & Game Boy Color emulator for the original PlayStation (PSX/PS1).

This project takes **aGBe**, an early-2000s Game Boy/Game Boy Color emulator
originally written for PS1 homebrew, and rebuilds it from an unbuildable CVS
dump into a working, tested emulator using the free, modern
[PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK) toolchain in place of the
original proprietary Psy-Q SDK.

`git log` has the full history, with a detailed rationale written into every
commit message — what was found, how it was diagnosed, how it was fixed, and
how it was verified. `STATUS.md` is the living project status doc: current
test results, what's implemented, what's known to still be broken or
missing, and suggested next steps.

## Status

- Full CPU core (LR35902/SM83) instruction set, verified against Blargg's
  `cpu_instrs` suite (11/11) and Gekkio's Mooneye test suite for MBC1/MBC5
  cartridge behavior (18/21 — the remaining 3 are either a real-hardware
  MBC1 multicart-wiring quirk beyond standard MBC1, confirmed to also fail
  against an independent reference core, or an out-of-scope cart type).
- PPU (background, window, sprites, including 8x8 and 8x16 sprite modes),
  MBC1/MBC2/MBC3/MBC5 cartridge mappers with battery-backed save RAM and
  MBC3 real-time-clock persistence, and a real PSn00bSDK platform layer:
  double-buffered rendering, CD-ROM multi-game loading with an on-screen
  ROM-select menu, and PS1 memory card save/load.
- **Game Boy Color support**: banked VRAM/WRAM, CGB palettes, per-tile and
  per-sprite color attributes, double-speed CPU mode, and HDMA/GDMA VRAM DMA.
- Verified against real commercial ROMs in addition to synthetic test
  suites — see `STATUS.md` for the specific titles and what each one
  confirmed or uncovered.
- Known gaps: sound is not implemented; a couple of specific real-hardware
  edge cases are tracked as open issues in `STATUS.md` rather than papered
  over.

## Repo layout

- `psx-gbc/` — the canonical, fixed source (this is the modernized,
  renamed continuation of the original **aGBe** codebase - see
  Acknowledgments below), including the real PSn00bSDK platform layer
  (`psx.c`/`psx.h`/`main.c`/`gui.c`). This is what actually builds and
  runs on real PS1 hardware.
- `test-harness/` — a host-native (Linux gcc) build of the *unmodified*
  CPU/MBC/PPU core (`emu.c` + `opcodes.c`) against stub PSX SDK headers, so
  it can be run against real Game Boy test ROMs without needing the PS1
  toolchain at all. Also contains `refharness.c`, a second, independent core
  ([Peanut-GB](https://github.com/deltabeard/Peanut-GB), MIT-licensed)
  wired up to print identical per-instruction traces, used throughout this
  project's history to pinpoint exact divergences by diffing traces
  instruction-by-instruction.
- `psn00bsdk-build/` — the real CMake project that builds `psx-gbc/`'s
  canonical source directly into a bootable PS-EXE (`PSXGBC.EXE`) and CD
  image via the real PSn00bSDK toolchain.
- `psn00bsdk-smoketest/` — an early proof-of-concept, superseded by
  `psn00bsdk-build/`. Kept for reference only.
- `STATUS.md` — the detailed, living project status document.

## Building

### Host-native testing (no PS1 toolchain needed)

```sh
cd test-harness
gcc -w -fcommon -I psx-stubs -I ../psx-gbc ../psx-gbc/emu.c ../psx-gbc/opcodes.c harness.c -o harness
./harness /path/to/test.gb 30000000         # run a ROM, see Blargg-style pass/fail
DUMP_PPM=out.ppm ./harness /path/to/test.gb 30000000   # dump the rendered screen as an image
```

See `STATUS.md` for the full list of test-harness debugging environment
variables (`TRACE`, `ROWSUMMARY`, `DUMP_VRAM`, `NOSTUCK`, and more).

### Real PS1 build

Requires the [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK) toolchain
(v0.24 or compatible) installed and on your `PATH`. See `STATUS.md` for the
exact setup commands used to build this toolchain from scratch, if you don't
already have it.

```sh
export PATH="/path/to/psn00bsdk/toolchain/bin:$PATH"
export PSN00BSDK_LIBS="/path/to/PSn00bSDK-0.24-Linux/lib/libpsn00b"
cd psn00bsdk-build
cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE="$PSN00BSDK_LIBS/cmake/sdk.cmake" \
  -DPSN00BSDK_TC="" -DPSN00BSDK_TARGET="mipsel-none-elf"
cmake --build build
# -> build/psxgbc.exe is a real, bootable PS-EXE

# Build a bootable CD image (see psn00bsdk-build/iso.xml for disc layout):
/path/to/PSn00bSDK-0.24-Linux/bin/mkpsxiso -y psn00bsdk-build/iso.xml
```

## Testing methodology

This project leans heavily on cross-referencing against an independent
reference emulator core (Peanut-GB) and authoritative test suites (Blargg's
`cpu_instrs`, Gekkio's Mooneye test suite) rather than trusting visual
inspection alone. Several of the most severe bugs found and fixed across
this project's history were only found by diffing full instruction-level
CPU traces between this core and the reference core down to the exact
instruction of first divergence — a technique documented and reused
throughout the commit history. Real commercial ROMs were also used
throughout to catch issues no synthetic test suite covers.

## Acknowledgments

- Built on **aGBe**, the original early-2000s PS1 homebrew Game Boy
  emulator this project modernizes.
- [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK) — the free,
  modern PS1 SDK this project builds against.
- [Peanut-GB](https://github.com/deltabeard/Peanut-GB) (MIT) — used as an
  independent reference core for correctness verification.
- [Blargg's Game Boy test ROMs](https://github.com/retrio/gb-test-roms)
  and [Mooneye's test suite](https://github.com/Gekkio/mooneye-test-suite)
  — the authoritative correctness test suites used throughout.
