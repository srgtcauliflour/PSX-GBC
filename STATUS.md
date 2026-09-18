# PSX-GBC (aGBe Modernization) — Status

This is a from-scratch git repo (`git log` has full history with detailed
rationale per commit) rebuilding **aGBe**, an early-2000s Game Boy/Game Boy
Color emulator for the original PlayStation, from an unbuildable CVS dump
into something on a path to a real Release build. The project now goes by
the name **PSX-GBC**; `aGBe` remains the name of the original codebase this
project is built on and modernizes, referenced throughout this document and
the commit history.

## For the next agent — start here

Read this section first; it's a short, current pointer into the much
longer, historical rest of this document (which stays as a detailed,
append-only log of what was tried, found, and fixed - useful for context,
not required reading to get started).

**Set up once, in order** (a fresh sandbox has none of this):
1. PSn00bSDK v0.24 toolchain - see "Setting up the toolchain yourself"
   under "How to rebuild and test right now" below.
2. Mooneye's authoritative GB test suite + the WLA-DX assembler needed to
   build it from source - see "Setting up Mooneye's test suite" in that
   same section. This is real, load-bearing infrastructure for the #1
   priority item below, and doesn't exist anywhere in this repo itself
   (it's cloned from its own upstream repos into the sandbox) - do this
   before touching sub-instruction timing work.
3. Real commercial ROMs, if you want to extend real-ROM testing (see
   "What's next" item 3 below) - **never commit ROM files to this repo or
   bundle them in any output archive** (copyright); keep them local only.

**Current state in brief** (see "What's proven so far" for the full,
evidenced version of all of this):
- CPU core: solid, `cpu_instrs` 11/11.
- MBC1/2/3/5 + save RAM + RTC: solid, Mooneye `mbc1`+`mbc5` 18/21 (the 3
  failures are a confirmed real-hardware multicart quirk / an
  out-of-scope cart type, not bugs here).
- DMG and GBC/CGB rendering: solid, verified against synthetic tests and
  several real commercial ROMs.
- Sound: the APU core itself is solid and independently verified (see
  `DUMP_WAV` below); the PS1 SPU output half has never been confirmed to
  actually produce audio, since this sandbox can't play or capture sound.
- Sub-instruction cycle-accurate timing: a large, real, **partially
  fixed** gap - currently 29/67 on Mooneye's `acceptance/ppu`+`timer`+
  `interrupts`+top-level suite (started at 11/67). See "What's next"
  item 1 for exactly what's fixed, what's failing, and why.
- Kirby's Pinball Land's long-standing blank-screen hang: resolved as
  a side effect of this suite's HALT-timing fix - see "What's next"
  item 2.

**Priority order to actually work from is "What's next" below, kept
current** - don't re-derive priorities from the historical log above it.

**Before committing anything**, run the exact regression sweep this
project has learned (twice, the hard way - see "What's next" item 1's
own history) is necessary: `cpu_instrs` full suite, Mooneye
`emulator-only/mbc1`+`mbc5`, the **full** `acceptance/ppu`+`timer`+
`interrupts`+top-level suite (diff the exact pass list, not just the
total - a change can silently swap one pass for another), and re-render
at least 2-3 real ROMs to confirm they still look correct. `cpu_instrs`
and the MBC suites alone have **not** been sufficient to catch every
real regression found this session.

## Layout

- `psx-gbc/` — the canonical, fixed source (renamed from the original
  `aGBe/` codebase folder - "aGBe" is still the name of the codebase this
  project modernizes, see the header note above), including the real
  PSn00bSDK platform layer (`psx.c`/`psx.h`/`main.c`). This is what
  actually builds and runs on real PS1 hardware/toolchain now.
- `test-harness/` — a host-native (Linux gcc) build of the *unmodified*
  CPU/MBC/PPU core (`emu.c`+`opcodes.c`) against stub PSX SDK headers, so
  it can be run against real Game Boy test ROMs without needing the PS1
  toolchain. Also contains `refharness.c`, a second core (Peanut-GB,
  MIT-licensed) wired up to print identical per-instruction traces, for
  diffing against this project's own core to pinpoint exact divergences.
  Has opt-in visual debugging via env vars: `DUMP_PPM=<path>` dumps the
  rendered screen to a PGM/PPM image, `ROWSUMMARY=1` prints a per-row
  non-white-pixel count, `DUMP_VRAM=1` dumps the tile map/tile data/
  palette at exit, `TRACE`/`trace` arg give per-instruction register
  traces.
- `psn00bsdk-build/` — the real CMake project that builds `psx-gbc/`'s
  canonical source directly (no copy-and-sync step) into a bootable
  PS-EXE (`PSXGBC.EXE`) via the real PSn00bSDK toolchain.
- `psn00bsdk-smoketest/` — an earlier proof-of-concept (superseded by
  `psn00bsdk-build/` now that the real platform layer exists). Kept for
  reference; not the thing to build from going forward.

## How to rebuild and test right now

```sh
# Host-native correctness testing (no PS1 toolchain needed)
cd test-harness
gcc -w -fcommon -I psx-stubs -I ../psx-gbc ../psx-gbc/emu.c ../psx-gbc/opcodes.c harness.c -o harness
./harness /path/to/test.gb 30000000         # run a ROM, see Blargg/Mooneye pass/fail
```

Full harness environment-variable/argument reference (all opt-in, all
combinable):

```sh
./harness rom.gb 3000000 trace     # per-instruction register trace (3rd positional arg, not an env var)
TRACE=1 ./harness rom.gb 3000000               # coarser trace + Draw_Buffer call count, every 500k instructions
DUMP_PPM=out.ppm ./harness rom.gb 30000000     # dump the rendered screen as a PGM/PPM image
DUMP_PPM=out.ppm DUMP_AT=5 ./harness rom.gb 30000000  # dump only the 5th Draw_Buffer call, not the last
DUMP_WAV=out.wav ./harness rom.gb 30000000     # render the APU core's own output to a real, listenable WAV file
TRACE_AUDIO=1 ./harness rom.gb 3000000         # per-frame channel freq/volume/enabled dump
ROWSUMMARY=1 ./harness rom.gb 30000000         # per-row non-white pixel counts
DUMP_VRAM=1 ./harness rom.gb 30000000          # tile map/tile data/palette dump at exit
DUMP_HRAM=1 ./harness rom.gb 60000000          # Mooneye pass/fail HRAM diagnostic fields
NOSTUCK=1 ./harness rom.gb 30000000            # disable the stuck-loop early-exit (needed for infinite-loop test ROMs with no HALT)
```

Blargg's test ROMs (already used throughout this project, not included in
this repo - copyright): `https://github.com/retrio/gb-test-roms`

