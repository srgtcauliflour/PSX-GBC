// psx.c - platform layer implementation (PSn00bSDK version).
//
// REWRITE NOTE: replaces the original Psy-Q/GsLib-based psx.c (711 lines,
// see git history or /tmp/old_psx.c during the session this was written
// in). That version's rendering, CD-ROM ROM-bank loading, and GUI/menu
// code were all built on Psy-Q's GsLib (GsSPRITE/GsOT/GsSortSprite/...),
// which PSn00bSDK has no equivalent for. This version covers what the
// emulator core actually needs to run and be visible/playable: real GPU
// output (the emulated Game Boy screen, centered on the PS1's 320x240
// display) and real controller input. Deliberately NOT yet covered here
// (tracked in STATUS.md): the CD-ROM ROM-bank loader, and the GsSPRITE/
// font-based GUI (splash screen, ROM select menu) from the original
// gui.c - both need their own from-scratch rewrites against psxcd.h/
// mkpsxiso and raw psxgpu.h primitives respectively, which is real,
// separate follow-up work.

#include <stdint.h>
#include <string.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>
#include <psxcd.h>
#include <psxspu.h>
#include <sys/fcntl.h>
#include "main.h"
#include "emu.h"
#include "pad.h"
#include "psx.h"

// The one real (defining) declaration of the cartridge ROM pointer and
// the raw pad state - psx.h/pad.h only extern-declare them (see the BUG
// FIX notes added to those headers earlier this session).
BYTE *ROM;
u_long pad, lastpad;

// ---- Display setup --------------------------------------------------
#define SCREEN_XRES 320
#define SCREEN_YRES 240

// BUG FIX (tearing): this used to be a single DISPENV/DRAWENV pair, with
// Draw_Buffer() blitting straight into the region currently being shown
// on screen - visible as tearing whenever a blit happened to land while
// that same area was being scanned out to the TV. Standard PS1 double
// buffering: two vertically-stacked regions in VRAM (0-239 and 240-479),
// with disp[i]/draw[i] deliberately pointing at OPPOSITE halves for the
// same index i, so showing disp[db] while drawing into draw[db] always
// targets two different physical areas. Flipping db each frame means
// each half alternates between "currently displayed" and "safe to draw
// the next frame into" - the same pattern PSn00bSDK's own multi-buffer
// examples (e.g. examples/cdrom/cdbrowse) use.
static DISPENV disp_env[2];
static DRAWENV draw_env[2];
static int db = 0;

// ---- Game Boy screen rendering ---------------------------------------
// The emulator core hands off a raw 160x144 buffer of 2-bit shade indices
// (0=white/lightest .. 3=black/darkest - the standard BGP/OBP register
// shade semantics, already resolved by the core's own palette lookups) by
// calling Draw_Buffer() once per emulated frame, from vblank(). Converting
// that to real pixels and blitting it into the currently non-displayed
// half of VRAM (via a synchronous LoadImage(), no primitive/OT queue
// needed for a plain, non-animated blit like this) and then flipping
// which half is displayed is what actually eliminates the tearing the
// single-buffer version had.
static uint16_t gb_framebuffer[GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT];

// BGR555 grayscale shades, brightest (0) to darkest (3).
static const uint16_t gb_shade_colors[4] = {
	0x7FFF, // shade 0: white
	0x56B5, // shade 1: light gray
	0x2D6B, // shade 2: dark gray
	0x0000, // shade 3: black
};

// Shared double-buffer frame lifecycle - see psx.h. Draw_Buffer() below
// and the ROM select menu (gui.c) both build on these instead of each
// managing their own display setup.
void BeginFrame(int *outOffsetX, int *outOffsetY) {
	// Clears draw_env[db]'s half (the one NOT currently displayed) to
	// black before drawing into it, same as PSn00bSDK's own multi-buffer
	// examples do every frame - isbg+PutDrawEnv together issue a
	// synchronous GPU fill of the draw area.
	PutDrawEnv(&draw_env[db]);
	*outOffsetX = draw_env[db].clip.x;
	*outOffsetY = draw_env[db].clip.y;
}

