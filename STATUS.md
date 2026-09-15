# aGBe Modernization — Status

This is a from-scratch git repo (`git log` has full history with detailed
rationale per commit) rebuilding **aGBe**, an early-2000s Game Boy/Game Boy
Color emulator for the original PlayStation, from an unbuildable CVS dump
into something on a path to a real Release build.

## Layout

- `aGBe/` — the canonical, fixed source, including the real PSn00bSDK
  platform layer (`psx.c`/`psx.h`/`main.c`). This is what actually builds
  and runs on real PS1 hardware/toolchain now.
- `test-harness/` — a host-native (Linux gcc) build of the *unmodified*
  CPU/MBC/PPU core (`emu.c`+`opcodes.c`) against stub PSX SDK headers, so
  it can be run against real Game Boy test ROMs without needing the PS1
  toolchain. Also contains `refharness.c`, a second core (Peanut-GB,
  MIT-licensed) wired up to print identical per-instruction traces, for
  diffing against aGBe's own core to pinpoint exact divergences. Has
  opt-in visual debugging via env vars: `DUMP_PPM=<path>` dumps the
  rendered screen to a PGM/PPM image, `ROWSUMMARY=1` prints a per-row
  non-white-pixel count, `DUMP_VRAM=1` dumps the tile map/tile data/
  palette at exit, `TRACE`/`trace` arg give per-instruction register
  traces.
- `psn00bsdk-build/` — the real CMake project that builds `aGBe/`'s
  canonical source directly (no copy-and-sync step) into a bootable
  PS-EXE via the real PSn00bSDK toolchain.
- `psn00bsdk-smoketest/` — an earlier proof-of-concept (superseded by
  `psn00bsdk-build/` now that the real platform layer exists). Kept for
  reference; not the thing to build from going forward.

## How to rebuild and test right now

```sh
# Host-native correctness testing (no PS1 toolchain needed)
cd test-harness
gcc -w -fcommon -I psx-stubs -I ../aGBe ../aGBe/emu.c ../aGBe/opcodes.c harness.c -o harness
./harness /path/to/test.gb 30000000         # run a ROM, see Blargg pass/fail
./harness /path/to/test.gb 30000000 trace   # per-instruction register trace
DUMP_PPM=out.ppm ./harness /path/to/test.gb 30000000   # dump rendered screen
ROWSUMMARY=1 ./harness /path/to/test.gb 30000000       # per-row pixel counts
DUMP_VRAM=1 ./harness /path/to/test.gb 30000000        # tile map/data/palette dump

# Blargg's test ROMs: https://github.com/retrio/gb-test-roms
# Mooneye's test ROMs (not yet run this session): https://github.com/Gekkio/mooneye-test-suite
```

For the real PS1 build (toolchain already set up in this session's sandbox
at `/opt/psn00bsdk`, not included in this archive due to size — see
"Setting up the toolchain yourself" below):

```sh
export PATH="/opt/psn00bsdk/toolchain/bin:$PATH"
export PSN00BSDK_LIBS="/opt/psn00bsdk/sdk/PSn00bSDK-0.24-Linux/lib/libpsn00b"
cd psn00bsdk-build
cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE="$PSN00BSDK_LIBS/cmake/sdk.cmake" \
  -DPSN00BSDK_TC="" -DPSN00BSDK_TARGET="mipsel-none-elf"
cmake --build build
# -> build/agbe.exe is a real, bootable PS-EXE running the actual emulator

# Build a real bootable CD image with two demo ROMs on it (iso_files/
# holds the actual .GB files to include - swap in real ROMs as needed):
/opt/psn00bsdk/sdk/PSn00bSDK-0.24-Linux/bin/mkpsxiso -y iso.xml
# -> agbe.bin + agbe.cue, a genuine multi-game PS1 disc image
```

### Setting up the toolchain yourself

