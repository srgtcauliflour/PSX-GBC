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
- Sub-instruction cycle-accurate timing: a large, real, **mostly
  fixed** gap - currently 52/67 on Mooneye's `acceptance/ppu`+`timer`+
  `interrupts`+top-level suite (started at 11/67). The entire CALL/
  PUSH/POP/RST/RET/JP/ADD SP,e/OAM-DMA sub-family and most of the STAT-
  interrupt/PPU-mode-timing cluster are now fixed; VRAM-access
  blocking during mode 3, line 0's own special post-power-on timing,
  and the TIMA/TMA write-during-reload edge cases are also all now
  fixed and verified regression-free (all 24 of `lcdon_timing-GS`'s LY
  checks pass; `tima_write_reloading`/`tma_write_reloading` both PASS
  outright). What's left is a narrow, LCD-on-adjacent-only quirk
  (mode 3 on line 1, specifically the one right after a power-on line
  0, needs a few extra T-cycles this project doesn't model yet -
  confirmed NOT a general mode-3 bug, see below), and a still-
  unidentified interrupt-timing gap that now looks to be the single
  root cause blocking two separate tests at once
  (`hblank_ly_scx_timing-GS` and `intr_2_mode0_timing_sprites`) - mode
  3's per-sprite length penalty is now implemented and hand-verified
  against 18 of `intr_2_mode0_timing_sprites.gb`'s own cases, but that
  test is blocked by the same interrupt-timing gap, not by the sprite
  formula. See "What's next" item 1 for detail.
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
  **Follow-up in the same session, after the user supplied the real
  ROMs this project's testing has always relied on**: with real ROMs
  available to validate against, revisited the CALL/PUSH/POP/RST/RET/
  OAM-DMA-boundary cluster this section originally deferred (see the
  Kirby's Pinball Land entry above and in "What's next" item 2 for the
  real-ROM re-verification setup - ROMs kept local only, never
  committed). One more genuine, isolated fix came out of it:
  **`oam_dma_start` - CPU bus-blocking ("reads outside HRAM return $FF
  during an active transfer") was tied directly to `dmaActive`, which
  `doDMA()` sets the instant the trigger write happens - real hardware
  only starts blocking 2 M-cycles *after* that write** (the test's own
  diagram: M=0 write, M=1 still accessible, M=2 blocking begins).
  Tracked with its own delay, independent of the existing
  `dmaPendingStart`/`dmaCyclesElapsed` pair (which govern the *byte
  transfer's* own start, a related but different question) since they
  need different answers for a restarted DMA (a second $FF46 write
  while a transfer is already active): confirmed via the test's own
  two rounds that the previous transfer's blocking is never interrupted
  by a restart, even though the restart does immediately take over the
  actual source/byte-progress - so the blocking-delay state only gets
  (re)armed by a genuinely fresh start, never by a restart mid-transfer.
  Derived and hand-verified against both rounds before implementing.
  29/67 → 30/67; re-verified against all 6 real ROMs (Dr. Mario,
  Kirby's Dream Land 2, Kirby's Pinball Land, Pokemon Red/Yellow/
  Crystal) - all render identically to the pre-fix baseline.

  Went on to empirically trace `push_timing.gb` (temporarily
  instrumenting `push()`/`doDMA()`/`DMAClock()` to print exact
  cycle counts, rather than trusting hand arithmetic after an earlier
  hand-derivation attempt on this exact test turned out to have a
  silent error) to understand the *end*-of-DMA-blocking boundary -
  the counterpart to the start boundary just fixed. Found the actual
  root cause, and it's bigger than a boundary-off-by-one: `doDMA()`
  (and the DIV-register write handler, confirmed via the same kind of
  trace on `pop_timing.gb`) both run as a *synchronous side effect
  inside `WriteMEM()`*, called from partway through their triggering
  opcode's implementation - but that opcode still charges its *entire*
  cost as a single lump-sum `cycleLength()` call at the very end. For
  an opcode whose write is its last M-cycle (`LDH (n),A`, `LD (HL),A`),
  crediting that whole lump sum as "time after the trigger" silently
  over-counts by however many M-cycles happened *before* the write
  within that same instruction (the opcode fetch, and any operand
  read) - a systematic few-T-cycle bias, not a simple constant, since
  the previous session's own regression sweep would have to have had a
  test sensitive to that exact instruction's split to ever catch it.
  `dmaPendingStart`'s existing "eat one whole `cycleLength()` call"
  convention happens to net out correctly for `LDH (n),A` specifically
  (its write already is the last M-cycle) but not in general - and
  `pop_timing.gb`'s failure (still failing, unchanged) traces to the
  identical root cause on the DIV-reset side. Fixing this properly
  means giving simple, single-cycleLength-call write opcodes the same
  fetch-then-execute split PUSH/POP/CALL/RST/RET already got - a
  broad, opcode-table-wide restructuring in its own right, not a
  narrow DMA-specific fix, and **deliberately not attempted this
  session**: this is exactly the class of change the existing
  `dmaPendingEnd`/`push_timing` regression history above warns about,
  and getting it right needs a dedicated pass with its own full
  regression sweep, not a same-session bolt-on to the DMA-start fix
  above. `push_timing`, `pop_timing`, `call_timing` (and its `_2`/`_cc`
  variants), `ret_timing`/`ret_cc_timing`/`reti_timing`,
  `jp_timing`/`jp_cc_timing`, `add_sp_e_timing`, `ld_hl_sp_e_timing`,
  and `rst_timing` all remain failing for this reason.

  **Second follow-up, same session, once the user finished uploading
  all 6 real ROMs: did the "opcode-table-wide restructuring" pass this
  entry originally deferred.** Split `OP77`/`OP7E`/`OPE0`/`OPF0` (the
  `LD (HL),A`/`LD A,(HL)`/`LDH (n),A`/`LDH A,(n)` family) into their
  real per-M-cycle fetch/operand-read/access charges, matching PUSH/
  POP/CALL/RST/RET's existing pattern - the concrete fix the previous
  entry's root-cause analysis called for. Also found and fixed OAM
  DMA's end-of-transfer timing: the bus-blocking restriction doesn't
  lift the instant the 160th byte's own transfer completes - it stays
  active for one more M-cycle beyond that (644T, not 640T), the
  teardown-side counterpart to the already-fixed 2-M-cycle startup
  latency.

  This round of hand-derivation was rougher than the first: initial
  attempts at each of these produced **real, confirmed regressions**
  (splitting `OP77`+`OPE0` alone broke `tim00`/`tim01`/`tim10`/`tim11`/
  `div_write`/etc. by shifting exactly when a DIV/TAC write becomes
  visible to the timer edge-detector relative to a *different*
  precisely-timed instruction sequence; fixing that by also splitting
  `OPF0` then broke `div_timing` until `OP7E` got the same treatment;
  and the DMA end-timing fix went through several self-contradictory
  hand-derived values before being found **empirically instead** -
  scanning candidate delays against `push_timing.gb`, `oam_dma_timing.gb`,
  and `oam_dma_restart.gb` together and taking the one value all three
  independently-authored tests agree on, after hand arithmetic on the
  same three tests kept producing answers that satisfied one while
  contradicting another). Every fix was re-verified against the full
  acceptance sweep, `cpu_instrs`, Mooneye `mbc1`+`mbc5`, and - for the
  first time this session - re-rendered against all 6 real ROMs
  (Dr. Mario, Kirby's Dream Land 2, Kirby's Pinball Land, Pokemon
  Red/Yellow/Crystal): all still render correctly, including confirming
  Pokemon Red's title-screen walk animation is still cycling normally
  frame-to-frame (a snapshot mid-animation looked like a regression at
  first glance - it wasn't, just a different frame of the same loop).

  **Result of this round: 30/67 → 35/67** (`push_timing`, `pop_timing`,
  `div_timing`, `call_timing2`, `call_cc_timing2`, `rst_timing`).

  **The remaining 9 tests in this cluster - `call_timing`/
  `call_cc_timing`, `ret_timing`/`ret_cc_timing`/`reti_timing`,
  `jp_timing`/`jp_cc_timing`, `add_sp_e_timing`, `ld_hl_sp_e_timing` -
  were also resolved, later in the same session, once the user finished
  uploading all 6 ROMs and asked to continue.** These hung
  (`UNKNOWN`, not a clean `$42` fail): the CPU parked forever
  re-executing `RST $38` at `$0038` (SP draining every iteration, since
  nothing in these ROMs defines a real handler there - the design
  relies on the DMA-blocked-read byte landing *within* HRAM, e.g.
  `call_timing.gb`'s "the read is blocked, so the call target becomes
  $FFCA/$FFDA", both valid HRAM addresses with real handlers copied
  there).

  Traced `call_timing.gb` directly and found the real bug - not a
  timing-constant tweak but a **scope** error: the bus-blocking
  restriction ("CPU reads outside HRAM return `$FF` during an active
  DMA transfer") had been modeled as covering everything except HRAM,
  when it should be scoped to the actual OAM range (`$FE00`-`$FE9F`)
  the transfer is writing to. Real hardware's DMA controller only
  contends with the CPU on the bus(es) it's actively using - these
  tests all source from VRAM (a separate bus) and write to OAM, so
  WRAM/ROM/echo-RAM stay normally accessible throughout. `call_timing.gb`
  deliberately places its copied test procedure's opcode byte at
  `$FDFE` - two bytes *before* real OAM, specifically so it reads
  normally regardless of DMA timing, with only a later, real-OAM byte
  meant to be boundary-sensitive - and the old "blocks everything"
  model was blocking that supposedly-safe byte too, derailing control
  flow into the unhandled RST $38 vector. Verified the fix doesn't
  regress `push_timing`/`pop_timing`/`oam_dma_timing`/`oam_dma_restart`/
  `oam_dma_start` (all of which specifically target true-OAM addresses,
  so are unaffected by the narrower scope) before trusting it - fixed
  the entire remaining hang cluster outright: `call_timing`,
  `call_cc_timing`, `ret_timing`, `ret_cc_timing`, `reti_timing` all
  now pass. 35/67 → 40/67.

  The last 4 - `jp_timing`/`jp_cc_timing`, `add_sp_e_timing`,
  `ld_hl_sp_e_timing` - no longer hung after that fix, but still failed
  cleanly: a genuinely separate, already-identified gap from an earlier
  session's own audit note ("Checked JP nnnn too while auditing... it
  still reads both address bytes atomically via ReadWord()... not
  fixed in this pass"). Gave `JP nnnn`/`JP cc,nnnn` (all 5 opcodes:
  C2/C3/CA/D2/DA) and `ADD SP,e`/`LD HL,SP+e` the same fetch-then-
  execute M-cycle split as OP77/OPE0/etc earlier in this entry. 40/67
  → 42/67 → 44/67.

  **Total for this cluster, across the full session: 21/67 → 44/67,
  eleven commits, zero regressions** - every fix re-verified against
  the full acceptance sweep, `cpu_instrs`, Mooneye `mbc1`+`mbc5`, and
  all 6 real ROMs at every step (most late-session diffs came back
  byte-identical to the prior baseline, confirming these opcodes
  genuinely aren't exercised differently by any of the 6 games during
  normal play). This closes out the entire CALL/PUSH/POP/RST/RET/JP/
  ADD SP,e/OAM-DMA sub-instruction-timing family this project's own
  history had flagged as high-regression-risk - none of it remains
  failing (only the boot-ROM-dependent tests, the separate PPU
  mode-timing cluster below, and `tima_write_reloading`/
  `tma_write_reloading` do).

  **Third follow-up, same session: the PPU mode-timing cluster this
  entry originally deferred as "a similarly-sized, separate dedicated
  effort" got most of the way done too**, once the user asked to keep
  going after the CALL/PUSH/etc. cluster closed out. Six more real,
  independently-verified fixes, each checked against the full
  acceptance sweep, `cpu_instrs`, `mbc1`+`mbc5`, and all 6 real ROMs
  (Pokemon Red's and Pokemon Yellow's title-screen animations each
  shifted which exact frame lands at the fixed instruction-count
  snapshot at two points in this round - confirmed both times, by
  dumping several consecutive frames, that the animation itself is
  still progressing normally, not corrupted):
    1. **STAT interrupt (bit 1) also fires at VBlank when the mode=2
       source is enabled** - real hardware fires it at the same
       T-cycle as the dedicated VBlank interrupt whenever bit 5 is
       set, even though entering VBlank isn't literally "mode 2".
       Fixed `vblank_stat_intr-GS`.
    2. **The STAT interrupt is level-triggered, not edge-per-condition**
       - real hardware ORs every currently-enabled AND currently-true
       condition (LYC=LY with bit 6, modes 0/1/2 with bits 3/4/5) into
       one internal signal, and only fires on that signal's rising
       edge; if it's already high when a second condition also becomes
       true, no second interrupt fires until the signal drops back to
       low first ("STAT IRQ blocking"). The old code requested an
       interrupt independently at each condition with no shared state,
       which also hid two real, separate bugs: mode 2's interrupt was
       requested a *second* time when OAM search ended (entering mode
       3, which has no STAT source of its own), and mode 0's interrupt
       was wired to the wrong bit entirely (checked when *entering*
       OAM mode - backwards - instead of when entering HBlank, which
       had no check at all). New `UpdateStatLine()` computes the
       composite signal and fires only on the rising edge, called from
       every mode transition. Fixed `intr_2_0_timing`,
       `intr_2_mode0_timing`, `intr_2_mode3_timing`.
    3. **`UpdateStatLine()` needed calling from STAT and LYC writes
       too, not just mode transitions** - newly *enabling* a source
       while its condition is already true (e.g. writing STAT with bit
       4 set while already in VBlank) is itself a rising edge, and
       nothing about LY or the PPU mode changes when that happens.
       Fixed `stat_irq_blocking`.
    4. **CPU access to OAM is blocked during PPU modes 2 and 3**,
       separately from the OAM-DMA blocking fixed earlier this
       session - the PPU itself has exclusive access to OAM while
       actively searching it (mode 2) or fetching sprite data (mode
       3), real hardware's CPU reads back `$FF` for OAM during both,
       independent of whether a DMA transfer is also active. Nothing
       modeled this before. Fixed `intr_2_oam_ok_timing`.
    5. **Mode 3's length now varies with SCX** - the background pixel
       FIFO discards `(SCX mod 8)` pixels from the first tile it
       fetches each scanline, costing that many extra T-cycles
       (sampled once from SCX at the moment mode 3 begins), which
       correspondingly shortens that line's HBlank. Real, independently
       verified behavior worth keeping (smooth, non-tile-aligned
       scrolling is common in real games) but **didn't fully fix
       `hblank_ly_scx_timing-GS`** - its very first check (SCX=0, where
       this fix contributes nothing) already fails, pointing at a
       separate, still-unidentified gap in the interrupt-dispatch-to-
       polling-read latency chain that test depends on.
  **Result: 45/67 → 50/67.**

  Not attempted this round, each a distinct, bounded-but-substantial
  gap rather than more of the same fix:
    - `lcdon_timing-GS`/`lcdon_write_timing-GS` - read `lcdon_timing-GS.s`
      in full: it needs a documented "the PPU is 2 T-cycles late on
      line 0 specifically, lines 1+ are normal" quirk modeled precisely
      enough to reproduce an exact 24-entry table of LY/STAT/OAM-access/
      VRAM-access values at specific T-cycle offsets after LCDC is
      written, checked 3 times at slightly different phase offsets -
      and critically, also needs **VRAM access blocking during mode 3**,
      a feature that doesn't exist at all yet (only OAM got blocked this
      session) and is much more central to real games' rendering code
      than the OAM edge cases fixed above, so wiring it in carries real
      regression risk that deserves its own dedicated, carefully-
      verified pass rather than a bolt-on here.
    - `intr_2_mode0_timing_sprites` - needs mode 3's length to also
      vary with the number/position of sprites on the current line (on
      top of the SCX penalty above), a separate, larger penalty model
      this project doesn't have yet.
    - `tima_write_reloading`/`tma_write_reloading` remain unchanged
      from the already-documented prior attempt (exactly which
      T-cycle(s) within TIMA's 4-cycle reload window a write does or
      doesn't cancel the reload).

  **Fourth follow-up, same session: VRAM access blocking during PPU
  mode 3.** The first, lower-risk half of what `lcdon_timing-GS`/
  `lcdon_write_timing-GS` need (see above). Added the mirror of the
  existing OAM-blocking checks in `ReadMEM`/`WriteMEM`: while the LCD
  is on and `videoMode == TRANSFERMODE`, CPU reads of `$8000-$9FFF`
  return `$FF` and CPU writes to that range are dropped, exactly as
  real hardware's CPU-vs-PPU VRAM bus contention works (mode 2's OAM
  search doesn't touch VRAM, so it's mode 3 only, unlike OAM which is
  blocked across both modes 2 and 3). The PPU's own rendering code
  (`DrawBGline` etc.) reads the `VRAM[]` array directly rather than
  through `ReadMEM`, so rendering itself is unaffected - only CPU
  program access is gated.

  Verified as the highest-regression-risk change attempted this
  session, given how central VRAM access is to virtually every real
  game's tile-fetching code (unlike OAM, touched more narrowly):
  full Mooneye acceptance/ppu+timer+interrupts sweep (50/67, byte-
  identical PASS/FAIL composition to the pre-change sweep - zero
  tests flipped either direction), mbc1/mbc5 (18/21, matching the
  pre-existing known-failure baseline), `cpu_instrs.gb` (still
  11/11), and all 6 real ROMs (Kirby's Dream Land 2, Kirby's Pinball
  Land, Dr. Mario, Pokemon Crystal, Pokemon Yellow, Pokemon Red) -
  every single one produced a byte-identical framebuffer to the prior
  round's verified-clean dump at the same fixed 5M-instruction
  snapshot. Zero regressions.

  As expected, `lcdon_timing-GS`/`lcdon_write_timing-GS` still fail on
  their own (confirmed directly, not just via the sweep) - VRAM
  blocking alone was never going to be sufficient; the still-missing
  "PPU is 2 T-cycles late on line 0" quirk documented above is the
  other, harder half of that pair and remains unimplemented.
  **Result: still 50/67, but VRAM blocking is now a safely-verified
  building block for the line-0-quirk work.**

  **Fifth follow-up, same session: line 0's special post-power-on
  timing.** Reverse-engineered the qualitative schedule directly from
  `lcdon_timing-GS.s`'s own 24-entry expectation table (all 3 passes,
  cross-referencing LY/STAT/OAM-access/VRAM-access at every sampled
  M-cycle - the test's own "nops XXX" comments turned out to label
  M-cycle counts, not T-cycles, once cross-checked against the actual
  instruction timings between reads) rather than trusting the `.s`
  file's own paraphrased "PPU is late by 2 T-cycles" comment, which
  didn't match the derived numbers closely enough to be usable
  directly. The real schedule: right after LCDC's power-on write,
  line 0 spends a normal 80T in a "fake mode 0" phase (STAT reads
  mode 0, but OAM/VRAM stay unblocked - it behaves like mode 2's
  length without being mode 2), then goes straight to mode 3
  (TRANSFERMODE), *skipping mode 2 (OAM search) entirely* - skipping
  the search itself is why OAM has real, uninitialized-looking data
  on the very first frame after power-on on real hardware. Mode 3
  then runs its normal 172T (plus the usual SCX penalty), and the
  line's closing HBlank is shorter than normal before LY increments
  to 1 and every following line proceeds completely normally.

  The *exact* size of that HBlank shortening is where hand-derivation
  from the table's absolute M-cycle offsets (matching this project's
  established, repeatedly-learned lesson about this exact kind of
  arithmetic) proved unreliable: a first symbolic derivation of the
  alignment between the LCDC write and the table's own "M-cycle 0"
  reference point predicted a 4T-short HBlank (200T HBlank, 452T
  total), which the harness's own Mooneye DUMP_HRAM state (fail_round/
  fail_expect/fail_actual, which the test conveniently writes to
  fixed, dumpable HRAM addresses on the very first mismatch) showed
  was wrong by exactly one M-cycle once actually tested. Corrected
  empirically against that same feedback instead of re-deriving by
  hand: the real deficit is 8T, not 4T (448T total, not 452T) -
  attributed here entirely to the closing HBlank (196T instead of
  204T) since that's the only unconstrained parameter that reproduces
  the correct total; the exact internal mode0->mode3 and mode3->hblank
  boundary *positions* within line 0 aren't independently confirmed
  by any check that currently passes (see below).

  Implemented as a small 3-state flag (`lcdOnLine0Phase`: 0 = normal,
  1 = in the fake-mode-0 phase headed for mode 3, 2 = in the
  shortened closing HBlank headed for LY++), set by the LCDC power-on
  write handler and consumed by two small intercepts in `cycleLength()`'s
  existing mode-transition state machine - no change to line 1 onward,
  which already goes through the same, unmodified general-case code.
  Verified directly against the DUMP_HRAM state described above:
  **all 24 of the test's `LY` checks now pass exactly** (previously
  wrong from the very first read after M-cycle 20). Full regression sweep unchanged
  at 50/67 (byte-identical composition), mbc1/mbc5 at the known 18/21
  baseline, `cpu_instrs.gb` still 11/11, and 5 of 6 real ROMs
  byte-identical to the prior round's dumps; Pokemon Red's dump
  differed, confirmed benign via a sequential run of dumps a few
  thousand instructions apart (same title screen, coherent bobbing-
  logo animation across the sequence - just a different animation
  phase landing at the fixed 5M-instruction snapshot, the same
  pattern already seen and verified benign for this exact ROM earlier
  in this project's history), not a rendering regression.

  `lcdon_timing-GS` as a whole still fails overall - past the LY
  checks, its STAT sub-check fails next, on **line 1** (the ordinary
  line immediately following the special power-on line 0, not line 0
  itself): at M-cycle 176 (relative to the LCDC write), my
  implementation has already left mode 3 for HBlank, while the test
  expects mode 3 to still be active there, only yielding at M-cycle
  177 - i.e. real hardware's mode 3 on *this specific line* lasts
  176T, not the standard 172T my engine (correctly) produces.
  **Correction to an initial misreading of this same data**: earlier
  drafting of this entry had the direction backwards (claimed my
  mode 3 was holding 4T *too long*, and speculated it was a general
  bug shared with `hblank_ly_scx_timing-GS`). Re-checking the raw
  fail_expect/fail_actual bytes directly shows the opposite: my mode 3
  ends 4T *too early* here, and `hblank_ly_scx_timing-GS`'s own
  documented expectations (SCX=0 => 51 M-cycles/204T of HBlank, which
  only holds together with a standard 80T mode 2 and 172T mode 3 to
  sum to a normal 456T line) actually *confirm* this project's
  existing, general-case 172T mode 3 is correct - ruling out the
  general-bug theory and the `hblank_ly_scx_timing-GS` connection
  entirely. What's actually going on: line 1 - specifically the one
  immediately after a power-on line 0, not ordinary line 1s in
  general - needs a handful of extra T-cycles in mode 3 that this
  project doesn't model, plausibly a residual settling effect from
  line 0's own glitched mode 3 (skipped OAM search, no primed
  fetcher/FIFO state to carry over cleanly into the very next line).
  Given the size of this session's line-0 investigation already, this
  narrow, LCD-on-adjacent-only quirk was deliberately left for its own
  pass rather than chased further here - it's a much smaller, more
  specific gap than initially (mis)framed above. **Result: still
  50/67 (this fix doesn't flip `lcdon_timing-GS`'s own pass/fail,
  since its STAT check still fails - just on line 1 now, for a
  precisely-identified reason, rather than line 0 for a completely
  unmodeled one), but line 0's own timing is now real, verified-
  correct, hardware-accurate behavior.**

  **Sixth follow-up, same session: `tima_write_reloading`/
  `tma_write_reloading`.** Both fixed. A prior session had already
  correctly modeled TIMA's basic overflow-reload delay (reads as $00
  for one M-cycle, then TMA - `tima_reload.gb` already passed) and a
  TIMA write during that same M-cycle correctly cancelled the pending
  reload (the written value sticking instead), but a first attempt at
  this session's own re-investigation (hand-derived, guessing the
  boundary was exactly at `timaReloadPending==1`) had zero effect once
  actually tested - a reminder, yet again, that this project's own
  M-cycle-offset arithmetic keeps being too easy to get wrong by hand.
  Rather than keep guessing, cross-checked against SameBoy's own
  timer implementation (a known-accurate reference core, fetched and
  read directly rather than trusted from memory): its
  `tima_reload_state` machine has *two* consecutive M-cycles after
  overflow, not one - the first (`RELOADING`, matching this project's
  existing "$00 for one M-cycle" behavior) still lets a TIMA write
  cancel the reload as before, but the *second* (`RELOADED`, once
  TIMECNT has already silently become TMA and reads look completely
  ordinary again) silently ignores any TIMA write instead. Added a
  second countdown (`timaWriteBlocked`, 4T/one M-cycle, starting the
  instant `timaReloadPending` reaches 0) that gates exactly this
  extra, otherwise-invisible window. Also ported SameBoy's TMA-write
  behavior, which this project hadn't modeled at all: a TMA write
  during *either* of these two windows immediately updates TIMA to
  match the newly-written value too (real hardware's reload circuitry
  reads TMA live while a reload is in flight, rather than only
  latching it at the moment of overflow).

  Verified: both tests now PASS outright (previously both FAIL) -
  Mooneye's own success signature, not just a partial-check
  improvement. Full acceptance sweep **50/67 -> 52/67**, with every
  other test's PASS/FAIL unchanged (only these two flipped). mbc1/mbc5
  at the known 18/21 baseline, `cpu_instrs.gb` still 11/11, and all 6
  real ROMs byte-identical to the prior round's dumps - this only
  touches the narrow TIMA-write-during-reload edge case, which none
  of these ROMs exercise in a way that shows up in a single
  fixed-instruction-count snapshot, so a fully clean diff here is
  expected rather than surprising.

  **Seventh follow-up, same session: mode 3's SCX penalty was a real
  bug, plus a promising-but-inconclusive interrupt-timing lead on
  `hblank_ly_scx_timing-GS`.** Investigating that test's own
  documented SCX-penalty table ("(SCX mod 8) = 0 => nothing, 1-4 =>
  50 M-cycles of HBlank, 5-7 => 49") surfaced two real, independent
  bugs in the existing SCX-length code from earlier this session:
  1. **The penalty itself was linear (`SCX mod 8` T-cycles), not the
     real step function.** The background pixel FIFO only ever
     resumes output in 4-pixel-aligned chunks, so a non-multiple-of-4
     discard still costs a full extra 4T slot - i.e. `SCX mod 8` of
     1-4 costs a flat 4T, 5-7 costs a flat 8T, not a smooth per-value
     gradient. Fixed via a small `Mode3ScxPenalty()` helper (round
     `SCX mod 8` up to the next multiple of 4).
  2. **The penalty was never subtracted back out of HBlank.**
     `HBLANK_CYCLES` was added as an unconditional flat 204T every
     line, regardless of whatever penalty mode 3 had just taken - so
     any line with a non-zero `SCX mod 8` grew to 460T or 464T total
     instead of staying at the real, fixed 456T. This is a genuine,
     independent bug (verified by direct T-cycle tracing of mode 3's
     actual start/end points against a synthetic SCX=1 case) that
     would have caused real, silent frame-timing drift in any game
     that scrolls to a non-8-aligned SCX value - which is most of
     them. Fixed by caching the penalty when mode 3 begins
     (`currentLineMode3Penalty`, since SCX can change again before
     mode 3 ends) and subtracting the same amount back out of
     `HBLANK_CYCLES` when it ends.

  Both fixes verified regression-free on their own (full sweep
  unchanged, mbc1/mbc5 at 18/21, `cpu_instrs.gb` 11/11, 5/6 real ROMs
  byte-identical - Pokemon Red's dump differed, confirmed benign via
  the same sequential-dump technique used earlier this session, same
  title-screen animation just at a different phase).

  Getting `hblank_ly_scx_timing-GS` to actually pass needed one more
  piece: empirically, its SCX=0 case only passed with an *additional*
  4T delay between mode 0's STAT bits becoming visible and the mode=0
  STAT interrupt actually firing (measured via a HALT-based round
  trip whose every other component - interrupt dispatch, `ADD SP,e`,
  `RET`, `CALL`, `NOP`, `LD A,(HL)` - is independently pinned by other
  already-passing tests, leaving this as the one unaccounted-for gap).
  That delay is a real, measured finding, but applying it broke
  `intr_2_0_timing` (which measures the interval between mode 2's and
  mode 0's STAT interrupts, and had been passing): delaying mode 0's
  interrupt alone stretches that interval by 4T. Delaying mode 2's
  interrupt too, bundled with mode 0's, was tried as a same-round fix
  and *appeared* to have zero effect on `intr_2_0_timing`'s result -
  **that specific claim turned out to be wrong; see the ninth
  follow-up further below, which re-tested mode 2's delay in isolation
  and found it has a large, real effect (and made genuine progress on
  a different test) - it just doesn't happen to fix `intr_2_0_timing`
  either way.** Given
  `intr_2_0_timing` was a currently-passing test, the interrupt-delay
  change was reverted entirely (confirmed by re-running the full
  regression sweep with it removed: byte-identical to the pre-this-
  round baseline) rather than accepted as a net-zero trade with a
  hidden regression. The two SCX fixes above were kept (independently
  justified, zero regression risk on their own); `hblank_ly_scx_timing-GS`
  itself remains failing, but with a precisely quantified, reproducible
  4T gap and a documented dead end (delaying mode 2 too) for whoever
  picks this up next - a considerably narrower unknown than "still-
  unidentified gap in the interrupt-dispatch-to-polling-read latency
  chain" was before this round. **Result: acceptance sweep unchanged
  at 52/67 (no new pass, no regression), but the SCX-penalty
  compensation bug fix is real, independently valuable progress kept
  from this investigation, and the interrupt-timing dead end is now
  on record instead of needing to be rediscovered.**

  **Eighth follow-up, same session: mode 3's per-sprite (OBJ) length
  penalty, previously entirely unmodeled.** Implemented real hardware's
  documented "OBJ penalty algorithm" (Pan Docs, gbdev.io/pandocs/
  Rendering.html#obj-penalty-algorithm - fetched and read directly
  rather than hand-recalled, given how easy this project has found it
  to get exactly this kind of detail subtly wrong from memory): OBJs
  intersecting the current line are considered leftmost-to-rightmost
  (capped at the first 10 in OAM order, then sorted by screen
  position); each incurs a flat 6T fetch cost, plus - only for the
  *first* OBJ landing in any given BG tile this line - up to 5 more T
  for how far into that tile its leftmost pixel falls (tracked via a
  small per-line "visited tiles" list); a second OBJ landing in an
  already-considered tile skips that extra part. An OBJ with OAM X of
  exactly 0 (fully off the left edge) is a documented exception,
  always contributing a flat 11T - empirically found via Mooneye's
  own `intr_2_mode0_timing_sprites.gb` (which sweeps sprite count,
  position, and grouping in one ROM) that this exception *also*
  participates in the same tile-sharing discount as everything else
  (first off-screen OBJ costs 11T, a second one costs just the
  ordinary 6T) - Pan Docs' wording doesn't make this explicit, and a
  naive flat-11-per-sprite reading (which was tried first) matched
  only the simplest single-sprite case, not the N=2..10-sprites-at-
  X=0 block. Verified this whole formula by hand against all 10 of
  that block's cases plus 8 more sweeping X=1..8 with 10 sprites each
  (18 independent data points, all exactly matching once the right
  T-cycle-to-test-unit relationship was found) before touching any
  code - the same "verify precisely by hand against real ground truth
  before implementing" discipline this project has repeatedly needed
  after getting cycle arithmetic wrong from memory or a paraphrase.

  Window tiles aren't modeled (this project has no window-fetch-timing
  model at all yet, and this test never enables the window, so it
  wasn't needed to pass this specific ROM).

  `intr_2_mode0_timing_sprites.gb` itself still fails - but on its
  very first, simplest case (a single sprite at X=0), which is
  *unaffected* by whether the sprite-penalty formula is right at all
  (confirmed directly: it fails identically with the sprite penalty
  completely disabled, i.e. this project's pre-existing zero-sprite-
  penalty baseline). This test catches its timing reference via the
  exact same "HALT waiting on a mode=2 STAT interrupt" mechanism as
  `hblank_ly_scx_timing-GS`/`intr_2_0_timing` from the follow-up
  above, so this is very likely the *same*, not-yet-understood
  interrupt-timing gap blocking it too, rather than anything specific
  to sprites - consistent with, and adding a third data point to, the
  dead end already on record. Verified regression-free regardless:
  full acceptance sweep unchanged at 52/67, mbc1/mbc5 at 18/21,
  `cpu_instrs.gb` 11/11, and all 6 real ROMs byte-identical to the
  prior round's dumps (this time including Pokemon Red, which had
  shifted in several previous rounds this session - a clean diff
  across the board). **Result: acceptance sweep unchanged at 52/67,
  but real, hand-verified sprite-penalty modeling is now in place for
  when the underlying interrupt-timing gap is eventually found - at
  which point this test (and likely `hblank_ly_scx_timing-GS`/
  `intr_2_0_timing` alongside it) should just start passing without
  further sprite-specific work.**

  **Ninth follow-up, same session: revisited the interrupt-timing dead
  end with a fresh angle, and corrected a wrong finding from earlier
  in this session.** The seventh follow-up above claimed "delaying
  mode 2's STAT interrupt by the same 4T as mode 0's measured zero
  effect on `intr_2_0_timing`" - re-tested that in isolation this
  round (mode 2 delayed, mode 0 left alone; the earlier round only
  ever tested it bundled with mode 0's delay) and that claim was
  simply wrong, apparently a testing mistake rather than a real
  result: delaying mode 2 alone has a large, real effect. It **breaks**
  `intr_2_0_timing` on its own (previously passing, now fails the same
  way mode 0's delay alone does) - so the two delays are *not*
  independent/additive the way the seventh follow-up assumed. It also
  makes real, measurable progress on `intr_2_mode0_timing_sprites.gb`:
  with mode 2 delayed, testcases #00-#09 (all ten of the "N sprites at
  X=0" block) now pass outright (confirmed via the ROM's own on-screen
  "TEST #NN FAILED" readout, the most direct ground truth available -
  more reliable than guessing WLA-DX's `\@` numbering scheme, which
  this round's investigation also found starts at 0, not 1, contrary
  to an unstated assumption made while first reading the ROM's HRAM
  state). Failure moves to testcase #0A (10, the first "10 sprites at
  X=1" case) - real forward progress, not a wash, even though the test
  as a whole still fails.

  Combining both delays (mode 0 *and* mode 2, each independently
  delayed by 4T) was tried too, on the theory that keeping them
  matched might preserve whatever relationship `intr_2_0_timing`
  depends on: it does *not* - `intr_2_0_timing` fails identically
  (byte-for-byte the same wrong register state) whether mode 0 is
  delayed alone or both are delayed together, and `intr_2_mode0_timing_sprites`
  stops at the exact same testcase #0A either way (mode 0's delay has
  no effect on it at all - consistent with that test's measurement
  never touching mode 0's interrupt in the first place, see below).
  So the two tests' requirements don't reconcile via any combination
  of flat 4T delays tried so far.

  The likely reason `intr_2_0_timing` resists both individually and
  combined: unlike the other three tests here, its *second* half
  (`setup_and_wait_mode0`) doesn't poll STAT directly or use HALT - it
  arms mode 0's STAT interrupt and busy-loops ("xor a; ld b,a; -inc b;
  jr -") waiting to be *interrupted* by it mid-loop, then reads
  whatever B reached. That's a third distinct measurement mechanism
  (HALT-catch, direct-STAT-poll, and now interrupt-mid-active-loop),
  and it may have its own timing subtlety independent of the "STAT
  bits visible vs. interrupt fires" gap the other three tests seem to
  share. Given three separate flat-4T-delay combinations were tried
  and none reconciles all four tests at once, further guessing wasn't
  pursued this round - the delay experiment (all of it) was reverted
  in full and confirmed byte-identical to this session's last clean
  commit via a fresh regression sweep, so no code changes came out of
  this follow-up. **Result: no code change, acceptance sweep unchanged
  at 52/67, but a corrected and considerably more precise picture of
  the interrupt-timing gap for whoever picks it up next** - mode 0
  and mode 2 both plausibly need their own 4T interrupt-firing delay
  (real, reproducible effects, not guesses), `intr_2_0_timing`'s
  interrupt-mid-active-loop mechanism is the likely odd one out and a
  good next place to look, and `intr_2_mode0_timing_sprites`'s
  testcase #0A (10 sprites at X=1, not X=0) is now a precise, narrow
  next data point once mode 2's delay is safe to apply.

  **Tenth follow-up, same session: pinned down exactly why
  `intr_2_0_timing` resists the delay fix, with hard cycle-level
  data.** Added a trace at the actual interrupt-dispatch entry point
  (not just the STAT rising-edge, which fires every line regardless of
  whether anything is watching) to directly measure the T-cycle gap
  between catching mode 2's interrupt and catching mode 0's, for both
  of this test's two rounds (`test_iter 4` producing D, `test_iter 3`
  producing E - the two nop-delay values differ by exactly one M-cycle
  between them). **With no delay applied at all** (this test's own
  currently-passing baseline), that gap is a fixed 252T for *both*
  rounds regardless of their different nop counts - expected, since
  mode 0's real firing instant is set entirely by the PPU's own
  mode2+mode3 duration and doesn't care what the CPU is doing
  in-between; the nop count only changes how much of that fixed window
  is left for the measurement loop to run in, not the window itself.

  **With both mode 2 and mode 0 delayed by 4T** (the combination
  tried in the ninth follow-up), the gap does *not* shift by a uniform
  amount for both rounds, which is the actual reason this test breaks:
  round A's (`test_iter 4`, producing D) gap grew by 8T instead of the
  expected net 4T (mode 0's own +4T, since mode 2's +4T dispatch delay
  shifts the measurement loop's own start later by the same amount and
  should cancel out) - checked directly against the ROM's actual D/E
  register values via `setup_assertions`/`assert_d`/`assert_e`, not
  inferred: D lands on $08 (one whole extra loop iteration beyond the
  expected $07). Round B's (`test_iter 3`, producing E) gap shifted by
  exactly the expected 4T, and E is still correctly $08 - **only round
  A breaks**, and the two rounds differ from each other only in that
  one-M-cycle nop-count. This rules out "the model is fundamentally
  wrong" (a genuinely broken model would be expected to break both
  rounds, or neither) and points at something alignment/parity-
  sensitive: `intr_2_0_timing`'s own measurement loop ("inc b; jr -")
  has two different check-points 4T apart in its 16T period, and
  exactly which one a given delayed interrupt instant rounds up to
  can flip depending on sub-4T-scale alignment this project's engine
  has no way to represent - `cycleLength()` only ever advances in
  whole CPU-instruction-cost chunks (4, 8, 12T...), never at finer
  granularity, so two scenarios that differ by less than one CPU
  M-cycle in when an event *conceptually* happens can't be told apart
  here. This is a plausible, mechanically consistent explanation for
  why three flat-4T-delay combinations all failed this one test while
  helping the other two - not certain, but well-supported by the
  round-A-breaks/round-B-doesn't asymmetry, which a purely-wrong-model
  theory doesn't explain as cleanly. Reverted again in full (confirmed
  byte-identical via a fresh sweep); this remains a genuine, currently
  unimplementable-without-deeper-architecture-work gap rather than a
  simple bug, so it's being set aside for now rather than continuing
  to spend this session's remaining effort on it. **Result: no code
  change, sweep still 52/67, but the interrupt-timing investigation
  now has a concrete, falsifiable explanation on record (sub-4T
  alignment sensitivity in `intr_2_0_timing`'s specific measurement
  loop) instead of an open "doesn't reconcile" mystery - worth
  revisiting if this project ever moves to finer-than-M-cycle PPU/CPU
  interleaving, but not a good use of further effort at the current
  architecture's resolution.**

  **Eleventh follow-up, same session: real, verified progress on the
  line-1-after-power-on mode-3-length quirk, plus a newly-found deeper
  layer underneath it.** The eighth follow-up (this session) narrowed
  `lcdon_timing-GS`'s remaining STAT-sub-check failure to line 1 -
  specifically the one right after a power-on line 0 - needing mode 3
  to hold 4T longer (176T, not the standard 172T) before yielding to
  HBlank. Implemented directly: a one-shot flag
  (`lcdOnLine1ExtraPending`) set the instant line 0's own special
  handling finishes, adding the 4T to the very next line's mode 3
  length and then clearing itself, so every line after that goes
  through the ordinary, unmodified code path. Verified via Mooneye's
  own `DUMP_HRAM`-readable failure state: the STAT sub-check's
  previously-failing index (the line-1 mode-3-length check) now
  matches exactly. Confirmed regression-free the usual way: full
  acceptance sweep unchanged at 52/67 (this doesn't flip
  `lcdon_timing-GS` to PASS outright, only moves its own internal
  failure point further - see below), mbc1/mbc5 at 18/21,
  `cpu_instrs.gb` 11/11, and all 6 real ROMs byte-identical to the
  prior round's dumps.

  Moving past that check exposed a **second, separate, deeper**
  timing gap on line 0 itself, not line 1: right at the boundary
  where line 0's initial "fake mode 0" phase hands off to mode 3 (the
  M-cycle position the STAT test's pass2/pass3 straddle - see the
  fifth follow-up earlier in this document for the schedule this
  refers to), the transition needs to become visible one read-instant
  earlier than the current, already-independently-verified-correct
  80T fake-mode-0 duration places it. Tried shifting that duration
  (compensating with an equal-and-opposite change to the immediately
  following mode 3's own length, to keep its own already-correct end
  point and the line's total length both unchanged) across several
  candidate values (-1T, -2T, -3T, -4T, with matching compensation).
  Only a full -4T/+4T pair actually moved the target boundary (matching
  this project's now-familiar finding that `VideoCyclesLeft`'s
  crossing point can only shift in whole-M-cycle steps when the CPU's
  own instruction-cost charges are always multiples of 4T) - but doing
  so unexpectedly broke the *next* boundary down the line (mode 3's
  own end point, previously correct), even though the arithmetic
  predicts it should cancel out exactly (shorter fake-mode-0 + longer
  mode 3 = same total, same absolute end point). It doesn't, in
  practice - almost certainly because the exact overshoot carried
  across the first boundary (how far `VideoCyclesLeft` actually laps
  past zero, which depends on precisely which CPU instruction's charge
  causes the crossing, not just the arithmetic sum) differs between the
  two threshold values, so the two boundaries don't shift as a clean,
  independent pair the way flat algebra suggests. None of the
  combinations tried get both boundaries right at once. Reverted this
  specific piece back to the plain, original 80T/172T values (keeping
  only the verified `lcdOnLine1ExtraPending` fix); confirmed via a
  fresh full regression sweep and all 6 real ROMs that the kept state
  is byte-identical to the fully-verified baseline. **Result: real,
  net-positive, verified progress (the line-1 mode-3-length quirk is
  fixed and kept), plus a precisely located next boundary to chase -
  the fake-mode-0-to-mode-3 handoff within line 0 itself needs to
  land 1 M-cycle earlier without disturbing mode 3's own already-
  correct end point, which needs either finding the actual missing
  ingredient (not just threshold-shifting) or accepting some overshoot-
  tracking refinement this project's current `VideoCyclesLeft` model
  doesn't yet carry.**

  **Twelfth follow-up, same session: built Mooneye's *full* test suite
  (`make all`) for the first time, rather than just the previously-
  explored `acceptance/{ppu,timer,interrupts}` + top-level subset, and
  fixed all 3 newly-discovered failures.** This surfaced three entirely
  unexplored categories this project had never run before:
  `acceptance/bits`, `acceptance/instr`, and `acceptance/oam_dma`. Of
  the 7 tests in these categories, 4 were already passing
  (`mem_oam`, `reg_f`, `daa`, `basic`) and 3 were failing:

  - `reg_read.gb` - `$FF46` (the DMA register) had no `ReadMEM` case at
    all and fell through to the generic I/O default, always reading
    back `0x00` regardless of what was written. Real hardware's DMA
    register is a plain write-then-read-back latch - always readable,
    fully reflecting the last byte written, independent of any transfer
    in progress. Fixed by adding a `DMAREG` state variable (set at the
    top of `doDMA()`) and the missing `ReadMEM` case.

  - `sources-GS.gb` - failed at its `test_fe00` case. Cross-referencing
    the test's own data-setup (`ram_pattern_1` written to both `$C000`
    and `$DE00`, `ram_pattern_2` to `$DF00`) against the adjacent
    already-passing `test_e000` (source `$E000`, echoing WRAM `$C000`)
    showed real hardware's OAM DMA source-address decoder doesn't fully
    decode the `$FE00`-`$FFFF` range: source addresses there wrap down
    to `$DE00`-`$DFFF` (subtract `$20` from the high byte) instead of
    reading literal OAM/HRAM/IO. Fixed in `doDMA()`'s computation of
    `dmaSourceBase`.

  - `unused_hwio-GS.gb` - a DMG-mode-specific test (its own header notes
    it's expected to pass on DMG/MGB/SGB/SGB2 and fail on CGB/AGB/AGS)
    checking that every unused bit in implemented `$FFxx` I/O registers,
    and every genuinely unmapped `$FFxx` address, reads back as `1`.
    Found and fixed four separate gaps, each isolated via a fresh
    `DUMP_HRAM` readout pinpointing exactly which register/mask the test
    was currently parked on:
    - `P1`/`$FF00`: the joypad-select write paths (`b==0x10`/`b==0x20`)
      did `P1 &= 0x0F`, clobbering the always-1 unused bits 6-7 down to
      0. Fixed by OR-ing `0xC0` into the read path (matching the
      existing pattern already used for `SERIALCONTROL`/`$FF02`).
    - `TAC`/`$FF07`: unused bits 3-7 read back as whatever `TIMCONT`
      held (usually 0) instead of forced 1s. Fixed with a `| 0xF8` on
      read.
    - Genuinely unmapped `$FFxx` I/O (`$FF03`, `$FF08`-`$FF0E`, `$FF15`,
      `$FF1F`, `$FF27`-`$FF29`, and most of `$FF4C`-`$FF7F`): the
      switch's `default` case returned `0x00` (real open-bus behavior on
      this hardware is `0xFF`). Fixed by changing the default to `0xFF`.
    - The GBC-only registers in `$FF4C`-`$FF7F` (`KEY1`, `VBK`, `HDMA5`,
      `BCPS`/`BCPD`, `OCPS`/`OCPD`, `SVBK`) had read handlers that
      always exposed their backing state (OR'd with the documented
      unused-bit mask) regardless of `GBC_MODE` - correct on real CGB
      hardware, but wrong for this DMG-mode test ROM, where those
      addresses are entirely unmapped on real hardware and must read
      `0xFF` like any other unmapped address. Their write-side handlers
      were already correctly gated on `GBC_MODE` (confirmed by reading
      the surrounding code); only the read side needed the same gating,
      falling back to `0xFF` when `!GBC_MODE`.

  All 7 tests in the newly-discovered categories now pass (was 4/7).
  Confirmed regression-free: full acceptance sweep byte-identical to the
  prior round's baseline (52/67, diffed line-by-line against the saved
  sweep file), mbc1/mbc5 at 18/21 (the 3 failures -
  `multicart_rom_8Mb`/`rom_16Mb`/`rom_8Mb` - independently confirmed
  pre-existing by rebuilding and re-running against a stashed pre-change
  copy of `emu.c`, not something this round introduced), `cpu_instrs.gb`
  11/11, and all 6 real ROMs re-dumped and visually compared against
  this session's earlier dumps of the same ROMs at the same cycle count
  - identical framebuffers throughout (including Pokemon Crystal's
  "designed only for use on the Game Boy Color" splash screen at the
  dump's cycle count, confirmed pre-existing via an earlier round's saved
  dump rather than a new regression from the `GBC_MODE`-gating change
  above, since that change only affects reads on a cart where `GBC_MODE`
  is already 0).

  **Thirteenth follow-up, same session: fixed MBC2's RAMG/ROMB address
  decoding, found by running `emulator-only/mbc2` for the first time.**
  With the full Mooneye suite now being built (twelfth follow-up above),
  `emulator-only/mbc2` turned out to have two failures alongside its 5
  existing passes: `bits_ramg.gb` and `bits_romb.gb`. Both write across
  the *entire* `$0000`-`$3FFF` range (not just their own nominal half)
  checking exactly which addresses in that range actually gate RAM
  enable (RAMG) versus select the ROM bank (ROMB). Every other MBC this
  project implements (MBC1/3/5) decodes that split the same way: address
  bit 14 divides it cleanly into `$0000`-`$1FFF` (RAMG) and
  `$2000`-`$3FFF` (ROMB), which is exactly how `WriteMEM`'s existing
  `loc <= 0x1FFF` / `else if (loc <= 0x3FFF)` structure was written.
  MBC2 is different at the hardware level: real MBC2 only has *one*
  address line (A8, bit 8) wired into the mapper for this decision, not
  the wider bit-14 split - so e.g. `$0100` (A8=1) is actually ROMB, and
  `$2000` (A8=0) is actually RAMG, both the *opposite* of what the
  generic bit-14 split would say. This project's existing MBC2 handling
  sat inside that same generic split (RAMG only recognized in
  `$0000`-`$1FFF`, ROMB only in `$2000`-`$3FFF`), so it was silently
  wrong across roughly half the addresses a real game could use for
  either register - it happened to still work for `$0000`-`$00FF` and
  `$3F00`-`$3FFF` (the addresses actual games conventionally use), which
  is presumably why this went unnoticed until a test ROM deliberately
  swept the entire range. Fixed by giving MBC2 its own dedicated branch
  ahead of the generic split, dispatching purely on `loc & 0x0100`
  across the combined `$0000`-`$3FFF` range, keeping the already-correct
  4-bit-bank/0-becomes-1 ROMB quirk and the RAMG enable/battery-save
  logic exactly as they were - just gated on the right addresses now.
  Verified via Mooneye's emulator-only/mbc2/bits_ramg.gb and
  bits_romb.gb (both now PASS, `mbc2` category now 7/7, up from 5/7).
  Confirmed regression-free: full acceptance sweep byte-identical to
  the baseline (52/67), mbc1 unchanged (10/13, same 3 pre-existing
  failures), mbc5 unchanged (8/8), `cpu_instrs.gb` 11/11, and all 6 real
  ROMs (including Kirby's Pinball Land, the one MBC2 title in the local
  real-ROM set) re-dumped and byte-identical to the prior round's
  framebuffers at the same cycle count.

  Also worth recording: building the full suite surfaced one more
  category this round, `madness/mgb_oam_dma_halt_sprites.gb` (an
  obscure HALT-bug/OAM-DMA/sprite-rendering edge case specific to the
  Game Boy Pocket). It's pathologically slow in this project's
  interpreter - roughly 30,000 instructions/second on this ROM
  specifically, versus 400,000+/second on ordinary ROMs like
  `cpu_instrs.gb` - to the point that reaching even a 1,000,000-
  instruction cap doesn't complete in 30 real seconds, let alone the
  usual 30,000,000-instruction cap. Confirmed via a throwaway git
  worktree at the prior commit that this slowness (and the resulting
  inability to reach a PASS/FAIL/hang verdict in any reasonable time)
  already existed before this round's changes - not a regression, just
  a previously-unbuilt, very obscure edge-case test this project has
  never been able to evaluate. Not chased further this round: `madness/`
  is an informal stress-test category (not `acceptance/`), and whatever
  is making this specific ROM's HALT-heavy code path so slow would need
  its own dedicated investigation separate from this round's DMA/HWIO/
  MBC2 fixes.

  **Fourteenth follow-up, same session: researched the still-stuck
  `hblank_ly_scx_timing-GS`/`intr_2_mode0_timing_sprites`/`intr_2_0_timing`
  interrupt-timing cluster against Mooneye GB's own reference emulator
  source (the actual implementation these test ROMs were built from,
  not just their black-box pass/fail behavior) - found a real, cited
  mechanism, but the direct translation attempt didn't work and was
  reverted; the investigation nonetheless narrowed the problem
  considerably.** Fetched and read
  `core/src/hardware/ppu.rs` from github.com/Gekkio/mooneye-gb directly
  (not summarized secondhand). Its `emulate()` function contains this,
  verbatim:
  ```rust
  self.cycles -= 1;
  if self.cycles == 1 && self.mode == Mode::AccessVram {
    // STAT mode=0 interrupt happens one cycle before the actual mode switch!
    if self.stat.contains(Stat::HBLANK_INT) {
      ctx.request_t34_interrupt(InterruptLine::STAT);
    }
  }
  ```
  Confirmation, straight from the authoritative source, that mode 0's
  STAT interrupt is *requested* a full M-cycle before the STAT
  register's visible mode bits actually flip to 0 - a genuine,
  deliberate hardware quirk, not a simplification or a test-ROM
  idiosyncrasy. This matters because Mooneye's own model can express
  this exactly: `emulate()` is called once per single M-cycle, so
  "1 cycle before the switch" is always a clean, unambiguous instant.

  Implemented the closest analog in this project: added an `extraMode0`
  parameter to `UpdateStatLine()` (mirroring the existing `extraMode2`
  parameter already used for the analogous mode-2-at-VBlank-entry
  quirk), and a check in `cycleLength()`, before `VideoCyclesLeft -=
  sysCycle`, firing it when `videoMode == TRANSFERMODE && VideoCyclesLeft
  > 4 && (VideoCyclesLeft - sysCycle) <= 4` - i.e. "this call's
  consumption is about to carry the countdown from above 4 down to 4 or
  less while still in mode 3," the closest this project's architecture
  can get to Mooneye's exact single-M-cycle checkpoint.

  Empirically, this **broke `intr_2_0_timing`** (a previously-passing
  test) **while making zero observable difference to
  `hblank_ly_scx_timing-GS`** (still fails, byte-identical `DUMP_HRAM`
  output to before the change) - confirmed via temporary debug tracing
  added to the new branch (`fprintf` on every firing, showing `LY`,
  `LCDSTATUS`, `sysCycle`, and `VideoCyclesLeft`). The trace explains
  why: at the exact point `hblank_ly_scx_timing-GS`'s critical mode-3
  end falls, the instruction executing there has `sysCycle=12` (a
  single 3-M-cycle-cost `cycleLength(12)` call, not split into
  individual 4T chunks), and `VideoCyclesLeft` is 8 going into it - so
  the new "1 M-cycle early" checkpoint (`VideoCyclesLeft` crossing 4)
  and the real mode-switch checkpoint (`VideoCyclesLeft` crossing 0)
  both fall *inside that same lumped call*, meaning the "early" fire
  and the real transition's own `UpdateStatLine` call both run within
  the same C function invocation - the same CPU-instruction boundary -
  so nothing about when the interrupt becomes observable to a polling
  loop actually moves. This is a precise, evidenced instance of the
  architectural gap the whole cluster has been running into all
  session: Mooneye's (and real hardware's) reference model evaluates
  the PPU state machine at true single-M-cycle granularity, with no
  exceptions, while this project's `cycleLength()` is called with a
  single instruction's *entire* multi-M-cycle cost in one lump at most
  call sites (`cycleLength(8)`, `cycleLength(12)`, `cycleLength(20)`,
  etc. - a `grep` this round found dozens of these) - only the specific
  instructions this session's earlier rounds already identified as
  needing sub-instruction accuracy (`CALL`/`PUSH`/`POP`/`RST`/`JP`/
  `ADD SP,e`/OAM-DMA) were ever split into individual `cycleLength(4)`
  calls. Any timing event that needs to land strictly *inside* one of
  the still-lumped instructions - exactly what "1 M-cycle before a
  mode switch" needs whenever that boundary happens to fall inside one
  of them - is structurally unable to produce an observable effect in
  this architecture, no matter how correct the offset itself is.
  `intr_2_0_timing` broke instead of showing zero effect only because
  its own critical instant apparently falls on a *different*
  instruction/timing alignment where the lumped call's boundaries
  don't coincide the same way - not because the underlying idea is
  wrong there and right elsewhere.

  Reverted in full per this project's standing rule against keeping a
  net-negative (here, actually net-negative, not merely net-zero:
  broke one test, fixed none) trade - confirmed via a fresh full
  regression sweep byte-identical to the pre-round baseline (52/67)
  after `git checkout -- psx-gbc/emu.c`. **Result: no code change kept,
  but the cluster's root cause is now far better understood and
  precisely evidenced (not just suspected) - the fix isn't a threshold
  or a direction to tune, it's identifying exactly which of this
  project's still-lumped multi-M-cycle instruction opcodes fall on each
  failing test's specific critical boundary and splitting *those*
  particular `cycleLength(N)` call sites into individual `cycleLength(4)`
  calls, the same mechanical treatment already proven to work for
  `CALL`/`PUSH`/`POP`/`RST`/`JP`/`ADD SP,e` earlier this session - not
  a blanket rewrite of every instruction, which would be both far
  riskier and unnecessary (most instructions never execute at a
  timing-critical PPU boundary). Whoever picks this up next should
  start by identifying the exact opcode executing at each failing
  test's critical instant (the debug-tracing technique used this round
  - a temporary `fprintf` gated behind a `#if defined(DEBUG_*)` block,
  triggered from inside `cycleLength()`'s existing boundary checks -
  is a reusable way to find it) rather than guessing from the opcode
  table alone.**