void PresentFrame(void) {
	// Shows what was just drawn (and starts drawing the next frame into
	// what was, until this line, the displayed half) by flipping to the
	// other index.
	PutDispEnv(&disp_env[db]);
	SetDispMask(1);
	db = !db;
}

void Draw_Buffer(int *screenBuffer, unsigned short *screenBufferColor, int gbcMode) {
	int i;
	if (gbcMode) {
		// CGB colors are already stored 15-bit RGB555, little-endian,
		// bits 14-10/9-5/4-0 = B/G/R - the exact same layout the PS1 GPU
		// itself uses for direct 16bpp color, so no conversion is needed
		// at all, just a straight copy.
		for (i = 0; i < GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT; i++) {
			gb_framebuffer[i] = screenBufferColor[i];
		}
	} else {
		for (i = 0; i < GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT; i++) {
			gb_framebuffer[i] = gb_shade_colors[screenBuffer[i] & 0x03];
		}
	}

	int offsetX, offsetY;
	BeginFrame(&offsetX, &offsetY);

	// draw_env[db] points at the half of VRAM currently NOT being shown
	// (disp_env[db] shows the other half) - safe to write into without
	// tearing whatever is currently on screen.
	RECT rect;
	rect.x = offsetX + (SCREEN_XRES - GB_SCREEN_WIDTH) / 2;
	rect.y = offsetY + (SCREEN_YRES - GB_SCREEN_HEIGHT) / 2;
	rect.w = GB_SCREEN_WIDTH;
	rect.h = GB_SCREEN_HEIGHT;
	LoadImage(&rect, (const uint32_t *) gb_framebuffer);
	DrawSync(0);

	PresentFrame();
}

// Legacy GsLib-era hooks the core still calls from vblank() (PrepScreen();
// DrawBG(); RenderWorld(0,0,0);) around the Draw_Buffer() call above.
// GsLib used these for its ordering-table submit/present cycle; the
// direct-LoadImage approach here doesn't need either, so both are no-ops.
void PrepScreen(void) {}
void RenderWorld(BYTE re, BYTE gr, BYTE bl) { (void) re; (void) gr; (void) bl; }
// No-op on real PS1 hardware - the SPU generates samples in hardware
// (see UpdateAudio() below), so there's nothing for the CPU to do here.
// Exists purely so the host test harness can hook the same call site
// to accumulate a real PCM waveform for listening verification.
void AudioSampleHook(int cycles) { (void) cycles; }