### Setting up Mooneye's test suite (not in this repo - clone + build yourself)

Load-bearing for the #1 priority item below. Needs WLA-DX (an assembler)
built from source; Mooneye ships its own test ROM sources, assembled via
its own Makefile.

```sh
# WLA-DX (assembler) - build once
git clone https://github.com/vhelin/wla-dx.git
cd wla-dx && cmake -B build && cmake --build build
export PATH="$(pwd)/build/binaries:$PATH"   # wla-gb, wlalink now on PATH
cd ..

# Mooneye's test suite - clone once, build whichever categories you need
git clone https://github.com/Gekkio/mooneye-test-suite.git
cd mooneye-test-suite

# Already-covered categories this session (mbc1/mbc5 cartridge behavior):
make WLA=wla-gb WLALINK=wlalink build/emulator-only/mbc1/bits_bank1.gb
# ...or loop over every .s file in a directory to build a whole category:
for f in emulator-only/mbc1/*.s emulator-only/mbc5/*.s; do
  make WLA=wla-gb WLALINK=wlalink "build/${f%.s}.gb"
done

# The #1 priority item's own test categories (sub-instruction timing):
for f in acceptance/ppu/*.s acceptance/timer/*.s acceptance/interrupts/*.s acceptance/*.s; do
  make WLA=wla-gb WLALINK=wlalink "build/${f%.s}.gb"
done
# Resulting .gb files land in build/<same path as the .s source>, e.g.
# build/acceptance/timer/tima_reload.gb - run them through the harness
# exactly like any other test ROM.
```

Expect several `acceptance/*` failures that are boot-ROM-dependent
(`boot_div*`/`boot_regs*`/`boot_hwio*`) - this project deliberately skips
boot ROM emulation (see "Boot path" below), so those are not real gaps.

### Real PS1 build

Toolchain already set up in this session's sandbox at `/opt/psn00bsdk`,
not included in this archive due to size — see "Setting up the toolchain
yourself" below if starting fresh.

```sh
export PATH="/opt/psn00bsdk/toolchain/bin:$PATH"
export PSN00BSDK_LIBS="/opt/psn00bsdk/sdk/PSn00bSDK-0.24-Linux/lib/libpsn00b"
cd psn00bsdk-build
cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE="$PSN00BSDK_LIBS/cmake/sdk.cmake" \
  -DPSN00BSDK_TC="" -DPSN00BSDK_TARGET="mipsel-none-elf"
cmake --build build
# -> build/psxgbc.exe is a real, bootable PS-EXE running the actual emulator

# Build a real bootable CD image with two demo ROMs on it (iso_files/
# holds the actual .GB files to include - swap in real ROMs as needed):
/opt/psn00bsdk/sdk/PSn00bSDK-0.24-Linux/bin/mkpsxiso -y iso.xml
# -> psx-gbc.bin + psx-gbc.cue, a genuine multi-game PS1 disc image
```

A stock, unmodified PS1 will not boot a plain burnt copy of this disc
image at all (the BIOS checks for a physical authenticity signature no
CD-R can reproduce) - real hardware testing needs a modchip, an optical
drive emulator, or a memory-card-based loader like UniROM (installable
without opening the console via the FreePSXBoot exploit). See the
project's own chat history for a fuller rundown if this comes up.

#### Setting up the toolchain yourself

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
  real, bootable PS-EXE (`psn00bsdk-build/build/psxgbc.exe`) that runs an
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
- **Memory card save support — verified end-to-end.** Battery-backed
  cart RAM (MBC1/2/3/5) now persists via real PSn00bSDK memory card I/O
  (`_bu_init`, `open`/`read`/`write` against `bu00:`). Saves trigger on
  the RAM-enable→disable transition (mirroring how real cartridges
  themselves commit), load automatically at boot, and are named from
  each cartridge's own header title so multiple games on one disc get
  separate saves. Proved the full cycle for real: wrote a distinctive
  byte pattern from a synthetic ROM, confirmed the saved file's exact
  size and content, then in a completely separate process/ROM run,
  confirmed the same byte came back correctly on load.
- **Visual polish for the ROM select menu.** No longer plain text on
  black — a real title bar, a bordered menu panel, and a selection-
  highlight bar that tracks the cursor, all drawn as GPU primitives
  underneath the existing `FntPrint` text. `Draw_Buffer`'s double-buffer
  logic was factored into reusable `BeginFrame`/`PresentFrame` calls so
  the menu shares the same VRAM buffers as the emulator's own screen.
- **Real commercial-game validation — and a severe bug found because of
  it.** The user supplied two real, commercial ROMs (Pokemon Red and
  Pokemon Yellow, not included in this repo/archive for copyright
  reasons — ask the person for copies again if you need to re-run this
  testing) for local testing. Pokemon Red hung completely under a
  second into booting; traced it to the dedicated VBlank interrupt (IF
  bit 0) being incorrectly gated on a STAT register bit that most real
  games never set, meaning it could essentially never fire — arguably
  the single most common interrupt-wait pattern in the entire GB/GBC
  library was completely broken, and no synthetic/Blargg test this
  project had run happened to exercise it. Fixed, and confirmed by
  actually watching Pokemon Red and Pokemon Yellow (a different cart
  type — MBC3 vs. MBC5 — and Yellow is CGB-flagged too) both boot all
  the way to their own correct, recognizable title screens. Almost
  certainly the highest real-world-compatibility-impact fix of the
  entire session. Also bumped `MAX_ROM_SIZE` from 512KB to 1.5MB in the
  same pass — both Pokemon ROMs are exactly 1MB and would have been
  rejected outright by the old cap before ever getting a chance to hit
  the bug above.
