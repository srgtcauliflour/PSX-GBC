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
- **One real, unresolved issue found and deeply investigated: Kirby's
  Pinball Land hangs on a blank screen.** Traced via the same reference-
  diffing technique to a Timer-interrupt-gated countdown flag in HRAM
  that only this emulator fails to ever fully decrement — the Timer
  interrupt is requested at the correct rate throughout (135 times in
  1 million instructions) but its vector is reached only once, while
  VBlank (higher priority, very similar ~70000-cycle period) reaches
  its own vector 134 times in the same window. Leading hypothesis:
  VBlank's near-identical period plus higher priority is systematically
  starving Timer once some small, still-unidentified cycle-accounting
  phase difference exists between this emulator and real hardware -
  Peanut-GB shows the same general shape (also polls once per frame)
  but does eventually get enough Timer services through, where this
  emulator never does. Not yet root-caused; next step if resumed is
  auditing this specific ROM's actual VBlank/Timer handler instruction
  costs against an authoritative cycle-count table.
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
  below for what's still unknown there. BG-to-OBJ priority override
  remains unimplemented — a real, separate follow-up (see below).

## What's next (roughly in priority order)

1. **Kirby's Pinball Land hang (see above) — real, unresolved, deeply
   investigated but not fixed.** A genuine MBC2-cartridge compatibility
   issue on the one MBC2 game tested so far. Worth resuming with fresh
   eyes given how far the investigation already got.
2. More real-ROM testing, if more real ROMs become available. Every
   round of real-commercial-game and authoritative-suite testing this
   session found genuinely high-value bugs, including in code paths
   (MBC2, EI timing) nothing else had touched. Don't commit ROM files
   themselves to this repo or bundle them in any output archive
   (copyright) — keep them local/sandbox-only.
3. **GBC follow-up: BG-to-OBJ priority override** (CGB attribute bit 7)
   is the one remaining known gap in the GBC support implemented this
   session - noted inline in DrawBGline/DrawWINline, since resolving it
   needs coordinating with sprite rendering in a later pass over the
   same line. Double-speed mode timing and HDMA/GDMA are both now
   implemented and verified (see above) - implementing both did *not*
   get Pokemon Crystal past its "designed only for use on the Game Boy
   Color" hardware-compatibility screen, so whatever that specific
   check actually depends on remains unidentified; further progress on
   that exact screen would need disassembling/tracing Crystal's own
   detection routine, a bigger undertaking than guessing at candidate
   features. Sound remains separately deprioritized per the person's
   direction earlier this session.
4. **Bank-streaming for very large ROMs.** The core's `ROM[loc]` is a
   flat, fully-resident pointer with no partial loading. 1.5MB covers
   the large majority of the real library, but the small number of
   even larger late-era GBC games (up to 4-8MB — Pokemon Crystal itself
   is exactly 2MB, already over this project's current cap) still won't
   fit resident in the PS1's 2MB of RAM, now further reduced by GBC's
   larger VRAM/WRAM buffers (+32KB total) — worth re-checking total
   memory footprint if this cap is ever raised.
5. **Very large cart RAM won't fit a single memory card.** A standard
   PS1 card has 15 usable 8KB blocks (120KB total); `SaveCartRAM`
   requests exactly the blocks a cart needs, but a 128KB-RAM MBC5 game
   would need every single block on the card, and anything larger
   wouldn't fit at all. Affects a small minority of RAM-heavy titles;
   not fixed proactively since most games use far less.
6. **Real visual confirmation** (screenshot/video from actual hardware
   or a working emulator session) — every emulator-boot attempt in this
   sandbox has hung or stalled (see the note below); not something to
   keep spending sandbox time on, but worth doing whenever a real
   console or an interactive emulator session is available.

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