// ---- Audio (GB APU -> PS1 SPU) -----------------------------------------
//
// IMPORTANT CAVEAT, stated up front: unlike essentially everything else in
// this project, this code has not been verified against actual audible
// sound - every attempt to get a screenshot/recording from an interactive
// emulator or real hardware in this sandbox has failed (see STATUS.md's
// "note on emulator verification" section), and that limitation applies
// doubly to audio, which this sandbox has no way to play back or capture
// at all. What *is* verified: the GB APU core itself (register behavior,
// triggering, DAC-off, length counters, envelope timing) via synthetic
// test ROMs checking internal state through the CPU - see STATUS.md. This
// file is the least-tested part of that chain: a principled, from-
// documentation implementation of real PS1 SPU hardware usage, not
// something confirmed to actually sound right yet.
//
// Approach: rather than mixing PCM in software and streaming it to the
// SPU (which would mean re-encoding a constantly-changing waveform to
// ADPCM every frame - expensive, and duplicates work the SPU's hardware
// already does), this drives 4 real SPU voices - one per GB channel -
// continuously updating each voice's pitch and volume to track the GB
// channel's current frequency/volume envelope, and lets the SPU's own
// hardware handle the actual waveform playback and mixing:
//   - Channels 1/2 (pulse): each of the 4 possible duty cycles (12.5/25/
//     50/75%) is pre-encoded once at init as a self-looping ADPCM sample
//     and uploaded to a fixed SPU RAM address; the voice's sample address
//     switches (with a re-trigger) when the duty cycle changes.
//   - Channel 3 (wave): re-encoded and re-uploaded to SPU RAM whenever
//     the GB's wave RAM actually changes (checked once per frame via a
//     content comparison, not on every write) - most games only change it
//     occasionally, not every frame.
//   - Channel 4 (noise): uses the SPU's own hardware noise generator
//     (SPU_NOISE_MODE) instead of an uploaded sample - real PS1 hardware
//     has one, and it's a much closer match in spirit to the GB's noise
//     channel than trying to synthesize a fixed noise sample would be.
//     The SPU's noise frequency is a single global setting shared by all
//     noise-mode voices, not per-voice - fine here since only one GB
//     channel ever uses it.
//
// All 4 voices' ADSR is configured once at init to be "flat" (instant
// attack, no decay, full sustain) so real hardware's own envelope
// generator never fights with the volume values this code writes every
// frame to track the GB's own envelope/sweep - this code is the only
// thing controlling perceived volume over time, not the SPU's ADSR unit.
//
// Known limitation: this updates once per frame (from vblank()), not
// continuously - very short notes or multiple triggers of the same
// channel within one frame won't be individually reflected, only
// whatever the channel's state happens to be at the moment each frame's
// update runs. A real, acknowledged simplification, not an oversight.

#define AUDIO_VOICE_CH1 0
#define AUDIO_VOICE_CH2 1
#define AUDIO_VOICE_CH3 2
#define AUDIO_VOICE_CH4 3

// Sample data lives in a fixed, small block near the start of SPU RAM -
// well clear of address 0 (reserved by hardware for a fixed 16-byte
// silence block) and small enough that there's no real risk of
// colliding with anything else, since nothing else in this project uses
// SPU RAM at all.
#define AUDIO_SPU_BASE      0x1010
#define AUDIO_DUTY_BYTES    32  // 2 ADPCM blocks (7 periods of the 8-step duty waveform)
#define AUDIO_WAVE_BYTES    128 // 8 ADPCM blocks (7 periods of the 32-sample GB wave table)
#define AUDIO_DUTY_ADDR(n)  (AUDIO_SPU_BASE + (n) * AUDIO_DUTY_BYTES)
#define AUDIO_WAVE_ADDR     (AUDIO_SPU_BASE + 4 * AUDIO_DUTY_BYTES)

// Real GB duty waveforms, 8 steps each, 0/1 amplitude (matches
// APU_DUTY_TABLE in emu.c - kept as a separate literal here rather than
// sharing the byte-packed table, since encoding wants one value per
// array element rather than packed bits).
static const int AUDIO_DUTY_PATTERNS[4][8] = {
	{0, 0, 0, 0, 0, 0, 0, 1}, // 12.5%
	{1, 0, 0, 0, 0, 0, 0, 1}, // 25%
	{1, 0, 0, 0, 0, 1, 1, 1}, // 50%
	{0, 1, 1, 1, 1, 1, 1, 0}, // 75%
};

static BYTE lastWaveRAM[16];
static int audioInitialized = 0;

// Encodes 28 4-bit sample values (0-15) into one 16-byte PS1 ADPCM
// block at dst. Always uses filter=0, shift=0: with source samples
// already 4-bit (16 levels), this maps 1:1 onto ADPCM's per-nibble
// range (nibble = sample-8, giving an exact, lossless encoding with no
// need for a general adaptive/predictive encoder).
static void EncodeADPCMBlock(uint8_t *dst, const int *samples4bit, int flags) {
	int i;
	dst[0] = 0x00; // filter 0, shift 0
	dst[1] = (uint8_t) flags;
	for (i = 0; i < 14; i++) {
		int lo = samples4bit[i * 2] - 8;
		int hi = samples4bit[i * 2 + 1] - 8;
		dst[2 + i] = (uint8_t) ((lo & 0x0F) | ((hi & 0x0F) << 4));
	}
}