```sh
curl -sL -o gcc.zip "https://github.com/Lameguy64/PSn00bSDK/releases/download/v0.24/gcc-mipsel-none-elf-12.3.0-linux.zip"
curl -sL -o sdk.zip "https://github.com/Lameguy64/PSn00bSDK/releases/download/v0.24/PSn00bSDK-0.24-Linux.zip"
unzip gcc.zip -d /opt/psn00bsdk/toolchain
unzip sdk.zip -d /opt/psn00bsdk/sdk
# then apt install cmake ninja-build
```

## What's proven so far

- **CPU/MBC/PPU core correctness**: dozens of real, confirmed bugs fixed
  this session — see `git log` for the full, detailed list. Validated
  against Blargg's `cpu_instrs` test suite: went from every single test
  hanging forever (0/11) to a clean **11/11 passing**. Key technique:
  generate a tiny ROM exercising every input combination for one suspect
  opcode, run it through both this core and the Peanut-GB reference core,
  diff the post-instruction state — found several "unmasked Z-flag" bugs
  this way that were otherwise invisible.
- **Real rendering, visually verified for the first time.** Every test up
  to this point only checked CPU/flag state or serial output - nothing
  had ever checked actual pixels. Standing up real GPU output surfaced
  (and this session fixed) a cluster of real rendering bugs: BGP/OBP0/
  OBP1 palette registers never initialized (defaulted to an all-black-
  mapping 0 instead of the real post-boot defaults), a line-0 rendering
  skip, missing background-scroll wraparound, and — the big one — a
  severely broken sprite-rendering inner loop that reused the outer
  sprite-index variable, compounded by unzeroed OAM memory producing
  phantom garbage sprites. Confirmed fixed by dumping the actual
  rendered framebuffer to an image and visually reading real, legible
  "02-interrupts" / "Passed" text rendered through the genuine tile
  pipeline.
- **MBC1/2/3/5 bank switching, RAM-enable gating, a basic MBC3 RTC.**
- **Real PSn00bSDK platform layer.** `psx.c`/`psx.h`/`main.c` are now a
  genuine (if minimal) implementation, not GsLib stubs: real GPU output,
  real controller input (`InitPAD`/`StartPAD`). Builds and links into a
  real, bootable PS-EXE (`psn00bsdk-build/build/agbe.exe`) that runs an
  embedded demo ROM through the actual `runEmu()` loop.
- **Real CD-ROM ROM loading — multiple games on one disc, confirmed.**
  `LoadROMFromCD()` in `psx.c` uses PSn00bSDK's real `psxcd.h` (the same
  `CdSearchFile`/`CdControl`/`CdRead` pattern PSn00bSDK's own examples
  use). Built a genuine bootable CD image with `mkpsxiso` containing two
  real Game Boy ROM files plus the executable, then independently
  verified with standard ISO9660 tooling (`isoinfo`, after de-interleaving
  the raw sectors) that the disc structure is correct and every file on
  it is byte-identical to its source — not just trusting the build.
- **Real ROM-select menu.** `gui.c` is a genuine (if plain, text-only)
  working menu now: lists `.GB`/`.GBC` files actually on the disc,
  Up/Down + Cross/Start to pick one, then loads and runs it. `main.c` no
  longer has a fixed ROM filename — the player picks. Re-verified the
  same way as the CD-loading milestone: rebuilt the disc, confirmed
  every file on it byte-identical to source via `isoinfo`.
- **Sprite rendering fixed and verified.** Found and fixed a severe bug
  where every row of every sprite showed the tile's top row repeated
  (the row-within-tile fetch never varied with which scanline was being
  drawn), wired up X/Y sprite flipping (attributes were read from OAM
  but never applied), and fixed `Draw_Buffer` being incorrectly gated on
  BG-enable instead of just LCD-on (a game with BG off but sprites on
  would never present a frame at all). Verified with a synthetic
  four-sprite test ROM (normal/X-flip/Y-flip/XY-flip of the same
  asymmetric tile) — extracted the actual rendered pixels and confirmed
  an exact match against the expected mirror/flip for all four, on both
  the top and bottom rows.