## What's next (roughly in priority order)

1. **Sub-instruction cycle-accurate memory timing (see above) — a
   large, real gap, most of it now closed.** Currently 52 of 67
   Mooneye acceptance/ppu+timer+interrupts tests pass (up from an
   initial 11). **The entire CALL/PUSH/POP/RST/RET/JP/ADD SP,e/OAM-DMA
   sub-instruction-timing family - the cluster this project's own
   history repeatedly flagged as the highest-regression-risk part of
   this whole effort - is fully fixed**, and **the STAT-interrupt/PPU-
   mode-timing cluster is mostly fixed too** (see the detailed entries
   above for exactly what each round did): the opcode-table
   fetch-then-execute restructuring plus the DMA end-of-transfer timing
   fix; narrowing OAM DMA's bus-blocking scope to true OAM; the same
   split applied to `JP`/`JP cc` and `ADD SP,e`/`LD HL,SP+e`; making the
   STAT interrupt level-triggered with a shared signal line (fixing a
   double-fire bug and a wrong-bit bug along the way); blocking CPU
   access to OAM during PPU modes 2-3; and making mode 3's length vary
   with SCX. **What's left, each a distinct, bounded gap - not more of
   what's already fixed**:
   - `lcdon_timing-GS`/`lcdon_write_timing-GS` - VRAM access blocking
     during mode 3 (fourth follow-up above), line 0's own special
     post-power-on timing (fifth follow-up above, `lcdOnLine0Phase`),
     and line 1's own 4T-longer mode 3 (eleventh follow-up above,
     `lcdOnLine1ExtraPending`) are all now implemented and verified
     regression-free; all 24 of `lcdon_timing-GS`'s `LY` checks pass,
     and the STAT sub-check now gets past line 1 too. What's left is
     narrower still, and back inside line 0 itself: the handoff from
     line 0's initial "fake mode 0" phase to mode 3 needs to become
     visible one M-cycle earlier than the current, already-verified
     80T duration places it, without disturbing mode 3's own already-
     correct end point or the line's total length - tried directly
     shifting the threshold (with a compensating opposite shift to
     mode 3) and it doesn't cleanly work due to overshoot/carry
     effects at the boundary (see the eleventh follow-up above for the
     detail); needs either the actual missing ingredient or some
     overshoot-tracking refinement, not just threshold-tuning.
   - `intr_2_mode0_timing_sprites` - mode 3's per-sprite length
     penalty (varying with sprite count/position, on top of the SCX
     penalty above) is now implemented and hand-verified against 18
     independent cases from this test's own ROM (see the eighth
     follow-up above). Its own baseline (zero-sprite-penalty) timing
     needs the *same* mode-2-interrupt-delay fix `hblank_ly_scx_timing-GS`
     needs for mode 0 (see the ninth follow-up above) - applying it
     gets testcases #00-#09 (confirmed via the ROM's own on-screen
     "TEST #NN FAILED" readout - `\@` in this ROM's macros starts at
     0, not 1) passing outright, real progress, with failure moving to
     #0A (10 sprites at X=1). Not applied yet only because it currently
     breaks `intr_2_0_timing` (see that test's entry below) - once that
     conflict is resolved, this test should move a lot further, though
     #0A suggests there may be one more distinct gap on top.
   - `hblank_ly_scx_timing-GS` - the SCX-length fix above is real and
     kept, and this session went further: fixed two real, independent
     bugs in the SCX penalty itself (it was a linear `SCX mod 8`
     instead of the real step function, and it was never subtracted
     back out of HBlank, so any non-zero-`SCX mod 8` line silently
     grew past the real, fixed 456T line length - see the seventh
     follow-up entry above for the full detail). Even so, this test's
     own SCX=0 case (where neither fix contributes anything) still
     fails on its own, narrowed down to a precisely quantified 4T gap:
     empirically, the read needs the mode=0 STAT interrupt to fire 4T
     *later* relative to when mode 0's STAT bits become visible than
     this project currently does. A same-sized delay applied to mode
     0's interrupt alone fixes this test but breaks the currently-
     passing `intr_2_0_timing` (which measures the mode2-to-mode0
     interrupt interval); delaying mode 2's interrupt too was
     re-investigated this session (ninth follow-up above, correcting
     an earlier wrong "zero effect" finding) and found to have a real
     effect of its own (fixing 10 cases of `intr_2_mode0_timing_sprites`
     above) but *still* doesn't fix `intr_2_0_timing`, whether applied
     alone or combined with mode 0's delay - all three combinations
     tried give `intr_2_0_timing` the exact same wrong result. The
     likely reason: `intr_2_0_timing`'s second half doesn't poll STAT
     or use HALT like the other three tests here - it arms mode 0's
     interrupt and busy-loops waiting to be interrupted by it
     mid-loop, a third, distinct measurement mechanism that may have
     its own separate timing subtlety. Worth checking `intr_2_0_timing`'s
     own mechanism specifically next, rather than continuing to guess
     at flat-delay combinations for the other three tests. (This
     session also briefly suspected a connection to the mode-3-timing
     quirk found via `lcdon_timing-GS`, but that quirk turned out to
     be specific to the line right after LCD power-on, not general -
     see that entry above. Ruled out, not the same bug.)
2. ~~**Kirby's Pinball Land hang**~~ **RESOLVED this session** (re-
   confirmed a second time later in the same session, against the
   `oam_dma_start` fix too - still rendering real, active gameplay,
   not a regression back to blank). The user supplied the actual real
   ROMs this project's own testing
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

## Known limitation: serial transfer timing

Mooneye's `acceptance/serial/boot_sclk_align-dmgABCmgb.gb` (found this
session via building the full test suite for the first time) fails.
Investigated: this project's serial transfer (`onSerialControlWrite()`
in `emu.c`) completes synchronously, in the same call as the `$FF02`
write that starts it — real hardware paces a transfer bit-by-bit off
the same internal divider DIV counts from, taking on the order of 1024
M-cycles for a full byte at the internal (fastest, non-double-speed)
clock, with the exact completion time depending on the divider's phase
*since reset* rather than the time the transfer was started. This is a
materially different, currently entirely-unimplemented mechanism (a
real per-bit serial-clock model tied to the shared internal divider),
not a tunable timing constant - the same class of gap as `instr_timing.gb`
above (it also needs boot-time divider phase to be exactly right, which
compounds the same already-documented boot-ROM-timing gap). Not
pursued this session given the effort involved and the very low
real-game-compatibility payoff: this project's serial link has no
actual link-cable partner (`onSerialControlWrite()`'s own comment notes
the PSX side is always the lone "master"), so no real game's own
gameplay logic depends on this timing being exact - only Mooneye's own
synthetic test does.

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