// Builds and uploads a self-looping ADPCM sample from a repeating
// pattern of `patternLen` 4-bit values, repeated `repeats` times to
// fill exactly `blocks` ADPCM blocks (so patternLen * repeats must
// equal blocks * 28) - the exact repeat count that lets the loop point
// land precisely on a pattern boundary, avoiding any click or pitch
// wobble at the loop seam.
static void UploadLoopingSample(uint32_t spuAddr, const int *pattern, int patternLen, int repeats, int blocks) {
	// Static rather than stack-local: called every frame for the wave
	// channel whenever its data changes, so avoids repeated stack
	// allocation of a ~1KB combined buffer on a platform with limited
	// RAM - same reasoning as the RTC save buffers in emu.c.
	static uint8_t buf[8 * 16]; // largest case here is 8 blocks (the wave channel)
	static int samples[8 * 28];
	int totalSamples = patternLen * repeats;
	int i;
	for (i = 0; i < totalSamples; i++) {
		samples[i] = pattern[i % patternLen];
	}
	for (i = 0; i < blocks; i++) {
		int flags = 0;
		if (i == 0) flags |= 0x04;                 // LOOP: this block is the loop start
		if (i == blocks - 1) flags |= 0x03;        // END | REPEAT: loop back at the end
		EncodeADPCMBlock(&buf[i * 16], &samples[i * 28], flags);
	}
	SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
	SpuSetTransferStartAddr(spuAddr);
	SpuWrite((const uint32_t *) buf, blocks * 16);
	SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
}

void InitAudio(void) {
	SpuInit();
	SPU_MASTER_VOL_L = 0x3FFF;
	SPU_MASTER_VOL_R = 0x3FFF;

	int d;
	for (d = 0; d < 4; d++) {
		int samples[8];
		int i;
		for (i = 0; i < 8; i++) {
			samples[i] = AUDIO_DUTY_PATTERNS[d][i] ? 15 : 0;
		}
		UploadLoopingSample(AUDIO_DUTY_ADDR(d), samples, 8, 7, 2);
	}
	// Wave channel starts silent (all zero) until the game writes its
	// own data and this gets refreshed by UpdateAudio() below.
	int silent[32] = {0};
	UploadLoopingSample(AUDIO_WAVE_ADDR, silent, 32, 7, 8);
	memset(lastWaveRAM, 0, sizeof(lastWaveRAM));

	// Flat ADSR on all 4 voices: instant attack, no decay, full sustain,
	// instant release - so only this code's own per-frame volume writes
	// control perceived volume, never the SPU's own envelope hardware.
	int v;
	for (v = AUDIO_VOICE_CH1; v <= AUDIO_VOICE_CH4; v++) {
		SpuSetVoiceADSR(v, 0x0F, 0x00, 0x00, 0x1F, 0x0F);
	}

	SpuSetVoiceStartAddr(AUDIO_VOICE_CH1, AUDIO_DUTY_ADDR(2)); // default 50% duty
	SpuSetVoiceStartAddr(AUDIO_VOICE_CH2, AUDIO_DUTY_ADDR(2));
	SpuSetVoiceStartAddr(AUDIO_VOICE_CH3, AUDIO_WAVE_ADDR);

	// Channel 4 uses the SPU's hardware noise generator instead of a
	// sample - SPU_NOISE_MODE is a bitmask, one bit per voice.
	SPU_NOISE_MODE1 = (1 << AUDIO_VOICE_CH4);
	SPU_NOISE_MODE2 = 0;

	audioInitialized = 1;
}