- **Mooneye's authoritative MBC test suite — a step up in rigor from
  Blargg's.** Set up WLA-DX (built from source) and RGBDS (prebuilt) to
  assemble Mooneye's emulator-only/mbc1 and emulator-only/mbc5 test
  ROMs. Found and fixed three more real, severe bugs:
    1. Cart-RAM and ROM bank numbers were both masked only against
       their protocol-level bit width, never against how many banks a
       specific cartridge actually has, so an in-range-per-protocol but
       physically-nonexistent bank number read/wrote straight past the
       end of the real buffer — confirmed to cause actual heap
       corruption and segfaults in the host test harness. Fixed by
       masking both against the cartridge's actual reported size (and
       fixed a related gap: the ROM-size lookup table stopped at 2MB,
       leaving larger carts with a bank count of zero, which turned the
       new masking fix into a division-by-zero until completed).
    2. MBC1/MBC2/MBC3's "bank 0 becomes bank 1" quirk checked the raw,
       unmasked byte written for zero instead of the masked value that
       actually becomes the new bank number — found by diffing this
       core's CPU trace against the Peanut-GB reference core down to
       the exact instruction of first divergence, which turned out to
       be a single `LD A,(DE)` reading a bank-mapped byte differently
       between the two cores. Mooneye's own test ROMs deliberately
       write garbage in the unused upper bits specifically to catch
       this exact class of bug (commented in their source as "set high
       bits to expose bugs") — and it did.
  **MBC5 now passes Mooneye's entire suite 100% clean** (all 8 ROM-size
  tests plus every bank-register test). **MBC1 passes 10 of 13** (up
  from 6) — all bank-register/RAM tests plus ROM sizes up to 4Mb/512KB.
  The remaining 2 ROM-size failures (rom_8Mb, rom_16Mb) were confirmed
  to also fail identically against the Peanut-GB reference core itself,
  strong evidence they test a specific real-hardware MBC1 wiring quirk
  (likely MBC1M multicart-variant behavior) beyond standard MBC1
  emulation, not a gap unique to this project. The 13th (multicart_
  rom_8Mb) is an expected failure — MMM01/multicart carts are a known,
  out-of-scope cart type entirely.
- **Tested 6 more real commercial ROMs** (Dr. Mario, Super Mario Land,
  Gargoyle's Quest, Kirby's Pinball Land, Kirby's Dream Land 2, Pokemon
  Crystal — not included in this repo/archive, copyright; ask the
  person for copies again to re-run this testing), covering gaps
  nothing had tested before: a plain ROM-only cart, more MBC1 games,
  and the first-ever MBC2 game tested against this project. Dr. Mario,
  Super Mario Land, Gargoyle's Quest, and Kirby's Dream Land 2 all boot
  and render correctly (confirmed by dumping the actual framebuffer).
  Found and fixed a real MBC2 buffer-overflow bug (RAM access wasn't
  masked to MBC2's actual 512-byte size — same class of bug as the
  Mooneye-driven MBC1/MBC5 fixes above) and a real, independent CPU-
  timing bug (EI's effect was immediate instead of correctly delayed by
  one instruction, a documented real hardware quirk). Pokemon Crystal
  (2MB, CGB-only) renders its title logo correctly then freezes on what
  is almost certainly a "Game Boy Color required" dialog that doesn't
  render correctly without CGB tile-attribute support — an expected
  consequence of this project's already-documented lack of GBC support,
  not a new bug.
- **Kirby's Pinball Land's blank-screen hang — RESOLVED in a later
  session** (see "What's next" item 2 for the fix and the real-ROM
  evidence). Originally traced via reference-diffing to a Timer-
  interrupt-gated countdown flag in HRAM that only this emulator
  failed to ever fully decrement — the Timer interrupt was requested
  at the correct rate throughout (135 times in 1 million instructions)
  but its vector was reached only once, while VBlank (higher priority,
  very similar ~70000-cycle period) reached its own vector 134 times in
  the same window. The leading hypothesis at the time - "some small,
  still-unidentified cycle-accounting phase difference" systematically
  starving Timer relative to VBlank - turned out to match a real bug
  found independently, much later, via Mooneye's suite rather than by
  chasing this ROM directly: HALT was overcharging every wait by 4
  T-cycles (see the sub-instruction timing section above). Once real
  ROMs were available again to check against, this ROM was confirmed
  actually playing (ball, flippers, and score all progressing across
  multiple checkpoints), not stuck.
- **8x16 sprite mode implemented** (`LCDC` bit 2) — real hardware ignores
  bit 0 of the OAM tile number and stacks two consecutive tiles as one
  16-pixel-tall sprite; Y-flip mirrors the whole 16-pixel sprite (which
  also swaps which physical tile ends up on top), not each 8-pixel half
  independently. `DrawOBJline`'s bounding-box check and row lookup both
  previously hardcoded an 8-pixel-tall sprite unconditionally.
- **MBC3 RTC now persists alongside cart RAM.** Previously only plain
  cart RAM was ever saved — a cartridge with an actual real-time-clock
  chip (Pokemon Gold/Silver/Crystal and similar) would silently lose
  its RTC state to power-on defaults every boot. `CartHasRTC()`
  correctly distinguishes the two real MBC3+TIMER cart types from the
  RAM-only MBC3 variants (Pokemon Red/Blue's own cart type has no RTC
  at all despite being numerically close). Verified with a full save →
  persist → reload round trip using a synthetic MBC3+TIMER ROM,
  including performing the real RTC latch sequence (the documented
  $6000 00-then-01 write real hardware requires before a read becomes
  visible) in the independent load-verification process.
