# aGBe Modernization — Status

This is a from-scratch git repo (`git log` has full history with detailed
rationale per commit) rebuilding **aGBe**, an early-2000s Game Boy/Game Boy
Color emulator for the original PlayStation, from an unbuildable CVS dump
into something on a path to a real Release build.

## Layout

- `aGBe/` — the canonical, fixed source. This is what a real platform port
  builds from.
- `test-harness/` — a host-native (Linux gcc) build of the *unmodified*
  CPU/MBC core (`emu.c`+`opcodes.c`) against stub PSX SDK headers, so it can
  be run against real Game Boy test ROMs without needing the PS1 toolchain.
  Also contains `refharness.c`, a second core (Peanut-GB, MIT-licensed)
  wired up to print identical per-instruction traces, for diffing against
  aGBe's own core to pinpoint exact divergences.
- `psn00bsdk-smoketest/` — a real PSn00bSDK CMake project proving the core
  compiles and links into a genuine bootable PS-EXE. Not the real
  front-end — see "What's next" below.

## How to rebuild and test right now

```sh
# Host-native correctness testing (no PS1 toolchain needed)
cd test-harness
gcc -w -fcommon -I psx-stubs -I ../aGBe emu.c opcodes.c harness.c -o harness
./harness /path/to/test.gb 30000000        # run a ROM, see Blargg pass/fail
./harness /path/to/test.gb 30000000 trace  # per-instruction register trace

# Blargg's test ROMs: https://github.com/retrio/gb-test-roms
# Mooneye's test ROMs (not yet run this session): https://github.com/Gekkio/mooneye-test-suite
```

For the real PS1 toolchain (already set up in this session's sandbox at
`/opt/psn00bsdk`, not included in this archive due to size — see
"Setting up the toolchain yourself" below):

```sh
export PATH="/opt/psn00bsdk/toolchain/bin:$PATH"
export PSN00BSDK_LIBS="/opt/psn00bsdk/sdk/PSn00bSDK-0.24-Linux/lib/libpsn00b"
cd psn00bsdk-smoketest
cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE="$PSN00BSDK_LIBS/cmake/sdk.cmake" \
  -DPSN00BSDK_TC="" -DPSN00BSDK_TARGET="mipsel-none-elf"
cmake --build build
# -> build/smoketest.exe is a real PS-EXE
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

- **CPU/MBC/PPU core correctness** (`aGBe/emu.c`, `aGBe/opcodes.c`): dozens
  of real, confirmed bugs fixed this session — see `git log` for the full,
  detailed list. Validated against Blargg's `cpu_instrs` test suite: went
  from every single test hanging forever (0/11), to 6/11, to a clean
  **11/11 passing**. The methodology that got the last 5 tests across the
  line: generate a tiny ROM that exercises every input combination for one
  suspect opcode, run it through both this core and a reference core
  (Peanut-GB, MIT-licensed) via `test-harness/refharness.c`, diff the
  post-instruction state. Found several "unmasked Z-flag" bugs this way
  (e.g. `0x00 - 0xFF - 1` wraps to a real zero result that an unmasked
  `a == 0` check misses) that were otherwise invisible.
  `instr_timing.gb` (the dedicated cycle-timing test, stricter than
  cpu_instrs) still fails at one remaining point — see "What's next".
- **MBC1/2/3/5 bank switching, RAM-enable gating, a basic MBC3 RTC.**
- **Toolchain viability**: the core compiles and links into a genuine
  bootable PS-EXE with the real, free PSn00bSDK toolchain (see
  `agbe-boot-screenshot.png` and `agbe-smoketest.psexe` alongside this
  archive — confirmed booting in mednafen with the open-source OpenBIOS).

## What's next (roughly in priority order)

1. Run Mooneye's MBC1/MBC5 test ROMs to validate the bank-switching work.
2. **Real platform-layer port.** `psx.c`/`gui.c`/`main.c` still assume
   Psy-Q's GsLib (`GsSPRITE`, `GsOT`, `GsSortSprite`, ...), which PSn00bSDK
   has no equivalent for. This needs a genuine rewrite against raw
   `psxgpu.h` primitives (ordering tables, `POLY_FT4`/`SPRT` primitives).
   `core_state.h` in the smoke test is a preview of the necessary split
   between "core state" and "GsLib rendering state."
3. **Real controller input.** `PadRead()` is a stub. Port to PSn00bSDK's
   `psxpad.h` buffer-polling model (different shape than Psy-Q's simple
   polled `PadRead()`).
4. **CD-ROM ROM loading.** The original `AGBEBANK.BIN` multi-ROM bundle
   format's packing tool was never committed to CVS — it needs to be
   rebuilt (a small Python script is enough) alongside switching the raw
   `CdRead()` sector calls to PSn00bSDK's `psxcd.h` + `mkpsxiso` for the
   actual bootable CD image.
5. **Saves.** No `BuWrite`/`BuRead` (memory card) calls exist anywhere yet
   — needed for battery-backed cart RAM.
6. **GBC support and sound** — explicitly deprioritized per the person's
   direction this session; sound especially can wait until everything else
   is solid.

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
internal mechanics better than the alternative tried. Not worth further
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