// Converts a GB channel's current volume (0-15) and the master volume/
// panning registers into this voice's actual left/right volume values.
// Scaled so 4 simultaneous full-volume channels sum to within the SPU's
// signed 16-bit range without clipping, mirroring the real headroom
// margin analog hardware mixing four channels together would also need.
static void SetVoiceVolumeForChannel(int voice, int currentVolume, int enabled, int panLeftBit, int panRightBit) {
	int masterLeft = (NR50 >> 4) & 0x07;
	int masterRight = NR50 & 0x07;
	int base = enabled ? (currentVolume * 0x2000) / 15 : 0;
	int left = ((NR51 & panLeftBit) && enabled) ? (base * (masterLeft + 1)) / 8 : 0;
	int right = ((NR51 & panRightBit) && enabled) ? (base * (masterRight + 1)) / 8 : 0;
	SpuSetVoiceVolume(voice, (int16_t) left, (int16_t) right);
}

// GB pulse/wave frequency-to-Hz formulas (Pan Docs) converted directly
// to an SPU pitch value: pitch = (Hz * samplesPerPeriod * 4096) / 44100,
// where samplesPerPeriod is 8 for the pulse duty samples and 32 for the
// wave channel (see UploadLoopingSample's repeat counts above - the
// loop always contains exactly 7 periods regardless of samplesPerPeriod,
// so this only depends on the per-period sample count, not block count).
static uint16_t PulsePitch(int freq11) {
	if (freq11 >= 2048) return 0;
	int hz_x64 = 131072 * 64 / (2048 - freq11); // keep some fractional precision
	int64_t pitch = ((int64_t) hz_x64 * 8 * 4096) / (44100 * 64);
	// Clamp rather than let a very high GB frequency (rare in real
	// music, but real SFX sometimes briefly use near-extreme values)
	// silently overflow the SPU's 16-bit pitch register and wrap
	// around to a garbage low value instead of a merely very high one.
	if (pitch > 0xFFFF) pitch = 0xFFFF;
	return (uint16_t) pitch;
}
static uint16_t WavePitch(int freq11) {
	if (freq11 >= 2048) return 0;
	int hz_x64 = 65536 * 64 / (2048 - freq11);
	int64_t pitch = ((int64_t) hz_x64 * 32 * 4096) / (44100 * 64);
	if (pitch > 0xFFFF) pitch = 0xFFFF;
	return (uint16_t) pitch;
}