- **GBC (Game Boy Color) support implemented.** Banked VRAM (16KB/2
  banks) and WRAM (32KB/8 banks), the CGB register set (`VBK`/`SVBK`/
  `KEY1`/`BCPS`-`BCPD`/`OCPS`-`OCPD`), and CGB-aware BG/window/sprite
  rendering (per-tile palette/VRAM-bank/flip attributes, 3-bit sprite
  palettes) — all gated on the cartridge header's CGB flag, so DMG
  carts are completely unaffected. Verified with a synthetic ROM that
  set an explicit palette color and got back an exactly-matching pixel
  in the rendered output (solid `RGB(255,0,0)` across the whole
  screen), and with two real ROMs: Pokemon Crystal (CGB-only) now
  renders a correct, legible hardware-compatibility dialog instead of a
  black or frozen screen, and Pokemon Yellow (CGB-enhanced) renders
  correctly under the new code path.
- **GBC double-speed mode timing and HDMA/GDMA VRAM DMA implemented.**
  `STOP` now correctly triggers the real speed-switch mechanism (toggles
  `KEY1` bit 7, clears bit 0, only when the game armed it first) and
  `cycleLength()` correctly halves DIV/timer/PPU timing advancement
  relative to CPU cycles while double-speed is active - verified with a
  synthetic ROM confirming `KEY1` reads exactly `$7E` before and `$FE`
  after arming and executing `STOP`. Both General-Purpose (immediate)
  and H-Blank-paced (16 bytes per scanline, continuing across multiple
  `hblank()` calls) DMA modes work, reusing the normal memory read/write
  path so source/destination banking resolves correctly - verified with
  two synthetic ROMs (one per mode) confirming transferred bytes matched
  exactly, including the H-Blank mode only after running long enough for
  every needed H-Blank to actually occur. **Implementing both did not
  get Pokemon Crystal past its hardware-compatibility screen** - see
  below for what's still unknown there.
- **Sprite-vs-background priority implemented (DMG + CGB) — closes the
  last known GBC rendering gap.** The OAM flags byte's priority bit had
  never actually been read anywhere before this - every sprite always
  drew on top of everything regardless of the bit, on both DMG and CGB.
  Now correctly resolves the sprite's own priority bit against the BG/
  window color underneath (never hidden behind BG color 0, only colors
  1-3), plus CGB's per-tile BG-priority attribute and its
  reinterpretation of LCDC bit 0 as BG/Window Master Priority. Verified
  with three synthetic ROMs (sprite over blank BG; sprite correctly
  hidden behind an opaque BG tile; sprite correctly visible in front of
  one) each producing exactly the expected pixel. **Also found and
  fixed while verifying this**: sprite X positioning used `bx - 7 + j`
  instead of the real-hardware-correct `bx - 8 + j` (OAM's X byte is
  the screen column plus 8) - shifting every sprite one pixel right of
  its correct position, in every game, for this entire project's
  history until now. Found because it made two deliberately different
  priority test cases produce identical (wrong) output.