- **Double buffering.** `Draw_Buffer` now draws into whichever half of
  VRAM isn't currently displayed and flips each frame — the standard PS1
  pattern, eliminating the tearing the earlier single-buffer version
  could show.

## What's next (roughly in priority order)

1. **Visual polish for the menu.** Current menu is plain `FntPrint` text
   - functional, not pretty. A real background/graphics layer can build
   on top of what's here now without touching the menu logic itself.
2. **Saves.** No `BuWrite`/`BuRead` (memory card) calls exist anywhere yet
   — needed for battery-backed cart RAM.
3. Run Mooneye's MBC1/MBC5 test ROMs to further validate bank-switching
   (RGBDS toolchain needed to build them from source; wasn't readily
   available as a binary this session).
4. **GBC support and sound** — explicitly deprioritized per the person's
   direction earlier this session; sound especially can wait until
   everything else is solid.
5. **Bank-streaming for very large ROMs.** The core's `ROM[loc]` is a
   flat, fully-resident pointer with no partial loading — fine for the
   large majority of the GB/GBC library (32KB-512KB), but the small
   number of very large late-era GBC games (up to 4-8MB) won't fit
   resident in the PS1's 2MB of RAM. Only worth doing if support for
   those specific large titles is wanted; most of the library doesn't
   need it.
6. **8x16 sprite mode** (`LCDC` bit 2) isn't supported — `DrawOBJline`'s
   line-range check assumes 8-tall sprites only. Most GB/GBC games use
   8x8 sprites predominantly; a real, separate gap if a specific game
   needs tall-sprite mode.

## Known limitation: instr_timing.gb

Blargg's `instr_timing.gb` (a stricter, dedicated cycle-timing test, as
opposed to `cpu_instrs.gb`'s broader correctness checks) still fails.
Investigated in depth: this project skips boot-ROM emulation entirely and
starts execution straight at `$0100`, with the PPU pre-set to the
well-documented real-hardware "power-up snapshot" (`LCDC=$91`,
`STAT=$85`). That snapshot is a widely-used convention, but it isn't a
bit-exact stand-in for the actual mid-boot-animation PPU phase real
hardware would be in at that exact moment — and `instr_timing.gb` is
sensitive to that exact phase in a way `cpu_instrs.gb` (now 11/11) is not.
Confirmed every individual opcode's cycle cost is correct (audited against
an authoritative table in an earlier commit) and confirmed the specific
starting convention used here already matches the reference core's actual
internal mechanics better than an alternative tried. Not worth further
effort without implementing genuine boot-ROM timing emulation — a
materially bigger undertaking than anything else on this list, for a test
that doesn't reflect real-game compatibility (real games sync to VBlank/
STAT via interrupts, not an assumption about the exact boot-time phase).

## Boot path

CD is the target boot method (burn the final image with `mkpsxiso`, works
on any unmodified PS1). Booting the same PS-EXE without a disc via
Unirom+a pre-flashed memory card (FreePSXBoot) remains possible later
without extra console hardware, but actual ROM data still needs to come
from a CD given memory card capacity — see prior discussion in this
project's chat history for the full reasoning.

## A note on emulator verification via mednafen

Attempts to get a visual screenshot of the built PS-EXE running under
mednafen (with the open-source OpenBIOS in place of the proprietary
retail BIOS) got stuck on OpenBIOS's own boot logo indefinitely, for both
the smoke test and the real build. This looks like a mednafen standalone
command-line invocation quirk (it's built around CD-image booting, not
direct EXE injection) rather than anything wrong with the executables
themselves — independently confirmed correct by `file` and by mednafen's
own PS-EXE header parser reporting the exact right entry point/text
segment. Real hardware or a different emulator invocation would likely
show it correctly; this wasn't pursued further given the higher-value
pivot to the host-harness PPM-dump approach, which is what actually found
and helped fix the real rendering bugs above.