void UpdateAudio(void) {
	if (!audioInitialized) {
		return;
	}
	if (!(NR52 & 0x80)) {
		// Master APU off - silence everything and skip further updates
		// until it's back on.
		SpuSetKey(0, (1 << AUDIO_VOICE_CH1) | (1 << AUDIO_VOICE_CH2) |
			(1 << AUDIO_VOICE_CH3) | (1 << AUDIO_VOICE_CH4));
		return;
	}

	// Channel 1 (pulse + sweep)
	{
		int duty = (apuCh1.nrX1 >> 6) & 0x03;
		int freq = apuCh1.nrX3 | ((apuCh1.nrX4 & 0x07) << 8);
		SpuSetVoiceStartAddr(AUDIO_VOICE_CH1, AUDIO_DUTY_ADDR(duty));
		SpuSetVoicePitch(AUDIO_VOICE_CH1, PulsePitch(freq));
		SetVoiceVolumeForChannel(AUDIO_VOICE_CH1, apuCh1.currentVolume, apuCh1.enabled, 0x10, 0x01);
		SpuSetKey(apuCh1.enabled ? 1 : 0, 1 << AUDIO_VOICE_CH1);
	}
	// Channel 2 (pulse, no sweep)
	{
		int duty = (apuCh2.nrX1 >> 6) & 0x03;
		int freq = apuCh2.nrX3 | ((apuCh2.nrX4 & 0x07) << 8);
		SpuSetVoiceStartAddr(AUDIO_VOICE_CH2, AUDIO_DUTY_ADDR(duty));
		SpuSetVoicePitch(AUDIO_VOICE_CH2, PulsePitch(freq));
		SetVoiceVolumeForChannel(AUDIO_VOICE_CH2, apuCh2.currentVolume, apuCh2.enabled, 0x20, 0x02);
		SpuSetKey(apuCh2.enabled ? 1 : 0, 1 << AUDIO_VOICE_CH2);
	}
	// Channel 3 (wave) - re-encode/upload only if the wave table
	// actually changed since the last frame.
	{
		if (memcmp(WAVERAM, lastWaveRAM, 16) != 0) {
			int samples[32];
			int i;
			for (i = 0; i < 32; i++) {
				BYTE byte = WAVERAM[i / 2];
				samples[i] = (i % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
			}
			UploadLoopingSample(AUDIO_WAVE_ADDR, samples, 32, 7, 8);
			memcpy(lastWaveRAM, WAVERAM, 16);
		}
		int freq = apuCh3.nrX3 | ((apuCh3.nrX4 & 0x07) << 8);
		int shift;
		switch ((apuCh3.nrX2 >> 5) & 0x03) {
			case 0: shift = 4; break;
			case 1: shift = 0; break;
			case 2: shift = 1; break;
			default: shift = 2; break;
		}
		int ch3Volume = apuCh3.enabled ? (15 >> shift) : 0;
		SpuSetVoicePitch(AUDIO_VOICE_CH3, WavePitch(freq));
		SetVoiceVolumeForChannel(AUDIO_VOICE_CH3, ch3Volume, apuCh3.enabled, 0x40, 0x04);
		SpuSetKey(apuCh3.enabled ? 1 : 0, 1 << AUDIO_VOICE_CH3);
	}
	// Channel 4 (noise) - hardware noise generator; approximate the GB's
	// clock-shift/divisor pair as a single noise frequency/step value in
	// SPU_CTRL. This is a real, acknowledged approximation - the SPU's
	// noise generator is not bit-for-bit the same algorithm as the GB's
	// LFSR, just similar broadband noise in spirit.
	{
		int shift = (apuCh4.nrX3 >> 4) & 0x0F;
		int div = apuCh4.nrX3 & 0x07;
		int noiseFreq = (shift << 2) | (div >> 1); // fold into SPU_CTRL's 6-bit noise frequency field
		if (noiseFreq > 0x3F) noiseFreq = 0x3F;
		// Only touches the noise-frequency/step bits - leaves every
		// other SPU_CTRL bit (master enable, reverb, etc.) exactly as
		// SpuInit() and the rest of this file already set them. The
		// per-voice SPU_NOISE_MODE1 bit set in InitAudio() is what
		// actually turns noise generation on for this voice at all;
		// this only tunes its perceived pitch.
		SPU_CTRL = (SPU_CTRL & ~0x003F) | noiseFreq;
		SetVoiceVolumeForChannel(AUDIO_VOICE_CH4, apuCh4.currentVolume, apuCh4.enabled, 0x80, 0x08);
		SpuSetKey(apuCh4.enabled ? 1 : 0, 1 << AUDIO_VOICE_CH4);
	}
}

// ---- Controller input -------------------------------------------------
// PSn00bSDK doesn't wrap the BIOS's pad driver the way the official Sony
// SDK's PadInit()/PadRead() did (see psxpad.h's own comments) - but the
// BIOS driver itself is the same either way, so this uses it directly via
// psxapi.h's InitPAD/StartPAD, matching what PadInit()/PadRead() would
// have done underneath in the original Psy-Q build.
static uint8_t pad_buff[2][34];

unsigned long PadRead(int pad_num) {
	(void) pad_num; // only controller port 1 is read for now
	PADTYPE *pad0 = (PADTYPE *) pad_buff[0];

	// stat != 0 means no controller responded (disconnected, or the BIOS
	// hasn't completed a poll cycle yet) - report nothing pressed rather
	// than garbage.
	if (pad0->stat != 0) {
		return 0;
	}

	// Real hardware button bits are active-low (0 = pressed); invert so
	// the bit tests below read naturally as "1 = pressed". Then remap
	// from PSn00bSDK's real controller-protocol bit layout (PadButton in
	// psxpad.h) into the historical bit layout aGBe's own pad.h macros
	// use - the two differ, and rather than touch the many existing
	// (pad & Pad1x)-style checks in emu.c, all the translation is
	// localized here in one place.
	uint16_t raw = ~(pad0->btn);
	unsigned long result = 0;
	if (raw & PAD_UP)       result |= Pad1Up;
	if (raw & PAD_DOWN)     result |= Pad1Down;
	if (raw & PAD_LEFT)     result |= Pad1Left;
	if (raw & PAD_RIGHT)    result |= Pad1Right;
	if (raw & PAD_L1)       result |= Pad1L1;
	if (raw & PAD_L2)       result |= Pad1L2;
	if (raw & PAD_R1)       result |= Pad1R1;
	if (raw & PAD_R2)       result |= Pad1R2;
	if (raw & PAD_TRIANGLE) result |= Pad1tri;
	if (raw & PAD_SQUARE)   result |= Pad1sqr;
	if (raw & PAD_CIRCLE)   result |= Pad1crc;
	if (raw & PAD_CROSS)    result |= Pad1x;
	if (raw & PAD_START)    result |= Pad1Start;
	if (raw & PAD_SELECT)   result |= Pad1Select;
	return result;
}

// ---- Initialization ----------------------------------------------------
void init_PSX(void) {
	ResetGraph(0);

	// Two vertically-stacked 320x240 regions in VRAM: disp_env[i] and
	// draw_env[i] deliberately point at OPPOSITE halves for the same i
	// (see the comment above the buffer declarations) - this is what
	// makes alternating db between 0 and 1 in Draw_Buffer() actually
	// double-buffer instead of drawing into what's currently shown.
	SetDefDispEnv(&disp_env[0], 0, 0, SCREEN_XRES, SCREEN_YRES);
	SetDefDispEnv(&disp_env[1], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&draw_env[0], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&draw_env[1], 0, 0, SCREEN_XRES, SCREEN_YRES);

	int i;
	for (i = 0; i < 2; i++) {
		setRGB0(&draw_env[i], 0, 0, 0);
		draw_env[i].isbg = 1;
	}
	PutDrawEnv(&draw_env[0]);
	PutDispEnv(&disp_env[0]);
	SetDispMask(1);

	// Loads PSn00bSDK's built-in debug font into an unused corner of VRAM
	// (x=960 is well clear of the 320-wide visible display area) so
	// FntPrint() works - used by the ROM select menu (gui.c) for now, and
	// available for any future debug/UI text.
	FntLoad(960, 0);

	InitPAD(pad_buff[0], 34, pad_buff[1], 34);
	StartPAD();
	// Match the original Psy-Q build's polling behaviour (continuous
	// state rather than the BIOS's optional auto-clear-on-VSync mode).
	ChangeClearPAD(0);

	CdInit();

	// Initializes the BIOS's memory card filesystem driver so the
	// bu00:/bu10: device paths used by Save/LoadCartRAM below work.
	_bu_init();

	InitAudio();
}

// ---- CD-ROM ROM loading (multi-game disc support) ----------------------
// Real ISO9660 file access via PSn00bSDK's psxcd.h - CdSearchFile() finds
// a named file's disc position and size (the same two-call pattern the
// original Psy-Q-based version of this file used, just against a plain
// ISO9660 file instead of a custom AGBEBANK.BIN multi-ROM bundle format:
// plain files are simpler, standard, and don't need a bespoke packing
// tool - the original's packer was never even committed to the project's
// CVS history in the first place, see STATUS.md).
int LoadROMFromCD(const char *filename, BYTE *dest, int maxSize) {
	CdlFILE file;
	char name[32];

	// CdSearchFile wants the ISO9660 version suffix; add it if the caller
	// didn't already include one, so callers can just pass "GAME.GB".
	strncpy(name, filename, sizeof(name) - 3);
	name[sizeof(name) - 3] = '\0';
	if (!strchr(name, ';')) {
		strcat(name, ";1");
	}

	if (!CdSearchFile(&file, name)) {
		return -1;
	}
	if (file.size > maxSize) {
		return -2;
	}

	CdControl(CdlSetloc, &file.pos, 0);
	// Sectors are 2048 bytes each in the default (non-CdlModeSize) mode
	// CdRead's own doc comments describe; round up so a file that isn't
	// an exact multiple of 2048 bytes still gets fully read.
	int sectors = (file.size + 2047) / 2048;
	if (!CdRead(sectors, (uint32_t *) dest, 0)) {
		return -3;
	}
	CdReadSync(0, 0);

	return file.size;
}

int ListRootDirectory(void *outFiles, int maxFiles) {
	CdlFILE *files = (CdlFILE *) outFiles;
	CdlDIR *dir = CdOpenDir("\\");
	int found = 0;

	if (!dir) {
		return 0;
	}
	while (found < maxFiles && CdReadDir(dir, &files[found])) {
		found++;
	}
	CdCloseDir(dir);
	return found;
}

// ---- Memory card save/load (battery-backed cart RAM) -------------------
// Real BIOS filesystem access via psxapi.h's open/close/read/write and
// sys/fcntl.h's FREAD/FWRITE/FCREATE/FNBLOCKS - the same "bu00:" device
// path convention and flag values the official SDK's memory card access
// uses (PSn00bSDK deliberately mirrors it; _bu_init() below is its name
// for what the official SDK exposes as InitCARD/StartCARD's underlying
// driver init).
//
// One save file per cartridge, named from the cart's own header title so
// multiple games on one disc (see the CD-loading/ROM-select work earlier
// this session) don't collide with each other's saves.
#define SAVE_BLOCK_SIZE 8192

// Builds a memory-card-safe file path from a cartridge title: "bu00:"
// plus up to 10 sanitized characters (alphanumeric only, everything else
// dropped) from the title, prefixed "AGBE-" so these saves are
// identifiable and don't collide with any other homebrew's saves on the
// same card.
static void build_save_path(char *out, const char *title) {
	strcpy(out, "bu00:AGBE-");
	int outLen = strlen(out);
	int i;
	for (i = 0; i < 10 && title[i] != '\0'; i++) {
		char c = title[i];
		int isAlnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
		if (isAlnum) {
			out[outLen++] = c;
		}
	}
	out[outLen] = '\0';
}

int SaveCartRAM(const char *saveId, BYTE *buf, int size) {
	char path[32];
	build_save_path(path, saveId);

	int blocks = (size + SAVE_BLOCK_SIZE - 1) / SAVE_BLOCK_SIZE;
	if (blocks < 1) {
		blocks = 1;
	}

	// Make sure a correctly-sized file exists first (creating one is a
	// no-op error, harmlessly ignored, if it already does).
	int f = open(path, FCREATE | FWRITE | FNBLOCKS(blocks));
	if (f >= 0) {
		close(f);
	}

	f = open(path, FWRITE);
	if (f < 0) {
		return 0; // no card present, card full, or some other I/O error
	}
	int written = write(f, buf, size);
	close(f);
	return written == size;
}

int LoadCartRAM(const char *saveId, BYTE *buf, int size) {
	char path[32];
	build_save_path(path, saveId);

	int f = open(path, FREAD);
	if (f < 0) {
		return 0; // no save yet - not an error, just a fresh cartridge
	}
	int got = read(f, buf, size);
	close(f);
	return got == size;
}