- **Sound implemented: full GB APU core (rigorously verified, including
  actual audio content) + PS1 SPU output (implemented, still
  unverified).** All 4 real channels (2 pulse, wave, noise) at the
  register/timing level - triggering, length counters, envelopes,
  sweep, and the 512Hz frame sequencer - verified with synthetic ROMs
  (trigger + immediate DAC-off silencing, length-counter auto-disable
  timing, noise channel trigger) all producing exactly the expected
  `NR52` status byte. Found and fixed one real bug during a self-review
  pass on the initially-unverified SPU code: `PulsePitch()`/
  `WavePitch()` could silently overflow the SPU's 16-bit pitch register
  at very high GB frequencies (rare in music, but real in some sound
  effects) and wrap to a garbage value instead of a merely very high
  one - now clamped.

  Beyond the register-level tests, a new `DUMP_WAV` harness feature
  mixes the APU core's own already-verified per-channel output directly
  in software into a real, standard WAV file - independent of (and
  doesn't test) the PS1 SPU integration, but gives genuine, listenable
  confirmation of the *core's* correctness specifically. Ran it against
  Dr. Mario's early gameplay music and directly analyzed the result:
  real non-trivial waveform content (97.6% non-zero samples), and an
  FFT of a sustained-tone window found a clean, ordinary 904Hz musical
  peak - with the raw samples in that same window showing a long
  constant plateau then a brief transition, exactly the expected shape
  of a low-duty-cycle square wave sampled mid-"off"-phase. Genuine,
  independently-obtained evidence the APU core produces correct audio
  content, not just documentation-matching register behavior.

  The PS1 SPU side remains the one unverified part of this whole
  project: it drives 4 real hardware voices (ADPCM square waves for the
  pulse channels, a dynamically re-encoded wave sample, and the SPU's
  actual hardware noise generator for the noise channel) rather than
  software-mixing PCM, and compiles/links cleanly against the real
  PSn00bSDK toolchain - but has not been confirmed to produce correct,
  or any, actual sound, since this sandbox has no way to play back or
  capture audio at all. Real hardware or a working interactive emulator
  session is the only way to close this gap.

- **Fixed a real PPU frame-timing bug affecting every game: every mode
  transition discarded overshoot cycles, making every frame run ~2%
  long.** `VideoCyclesLeft -= sysCycle` regularly undershoots past zero
  by a few cycles (GB instruction costs rarely divide the PPU mode-
  length constants evenly), and every mode transition was doing a plain
  `VideoCyclesLeft = <constant>` assignment that threw away that
  overshoot instead of carrying it forward - happening several hundred
  times a frame, compounding into a real, measured ~1600-2000 extra
  cycles per frame (actual measured period ~71800-71840 instead of the
  correct 70224). Found by adding a lightweight, always-on cumulative
  cycle counter and measuring the actual elapsed cycles between
  consecutive VBlank interrupts empirically, rather than continuing to
  assume they matched the textbook constant. Fixed at all 5 mid-stream
  transition sites; re-measured afterward and confirmed frame period
  now lands almost exactly on 70224. This was found while re-
  investigating the Kirby's Pinball Land hang, but does **not** resolve
  it - re-tested afterward and the same ROM still hangs identically,
  and the Timer-vs-VBlank starvation pattern is only marginally
  improved (Timer's vector went from being reached once to twice in a
  million instructions) - whatever is really keeping that ROM's Timer
  interrupt starved remains open. The timing fix itself is real and
  independently valuable regardless of that specific ROM's outcome.

- **Newly discovered, large, real gap: sub-instruction cycle-accurate
  memory timing has never been validated, and mostly fails.** Built and
  ran Mooneye's `acceptance/ppu`, `acceptance/timer`, and `acceptance/
  interrupts` test categories against this core for the first time ever
  (previously only `emulator-only/mbc1` and `mbc5` had been tried) - 67
  tests, only 11 passed. Some failures are expected (boot-ROM-dependent
  tests - `boot_div*`, `boot_regs*`, `boot_hwio*` - this project
  deliberately skips boot ROM emulation, an established prior
  decision). But a large number are ordinary instruction-timing tests -
  `call_timing`, `push_timing`, `pop_timing`, `ret_timing`,
  `jp_timing`, `rst_timing`, both plain and conditional variants, plus
  `ei_sequence`, `halt_ime1_timing2-GS`, `oam_dma_timing`, and more -
  confirmed via direct trace to be genuine Mooneye `$42`-signature
  failures, not harness timeouts or a false alarm. These test exactly
  WHEN within a multi-cycle instruction's execution its memory accesses
  happen (interactions with interrupts, OAM DMA, etc. depend on this),
  not just an instruction's total cycle count and final register/memory
  result - which is all `cpu_instrs` validates, and which this core
  already passes cleanly (11/11). This core currently performs a memory
  write, then accounts the whole instruction's cycle cost in one lump
  sum afterward, with no notion of sub-instruction timing at all.
  Fixing this comprehensively would mean restructuring how memory
  accesses are timed across a very large fraction of the opcode table -
  comparable in scope to this session's GBC-support or sound-
  implementation efforts, not something to attempt piecemeal. Full list
  of currently-failing tests (excluding the expected boot-ROM ones):
  `add_sp_e_timing`, `call_cc_timing`, `call_cc_timing2`, `call_timing`,
  `call_timing2`, `di_timing-GS`, `ei_sequence`, `halt_ime1_timing2-GS`,
  `if_ie_registers`, `jp_cc_timing`, `jp_timing`, `ld_hl_sp_e_timing`,
  `oam_dma_restart`, `oam_dma_start`, `oam_dma_timing`, `pop_timing`,
  `push_timing`, `rapid_di_ei`, `ret_cc_timing`, `ret_timing`,
  `reti_timing`, `rst_timing`, `ie_push`, `div_write`, `rapid_toggle`,
  `tim00`, `tim01_div_trigger`, `tim10`, `tim10_div_trigger`, `tim11`,
  `tima_reload`, `tima_write_reloading`, `tma_write_reloading`,
  `hblank_ly_scx_timing-GS`, `intr_1_2_timing-GS`, `intr_2_0_timing`,
  `intr_2_mode0_timing`, `intr_2_mode0_timing_sprites`,
  `intr_2_mode3_timing`, `intr_2_oam_ok_timing`, `lcdon_timing-GS`,
  `lcdon_write_timing-GS`, `stat_irq_blocking`, `stat_lyc_onoff`,
  `vblank_stat_intr-GS`. Test source and a working build toolchain are
  already set up at `/home/claude/mooneye-test-suite` (WLA-DX at
  `/home/claude/wla-dx/build/binaries`) for whoever picks this up.

  (This list reflects the original discovery, at 11/67 passing. See
  below for the fixes made since - `push_timing`, `oam_dma_restart`,
  `oam_dma_timing`, and `rst_timing` have since been removed from the
  failing list, at 14/67 passing as of the latest commit.)

  One isolated piece already fixed along the way: OAM DMA previously
  completed all 160 bytes instantly, for free, with zero cycle cost -
  real hardware takes 640 T-cycles (one byte per M-cycle). Now properly
  cycle-stepped via a new `DMAClock()`. Confirmed via direct trace of
  `oam_dma_start.s` that this alone isn't enough to pass `oam_dma_*`
  (they check OAM contents at precise M-cycle checkpoints against
  concurrently-executing HRAM code, which needs the same sub-
  instruction timing model as everything else in this section) - but
  it's a real, independent correctness improvement on its own terms.

  Two more pieces attempted: `push()`/`pop()` now charge their M-cycles
  progressively (matching real hardware's actual per-cycle timing)
  instead of one lump sum at the end, and every PUSH/POP opcode updated
  to match; CPU memory access is now correctly restricted to HRAM-only
  while OAM DMA is active (except `$FF46` itself, the DMA trigger
  register, which real hardware never blocks even mid-transfer, since
  that's how a game re-triggers the next one).

  **Important lesson learned the hard way**: the progressive push()/
  pop() change broke two previously-passing tests (`intr_timing`,
  `halt_ime0_nointr_timing`) by double-charging interrupt dispatch's
  cycle cost - `interrupt()` also calls `push()` (via `rst()`) and still
  had its own full lump-sum charge afterward, on top of push()'s new
  internal charges. `cpu_instrs` and Mooneye's `mbc1`/`mbc5` suites
  alone did **not** catch this - only re-running the full `acceptance/
  ppu`+`timer`+`interrupts`+top-level suite and diffing the exact
  before/after pass lists (not just the totals) surfaced it. Fixed and
  re-verified.

  Continuing from there: found and fixed DMA's own *start* timing too
  (it began counting its 640T budget immediately when triggered, mid-
  instruction, rather than only after the triggering instruction
  actually finished - costing every transfer 12T it shouldn't have
  had). This alone was a real, safe win: **11/67 → 14/67** on the full
  suite (`push_timing` passing, plus `oam_dma_restart`, `oam_dma_timing`,
  and `rst_timing` as knock-on wins from the same fix).

  Chasing `push_timing`'s second scenario further led to a matching fix
  for DMA's *end* timing (keeping OAM blocked through the exact M-cycle
  DMA's last byte completes, not just up to it) - traced directly
  against the test's own source and confirmed by register tracing that
  it made `push_timing` pass completely. **This one turned out to be a
  real regression**: the full regression sweep (now run after every
  change, per the lesson above) caught Pokemon Red hanging indefinitely
  and rendering solid black instead of its title screen. Bisected to
  this exact change and confirmed via the project's own pre-existing
  baseline build that it was new. Real game compatibility outweighs
  passing one additional synthetic test - fully reverted; `push_timing`
  is back to failing, but the safe start-timing fix and its 3 knock-on
  passes are kept. Second lesson reinforced: matching a test's own
  documented intent exactly is not the same claim as "safe for real
  games," and the full sweep - real ROMs included, not just the Mooneye
  suites - is what actually proves the difference.

  Next, found and fixed the same double-charge bug (already known from
  `interrupt()`) in **all 8 `RST` opcodes and all 5 `CALL` opcodes**:
  each called `push()` (which now charges its own M-cycles
  progressively) but *also* charged the instruction's old, full lump
  sum afterward - RST was taking 28T instead of the correct 16T,
  confirmed by direct measurement with a tiny synthetic ROM. `CALL`
  also needed its 2 address-byte reads split into individually-charged
  M-cycles, matching the same pattern as PUSH/POP. Fixing RST's timing
  correctly caused `rst_timing.gb` to regress - traced immediately and
  confirmed it's the *same* OAM-DMA-completion-boundary nuance behind
  the reverted `dmaPendingEnd` regression above: `rst_timing.gb` was
  only passing before by coincidence, with RST's old *incorrect* timing
  happening to land on the right cycle alignment. Kept the genuine
  fix rather than revert it to keep a coincidental pass. `call_timing.gb`
  and its conditional variants remain failing for the identical
  underlying reason (confirmed via source inspection) - not a new
  regression, since they were already failing beforehand. Currently
  13/67 on the full suite (`rst_timing` swapped out, everything else
  from the DMA start-timing fix retained). Real ROMs (Pokemon Red
  especially, given how heavily real games use CALL) re-verified
  correct throughout.

  Finished the same fix for `RET`/`RETI` (unconditional RET was 24T
  instead of 16T, conditional RET cc taken was 28T instead of 20T,
  RETI matched unconditional RET) - took particular care verifying
  `reti_intr_timing.gb`, `intr_timing.gb`, and `halt_ime0_nointr_
  timing.gb` immediately after, since RETI's own IME-enable timing is
  exactly what those tests check, and confirmed all three still pass.
  Still 13/67 overall - `ret_timing`/`ret_cc_timing`/`reti_timing`
  fail for the identical underlying reason as `call_timing`/
  `rst_timing` (not a regression, already failing beforehand). PUSH,
  POP, CALL, RST, RET, and RETI are now all consistently fixed to
  their correct real-hardware total cycle counts, and a full audit of
  every remaining `push()`/`pop()`/`call()`/`ret()`/`rst()` call site
  confirmed none of the others were missed.

  Checked `JP nnnn` too while auditing: its total cycle count (16T) is
  already correct (it doesn't call `push()`/`pop()` at all, so no
  double-charge risk there) - but it still reads both address bytes
  atomically via `ReadWord()` rather than as two separately-charged
  M-cycles the way CALL's own reads now are. A narrower, lower-priority
  correctness gap than anything fixed above: it would only matter if
  DMA's active/inactive state actually changes mid-read (a real but
  rare scenario), not something affecting every JP the way the
  double-charge bugs affected every CALL/RET/RST/PUSH/POP. Not fixed in
  this pass - noted here for whoever picks this up next, rather than
  expanding scope further without checking in first.

  Pivoted to the Timer-specific tests, since most don't depend on the
  OAM-DMA nuance the CALL/RET/RST family does - genuinely different
  ground. Fixed a real, documented hardware quirk: TIMA reads as $00
  for 4 T-cycles after overflow before taking TMA's value, not
  immediately (verified real on DMG/MGB/SGB2/CGB/AGB/AGS per
  Mooneye's own test comment).

  That alone didn't get `tima_reload.gb` passing - tracing its source
  revealed a separate, deeper architectural gap: TIMA and DIV are
  actually driven by the same underlying 16-bit hardware counter, not
  two independent ones. **Fixed properly**, since - unlike the OAM-DMA
  nuance - this one is precisely documented and didn't carry the same
  risk of encoding a guessed, possibly-wrong hardware behavior.
  Replaced the old independent DIVCOUNTER/TIMECOUNTER pair with a real
  unified 16-bit counter (`internalDivCounter16`); DIV is now purely
  derived from its upper byte; a new `CheckTimerEdge()` detects the
  real falling-edge-on-a-specific-bit mechanism that drives TIMA,
  called every T-cycle and immediately after any DIV or TAC write
  (both of which can themselves cause the edge that increments TIMA -
  the whole reason `div_write.gb`/`rapid_toggle.gb` exist).

  **Result: 8 more tests pass at once - 13/67 → 21/67, the single
  biggest jump from any change in this entire timing effort**: `tim00`,
  `tim01_div_trigger`, `tim10`, `tim10_div_trigger`, `tim11`,
  `tima_reload`, `div_write`, `rapid_toggle`. `tima_write_reloading`/
  `tma_write_reloading` remain failing - a related but distinct nuance
  (canceling the reload by writing TIMA/TMA during the 4-cycle window)
  not yet implemented. Real ROMs re-verified correct throughout
  (Pokemon Red pixel-matched to its known-good baseline); one apparent
  regression (Kirby's Dream Land 2 showing blank white) turned out,
  on direct comparison against the pre-rewrite committed state at the
  identical instruction counts, to already be blank there too - a
  pre-existing "different point in dynamic gameplay" artifact, not
  something this fix introduced.

  Follow-up attempt on the remaining 2 Timer failures
  (`tima_write_reloading`/`tma_write_reloading`): added "a write to
  TIMA while a reload is pending cancels it," a real, documented
  behavior confirmed via both tests' own source comments - but not
  precise enough to pass either, since they test a more specific
  cycle-by-cycle sub-nuance (exactly *which* T-cycle(s) within the
  4-cycle window a write does or doesn't cancel the reload) this
  simpler, uniform model doesn't capture. Kept anyway since it's a
  real improvement over the previous "reload state ignored entirely on
  write" behavior and introduces no regression (confirmed: still
  21/67) - getting the exact remaining nuance right would need the
  same careful, direct empirical tracing that resolved `push_timing.gb`
  earlier, not attempted here to avoid guessing at something subtly
  wrong the way an earlier DMA-nuance attempt turned out to be.

  **New session: 21/67 → 29/67, six independent, individually-verified
  fixes, zero regressions.** Toolchain setup was ephemeral (a fresh
  sandbox, nothing preserved from prior sessions) - rebuilt WLA-DX from
  source and Mooneye's test suite exactly per "Setting up Mooneye's
  test suite" above, plus cloned `retrio/gb-test-roms` for `cpu_instrs`
  regression testing (also not in this repo - see that section for the
  clone commands). Every fix below was verified against the full
  `acceptance/ppu`+`timer`+`interrupts`+top-level sweep (diffing the
  exact pass list, not just the total), `cpu_instrs` (stayed 11/11),
  Mooneye `mbc1`+`mbc5` (stayed 18/21), and the two small bundled
  synthetic ROMs at `psn00bsdk-build/iso_files/GAME.GB`/`GAME2.GB`
  (Blargg's `02-interrupts`/`01-special`, already in this repo, not
  copyrighted commercial ROMs) re-rendered and diffed pixel-for-pixel
  via `ROWSUMMARY`/`DUMP_PPM` - no real commercial ROMs were available
  in this sandbox this session, so that piece of the usual regression
  discipline (re-render 2-3 real ROMs) couldn't be done; worth doing
  the next time real ROMs are available, given how central some of
  these changes are (the interrupt dispatch and PPU-off rewrites
  especially).
    1. **IF ($FF0F) now reads unused bits 5-7 as 1** (same convention
       already used for SIO control and the sound registers) - fixed
       `if_ie_registers`.
    2. **DI now cancels a still-pending EI delay, and EI no longer
       re-arms an already-pending one.** DI only cleared IME, not
       `EI_PENDING`'s countdown, so a DI executed right after EI didn't
       actually stop IME from turning on a moment later; EI
       unconditionally reset the delay to 2 even mid-countdown, pushing
       IME's activation out an extra instruction whenever EI executed
       twice in a row. Fixed `rapid_di_ei` and `ei_sequence`.
    3. **HALT was overcharging 4 T-cycles on every single wait** - its
       own one-M-cycle decode cost was charged *after* the wait loop,
       on top of whatever the loop itself already charged, instead of
       up front like every other opcode. Since a HALT-then-wait-for-
       VBlank main loop is close to universal across the library, this
       was a broad per-frame pacing bug, not an edge case. Fixed
       `halt_ime1_timing2-GS` and two others.
    4. **Interrupt dispatch now re-reads IE/IF live during the actual
       PC push**, matching real hardware, instead of picking a vector
       once up front and pushing PC via the ordinary push()/rst()
       helpers. Games that deliberately point SP at $FFFF/$FF0F right
       before an interrupt (a real, documented hardware quirk) can have
       the push's own write to IE/IF redirect or cancel that exact
       dispatch. The precise rule (derived and hand-verified against
       all 4 rounds of the test before implementing): a change from the
       PC-high-byte write (SP-1) can still redirect/cancel the vector;
       a change from the PC-low-byte write (SP-2) is always too late,
       since the vector is already committed by then; IME is cleared
       either way, cancellation or not. Fixed `ie_push`.
    5. **The PPU now actually freezes while LCDC bit 7 (LCD enable) is
       0** - previously LY and the STAT mode/coincidence bits kept
       advancing even with the display "off" (the entire PPU state
       machine in `cycleLength()` had no gate on this at all). Real
       hardware freezes LY at 0 and stops recomputing STAT's mode and
       LYC-coincidence bits the instant the display turns off, resuming
       only once turned back on; a coincidence bit that goes from
       false to true (not "was already true") right as the display
       turns back on fires the STAT interrupt if that source is
       enabled. Also made the LYC-coincidence bit recompute immediately
       on a software write to LYC while the LCD is on (previously only
       recomputed once per 80-456-cycle PPU mode transition - too
       coarse for real hardware's effectively-live comparator), and
       fixed STAT's unused bit 7 to read as 1 (same convention as IF
       above). Fixed `stat_lyc_onoff`.
  Remaining failures in this suite, past the already-documented
  boot-ROM-dependent ones: the CALL/PUSH/POP/RST/RET/OAM-DMA-boundary
  cluster (`push_timing`, `pop_timing`, `rst_timing`, `call_timing`
  and its `_2`/`_cc` variants, `ret_timing`/`ret_cc_timing`/
  `reti_timing`, `jp_timing`/`jp_cc_timing`, `add_sp_e_timing`,
  `ld_hl_sp_e_timing`, `oam_dma_start`) - **deliberately not touched
  this session**, since this is the exact cluster whose earlier attempts
  caused real, silent regressions in actual commercial games (see the
  `dmaPendingEnd` and `push_timing` history above) and this session had
  no real ROMs available to validate against; a finer-grained PPU mode-
  timing cluster (`hblank_ly_scx_timing-GS`, `intr_2_0_timing`,
  `intr_2_mode0_timing`(`_sprites`), `intr_2_mode3_timing`,
  `intr_2_oam_ok_timing`, `lcdon_timing-GS`, `lcdon_write_timing-GS`,
  `stat_irq_blocking`, `vblank_stat_intr-GS`) that's a similarly-sized,
  separate dedicated effort from what just got fixed above (spot-
  checked `di_timing-GS`'s source specifically - it hinges on exact
  frame-period cycle counting across a full VBlank-to-VBlank span, a
  genuinely deeper nuance than the LCD-on/off freeze fixed above, not
  more of the same fix); and `tima_write_reloading`/
  `tma_write_reloading`, unchanged from the already-documented prior
  attempt.

## What's next (roughly in priority order)

1. **Sub-instruction cycle-accurate memory timing (see above) — a
   large, real gap, though real progress has been made.** Currently
   29 of 67 Mooneye acceptance/ppu+timer+interrupts tests pass (up
   from an initial 11), most of the remaining failures for reasons
   unrelated to the already-known boot-ROM limitation. Comparable in
   scope to the GBC-support or sound-implementation efforts this
   session - a dedicated pass, not a quick fix, and one where every
   change needs the full regression sweep (see above) before being
   trusted.
2. ~~**Kirby's Pinball Land hang**~~ **RESOLVED this session** (still
   worth a skeptical re-check next time real ROMs are available, given
   how long this one resisted diagnosis - but the evidence is strong).
   The user supplied the actual real ROMs this project's own testing
   had relied on in earlier sessions (Dr. Mario, Kirby's Dream Land 2,
   Kirby's Pinball Land, Pokemon Red/Yellow/Crystal - kept local only,
   per usual, never committed). Re-running Kirby's Pinball Land against
   this session's HALT-timing fix (see above - HALT was overcharging
   every wait by 4 T-cycles) shows real, progressing pinball gameplay
   at three separate checkpoints (5M/15M/30M/60M instructions in) -
   a title/ranking screen, then live gameplay with the ball, flippers,
   and score all visibly changing across checkpoints (score reset
   partway through is expected: the harness supplies no controller
   input, so the ball just drains and a new game starts) - not the
   permanent blank screen this ROM was previously stuck on. This
   strongly matches the leading hypothesis from the original
   investigation ("some small, still-unidentified cycle-accounting
   phase difference" between this emulator and real hardware
   systematically starving the Timer interrupt relative to VBlank) -
   the HALT fix is exactly that class of bug, and was found completely
   independently via Mooneye's suite, not by chasing this ROM directly.
   Also re-verified via the same real ROMs: Dr. Mario, Kirby's Dream
   Land 2, Pokemon Red, and Pokemon Yellow all still render correctly
   (title/gameplay screens visually confirmed, pixel content sane) -
   no regressions from this session's 6 timing commits. Pokemon Crystal
   is unchanged from its previously-documented state (correctly renders
   the "designed only for use on the Game Boy Color" compatibility
   screen, still doesn't get past it - see item 4 below, unrelated to
   anything fixed this session).
3. More real-ROM testing now that real ROMs are available again this
   session (see item 2) - previous rounds of real-commercial-game and
   authoritative-suite testing found genuinely high-value bugs,
   including in code paths (MBC2, EI timing) nothing else had touched.
   Don't commit ROM files themselves to this repo or bundle them in
   any output archive
   (copyright) — keep them local/sandbox-only.
4. **GBC support is functionally complete for rendering purposes** -
   sprite-vs-background priority (the last known gap, including the
   CGB-specific BG-to-OBJ override) is implemented and verified (see
   above). Pokemon Crystal still doesn't get past its "designed only
   for use on the Game Boy Color" hardware-compatibility screen despite
   double-speed mode and HDMA/GDMA both being implemented - whatever
   that specific check actually depends on remains unidentified;
   further progress on that exact screen would need disassembling/
   tracing Crystal's own detection routine, a bigger undertaking than
   guessing at candidate features.
5. **Sound is implemented (see above) but the PS1 SPU output half is
   unverified** - real hardware or a working interactive emulator
   session (neither available in this sandbox - see the existing note
   on this below) is needed to confirm it actually produces correct
   audio, as opposed to just compiling and matching documentation.
   Worth prioritizing a real playback test above most other remaining
   items here, since it's the one part of this whole project that has
   never been confirmed to actually work as intended.
6. **Bank-streaming for very large ROMs.** The core's `ROM[loc]` is a
   flat, fully-resident pointer with no partial loading. 1.5MB covers
   the large majority of the real library, but the small number of
   even larger late-era GBC games (up to 4-8MB — Pokemon Crystal itself
   is exactly 2MB, already over this project's current cap) still won't
   fit resident in the PS1's 2MB of RAM, now further reduced by GBC's
   larger VRAM/WRAM buffers (+32KB total) — worth re-checking total
   memory footprint if this cap is ever raised.
7. **Very large cart RAM won't fit a single memory card.** A standard
   PS1 card has 15 usable 8KB blocks (120KB total); `SaveCartRAM`
   requests exactly the blocks a cart needs, but a 128KB-RAM MBC5 game
   would need every single block on the card, and anything larger
   wouldn't fit at all. Affects a small minority of RAM-heavy titles;
   not fixed proactively since most games use far less.
8. **Real visual confirmation** (screenshot/video from actual hardware
   or a working emulator session) — every emulator-boot attempt in this
   sandbox has hung or stalled (see the note below); not something to
   keep spending sandbox time on, but worth doing whenever a real
   console or an interactive emulator session is available. This is
   also the only way to verify item 4 (actual SPU audio output) above.

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

Attempts to get a visual screenshot of the built PS-EXE/CD image running
under an emulator have not succeeded in this sandbox despite several
different approaches:

- mednafen standalone with a raw injected `.exe` (no disc) - stuck on
  OpenBIOS's boot logo indefinitely.
- mednafen standalone with a real, mkpsxiso-built CD image (later
  sessions, once CD loading existed) - same stuck-on-logo behavior, even
  though the disc structure itself was independently confirmed correct
  with `isoinfo` in that same session.
- pcsxr (a different emulator core entirely, `-cdfile ... -nogui`) -
  hung indefinitely on startup in this sandbox (unclear why; possibly a
  GTK/display-session assumption that doesn't hold headless), consuming
  two full 5-minute timeouts before being abandoned as a dead end.

None of this points at anything wrong with the executables or disc
images themselves - independently confirmed correct by `file`/mednafen's
own PS-EXE header parser (exact right entry point/text segment both
times), and by `isoinfo` (every file on the CD image byte-identical to
its source, exactly as intended). This looks like a genuine limitation
of getting *any* of these emulators to boot arbitrary homebrew
non-interactively in this specific sandboxed, headless environment,
rather than a property of this project's output. Real hardware, or a
GUI session where these emulators' normal interactive boot flow (and any
manual disc-eject/BIOS-menu tricks some of them need) can actually be
driven, would be the way to get an actual screenshot - worth trying if
that's ever available, but not something to keep spending sandbox time
on. The host-harness PPM-dump approach (rendering the actual emulator
core's output to a real image file, verified extensively earlier this
session) remains the one visual-correctness technique that has
genuinely worked throughout, and is what actually found and helped fix
the real rendering bugs in this project.
