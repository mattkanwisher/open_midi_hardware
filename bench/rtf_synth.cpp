/*
 * rtf_synth.cpp -- mt32emu cost-per-partial harness, on SYNTHETIC ROMs.
 *
 * ############################################################################
 * ##  THE ROMs THIS PROGRAM USES ARE FABRICATED. THE AUDIO IS NOT MT-32     ##
 * ##  AUDIO. NOTHING THIS PROGRAM PRINTS IS A MEASUREMENT OF A REAL SCORE.  ##
 * ############################################################################
 *
 * WHY THIS EXISTS.  bench/rtf.cpp answers docs/PLAN.md section 0's gate -- does
 * mt32emu render faster than real time on one Cortex-A7 -- but it needs MT-32
 * ROM dumps, which are Roland's and are absent from this repository by design.
 * So it has never been run, and the repository has correctly refused to guess.
 *
 * This harness replaces "no evidence" with "a bounded estimate".  It does NOT
 * close the gate.  See bench/ANALYSIS.md section 9.
 *
 * THE TRICK.  mt32emu's own test suite (src/test/FakeROMs.cpp) fabricates ROM
 * images the library accepts, by handing ArrayFile the *expected* SHA-1 digest
 * instead of hashing the fabrication, and filling in only the handful of
 * control-ROM fields Synth::open() actually reads.  emu/src/
 * engine_mt32emu_fake_roms.cpp already uses this in workstream E; this file is
 * the same technique, generalised for benchmarking.
 *
 * THE HONESTY PROBLEM, AND HOW THIS DEALS WITH IT.  Instruction count per
 * output sample is a function of how many partials are sounding, and which
 * partial allocation a note triggers is a function of the timbre data in the
 * control ROM.  A fabricated control ROM is all zeroes, so it carries no
 * timbres at all -- a note-on on a stock fake ROM allocates ZERO partials
 * (Part::cacheTimbre reads common.partialMute, which is 0).  Therefore:
 *
 *   A synthetic ROM cannot reproduce a real score's workload, and this program
 *   does not try to.  Instead it BOUNDS the workload.  It writes timbres of
 *   its own choosing into the Timbre Temp Area over sysex -- which is ordinary
 *   MT-32 RAM, not ROM -- so the number of active partials, the waveform, and
 *   the partial structure are all chosen by the operator and verified from
 *   Synth::getPartialStates().  Sweeping the partial count produces a
 *   cost-per-partial slope plus a fixed overhead, and a real score's cost can
 *   then be predicted from its partial count, which is a knowable property of
 *   MT-32 hardware (32 partials, hard ceiling, globals.h:97).
 *
 * WHAT IS STILL SYNTHETIC even with this in place, listed so nobody forgets:
 *   - the PCM ROM is all zeroes, so PCM partials read silence.  They do the
 *     same work; they produce different numbers.  Data-dependent branches in
 *     the LA32 (clamping, phase segment selection) may therefore be biased.
 *   - the timbre parameters here are deliberately plain (flat envelopes,
 *     no LFO, no pitch envelope).  A real Sierra-era timbre modulates more.
 *   - real scores retrigger constantly; --retrigger exists to bracket that.
 *
 * This file: no copyright asserted, do as you like.  It links against mt32emu,
 * which is LGPL 2.1 -- see vendor/munt/mt32emu/COPYING.LESSER.txt.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#include <mt32emu/mt32emu.h>

/* Internal headers.  Deliberate: this is a bench, not the port.  We need
 * ControlROMMap (to know where to poke the fabricated control ROM) and
 * TimbreParam (to build a timbre byte-for-byte).  bench/CMakeLists.txt adds
 * vendor/munt/mt32emu/src to this target's include path and defines
 * MT32EMU_WITH_TESTING so Test::getControlROMMap() is exported. */
#include "Structures.h"

using namespace MT32Emu;

namespace MT32Emu {
namespace Test {
	const ControlROMMap *getControlROMMap(const char *shortName);
}
}

/* ------------------------------------------------------------------ clock */

static double monotonicSeconds() {
	struct timespec ts;
#if defined(CLOCK_MONOTONIC_RAW)
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0) {
		return double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec);
	}
#endif
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec);
}

/* --------------------------------------------------------- report handler */

class SynthReportHandler : public ReportHandler {
public:
	bool verbose;
	unsigned long noteOnIgnored;
	SynthReportHandler() : verbose(false), noteOnIgnored(0) {}
	void printDebug(const char *fmt, va_list list) {
		if (!verbose) return;
		fprintf(stderr, "[mt32emu] ");
		vfprintf(stderr, fmt, list);
		fprintf(stderr, "\n");
	}
	void showLCDMessage(const char *) {}
	void onErrorControlROM() { fprintf(stderr, "FATAL: fabricated control ROM rejected\n"); }
	void onErrorPCMROM()     { fprintf(stderr, "FATAL: fabricated PCM ROM rejected\n"); }
};

/* ------------------------------------------------------- fabricated ROMs */

static const char STARTUP_DISPLAY_MESSAGE[] = "Starting up...      ";
static const char ERROR_DISPLAY_MESSAGE[]   = "SysEx error!        ";

static const ROMInfo *findRomInfo(const char *shortName) {
	for (const ROMInfo * const *ri = ROMInfo::getAllROMInfos(); *ri != NULL; ri++) {
		if (strcmp(shortName, (*ri)->shortName) == 0) return *ri;
	}
	return NULL;
}

/* Fabricate a control ROM that Synth::open() accepts.  Same set of fields as
 * mt32emu's own FakeROMs.cpp, plus one addition: a valid PCM wave map, so that
 * partial structures which use PCM partials do not read a degenerate entry.
 * Without this, every wave map entry decodes to addr 0 / len 0x800 / loop=off
 * (Synth.cpp:677 initPCMList on all-zero data), and a PCM partial would run
 * off the end of its wave and deactivate mid-measurement, silently changing
 * the partial count we think we are measuring. */
static Bit8u *makeControlRom(const ROMInfo *info) {
	Bit8u *data = new Bit8u[info->fileSize];
	memset(data, 0, info->fileSize);

	const ControlROMMap *map = Test::getControlROMMap(info->shortName);
	if (map == NULL) { delete[] data; return NULL; }

	memset(data + map->patchMaxTable,  0x7f, 16);
	memset(data + map->rhythmMaxTable, 0x7f, 4);
	memset(data + map->systemMaxTable, 0x7f, 23);
	memset(data + map->timbreMaxTable, 0x7f, 72);

	memcpy(data + map->startupMessage,    STARTUP_DISPLAY_MESSAGE, 21);
	memcpy(data + map->sysexErrorMessage, ERROR_DISPLAY_MESSAGE,   21);

	{
		const Bit32u groups = 4u;
		SoundGroup *tbl = reinterpret_cast<SoundGroup *>(data + map->soundGroupsTable);
		data[map->soundGroupsCount] = Bit8u(groups);
		for (Bit32u i = 0; i < groups; i++) {
			memcpy(tbl[i].name, "Group 1|", 8);
			tbl[i].name[6] = Bit8u(tbl[i].name[6] + i);
		}
		for (int i = -128; i < 0; i++) data[map->soundGroupsTable + i] = Bit8u(i & 1);
	}

	/* PCM wave map: every entry points at offset 0, length 0x800 samples, and
	 * LOOPS (bit 7 of len).  Pitch word is left at 0. */
	{
		ControlROMPCMStruct *tps =
			reinterpret_cast<ControlROMPCMStruct *>(data + map->pcmTable);
		for (Bit16u i = 0; i < map->pcmCount; i++) {
			tps[i].pos      = 0;
			tps[i].len      = 0x80;   /* loop = 1, length exponent = 0 -> 0x800 */
			tps[i].pitchLSB = 0;
			tps[i].pitchMSB = 0;
		}
	}
	return data;
}

/* ------------------------------------------------------- sysex construction */

/* Roland DT1 to an MT-32: F0 41 <dev> 16 12 <a1 a2 a3> <data...> <sum> F7 */
static void buildDt1(std::vector<Bit8u> &out, Bit32u sysexAddr,
                     const Bit8u *payload, Bit32u len) {
	out.clear();
	out.push_back(0xf0); out.push_back(0x41); out.push_back(0x10);
	out.push_back(0x16); out.push_back(0x12);
	Bit8u a1 = Bit8u((sysexAddr >> 16) & 0x7f);
	Bit8u a2 = Bit8u((sysexAddr >>  8) & 0x7f);
	Bit8u a3 = Bit8u( sysexAddr        & 0x7f);
	out.push_back(a1); out.push_back(a2); out.push_back(a3);
	unsigned int sum = a1 + a2 + a3;
	for (Bit32u i = 0; i < len; i++) {
		Bit8u b = Bit8u(payload[i] & 0x7f);
		out.push_back(b);
		sum += b;
	}
	out.push_back(Bit8u((128 - (sum & 0x7f)) & 0x7f));
	out.push_back(0xf7);
}

static void sendDt1(Synth &synth, Bit32u internalAddr, const Bit8u *payload, Bit32u len) {
	std::vector<Bit8u> msg;
	buildDt1(msg, MT32EMU_SYSEXMEMADDR(internalAddr), payload, len);
	synth.playSysexNow(&msg[0], Bit32u(msg.size()));
}

/* ------------------------------------------------------- timbre construction */

struct TimbreShape {
	int  waveform;    /* 0 square, 1 sawtooth */
	int  structure12; /* 0..12 */
	int  structure34; /* 0..12 */
	int  partialMute; /* 0..15 bitmask */
};

/* A deliberately plain timbre: flat pitch envelope, no LFO, no TVF envelope,
 * TVA straight to full sustain and held there.  The point is a workload whose
 * partial count is exactly what we asked for and stays that way, not a
 * musically plausible patch. */
static void buildTimbre(TimbreParam &t, const TimbreShape &s) {
	memset(&t, 0, sizeof(t));
	memcpy(t.common.name, "BENCHTIM  ", 10);
	t.common.partialStructure12 = Bit8u(s.structure12);
	t.common.partialStructure34 = Bit8u(s.structure34);
	t.common.partialMute        = Bit8u(s.partialMute);
	t.common.noSustain          = 0;

	for (int p = 0; p < 4; p++) {
		TimbreParam::PartialParam &pp = t.partial[p];

		pp.wg.pitchCoarse   = 48;   /* 0-96, centre of the range */
		pp.wg.pitchFine     = 50;   /* 0-100 -> -50..+50 cents; 50 = 0 */
		pp.wg.pitchKeyfollow = 11;  /* table index 11 = 1 (normal) */
		pp.wg.pitchBenderEnabled = 0;
		pp.wg.waveform      = Bit8u(s.waveform);
		pp.wg.pcmWave       = 0;
		pp.wg.pulseWidth    = 50;
		pp.wg.pulseWidthVeloSensitivity = 7;  /* 0-14 -> -7..+7; 7 = 0 */

		pp.pitchEnv.depth = 0;
		pp.pitchEnv.veloSensitivity = 50;
		pp.pitchEnv.timeKeyfollow = 0;
		for (int i = 0; i < 4; i++) pp.pitchEnv.time[i] = 50;
		for (int i = 0; i < 5; i++) pp.pitchEnv.level[i] = 50;  /* 50 = 0 */

		pp.pitchLFO.rate = 0;
		pp.pitchLFO.depth = 0;
		pp.pitchLFO.modSensitivity = 0;

		pp.tvf.cutoff = 100;
		pp.tvf.resonance = 0;
		pp.tvf.keyfollow = 11;
		pp.tvf.biasPoint = 0;
		pp.tvf.biasLevel = 7;       /* 0-14 -> -7..+7; 7 = 0 */
		pp.tvf.envDepth = 0;        /* flat: TVF envelope contributes nothing */
		pp.tvf.envVeloSensitivity = 50;
		pp.tvf.envDepthKeyfollow = 0;
		pp.tvf.envTimeKeyfollow = 0;
		for (int i = 0; i < 5; i++) pp.tvf.envTime[i] = 50;
		for (int i = 0; i < 4; i++) pp.tvf.envLevel[i] = 100;

		pp.tva.level = 100;
		pp.tva.veloSensitivity = 50;
		pp.tva.biasPoint1 = 0;
		pp.tva.biasLevel1 = 12;     /* 0-12 -> -12..0 dB; 12 = 0 */
		pp.tva.biasPoint2 = 0;
		pp.tva.biasLevel2 = 12;
		pp.tva.envTimeKeyfollow = 0;
		pp.tva.envTimeVeloSensitivity = 0;
		pp.tva.envTime[0] = 0;      /* instant attack, straight to sustain */
		pp.tva.envTime[1] = 0;
		pp.tva.envTime[2] = 0;
		pp.tva.envTime[3] = 0;
		pp.tva.envTime[4] = 100;    /* release; never reached, notes are held */
		for (int i = 0; i < 4; i++) pp.tva.envLevel[i] = 100;
	}
}

/* --------------------------------------------------------------- options */

static void usage(const char *argv0) {
	fprintf(stderr,
"mt32emu cost-per-partial harness on SYNTHETIC ROMs (mt32-t113 bench)\n"
"\n"
"  !! The ROMs are fabricated. The audio is not MT-32 audio. This program\n"
"  !! does NOT answer docs/PLAN.md section 0's gate. See bench/ANALYSIS.md.\n"
"\n"
"usage: %s [options]\n"
"\n"
"workload:\n"
"  --partials N     number of partials to hold active, 0..32   (default 32)\n"
"  --waveform W     square | saw                               (default square)\n"
"  --structure N    partial structure 0..12                    (default 0)\n"
"                   0 = two independent synth partials, no ring mod, no PCM\n"
"                   1 = ring modulated pair;  2 = one PCM partial per pair\n"
"  --key N          MIDI key for the held notes, 0..127        (default 60)\n"
"  --velocity N     note-on velocity, 1..127                   (default 100)\n"
"  --retrigger MS   all-sound-off + re-strike every MS ms      (default 0, hold)\n"
"\n"
"synth configuration:\n"
"  --seconds S      seconds of audio to render                 (default 1.0)\n"
"  --block N        frames per render() call                   (default 256)\n"
"  --sample-rate N  0 = synth native, else engage resampler    (default 0)\n"
"  --src-quality Q  fastest|fast|good|best                     (default good)\n"
"  --renderer R     int | float                                (default int)\n"
"  --reverb on|off                                             (default on)\n"
"                   off is done the hardware's way, System Area reverb time\n"
"                   and level both zero -- see the note in the source\n"
"  --analog MODE    digital|coarse|accurate|oversampled        (default coarse)\n"
"  --max-partials N synth partial ceiling                      (default 32)\n"
"\n"
"measurement:\n"
"  --warmup S       discard the first S seconds                (default 0)\n"
"  --count-mode     no per-block clock_gettime, no partial polling, no csv.\n"
"                   Use this under qemu-arm when counting instructions: the\n"
"                   syscalls would otherwise dominate the count.\n"
"  --csv FILE       per-block timings\n"
"  --verbose        let mt32emu print its debug chatter\n"
"  --help\n", argv0);
}

static bool argMatch(const char *a, const char *name) { return strcmp(a, name) == 0; }

int main(int argc, char **argv) {
	unsigned int targetPartials = 32;
	unsigned int maxPartials = 32;
	unsigned int blockFrames = 256;
	double seconds = 1.0;
	double warmup = 0.0;
	double wantRate = 0.0;
	unsigned int key = 60, velocity = 100;
	unsigned int retriggerMs = 0;
	int waveform = 0;
	int structure = 0;
	bool reverb = true;
	bool countMode = false;
	bool verbose = false;
	const char *csvPath = NULL;
	RendererType rendererType = RendererType_BIT16S;
	AnalogOutputMode analogMode = AnalogOutputMode_COARSE;
	SamplerateConversionQuality srcQuality = SamplerateConversionQuality_GOOD;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
		if (argMatch(a, "--help") || argMatch(a, "-h")) { usage(argv[0]); return 0; }
		else if (argMatch(a, "--partials") && v)   { targetPartials = (unsigned)atoi(v); i++; }
		else if (argMatch(a, "--max-partials") && v) { maxPartials = (unsigned)atoi(v); i++; }
		else if (argMatch(a, "--block") && v)      { blockFrames = (unsigned)atoi(v); i++; }
		else if (argMatch(a, "--seconds") && v)    { seconds = atof(v); i++; }
		else if (argMatch(a, "--warmup") && v)     { warmup = atof(v); i++; }
		else if (argMatch(a, "--sample-rate") && v){ wantRate = atof(v); i++; }
		else if (argMatch(a, "--key") && v)        { key = (unsigned)atoi(v); i++; }
		else if (argMatch(a, "--velocity") && v)   { velocity = (unsigned)atoi(v); i++; }
		else if (argMatch(a, "--retrigger") && v)  { retriggerMs = (unsigned)atoi(v); i++; }
		else if (argMatch(a, "--structure") && v)  { structure = atoi(v); i++; }
		else if (argMatch(a, "--csv") && v)        { csvPath = v; i++; }
		else if (argMatch(a, "--count-mode"))      { countMode = true; }
		else if (argMatch(a, "--verbose"))         { verbose = true; }
		else if (argMatch(a, "--waveform") && v) {
			if (argMatch(v, "square")) waveform = 0;
			else if (argMatch(v, "saw")) waveform = 1;
			else { fprintf(stderr, "error: --waveform square|saw\n"); return 2; }
			i++;
		}
		else if (argMatch(a, "--renderer") && v) {
			if (argMatch(v, "int")) rendererType = RendererType_BIT16S;
			else if (argMatch(v, "float")) rendererType = RendererType_FLOAT;
			else { fprintf(stderr, "error: --renderer int|float\n"); return 2; }
			i++;
		}
		else if (argMatch(a, "--reverb") && v) {
			if (argMatch(v, "on")) reverb = true;
			else if (argMatch(v, "off")) reverb = false;
			else { fprintf(stderr, "error: --reverb on|off\n"); return 2; }
			i++;
		}
		else if (argMatch(a, "--analog") && v) {
			if (argMatch(v, "digital")) analogMode = AnalogOutputMode_DIGITAL_ONLY;
			else if (argMatch(v, "coarse")) analogMode = AnalogOutputMode_COARSE;
			else if (argMatch(v, "accurate")) analogMode = AnalogOutputMode_ACCURATE;
			else if (argMatch(v, "oversampled")) analogMode = AnalogOutputMode_OVERSAMPLED;
			else { fprintf(stderr, "error: --analog digital|coarse|accurate|oversampled\n"); return 2; }
			i++;
		}
		else if (argMatch(a, "--src-quality") && v) {
			if (argMatch(v, "fastest")) srcQuality = SamplerateConversionQuality_FASTEST;
			else if (argMatch(v, "fast")) srcQuality = SamplerateConversionQuality_FAST;
			else if (argMatch(v, "good")) srcQuality = SamplerateConversionQuality_GOOD;
			else if (argMatch(v, "best")) srcQuality = SamplerateConversionQuality_BEST;
			else { fprintf(stderr, "error: --src-quality fastest|fast|good|best\n"); return 2; }
			i++;
		}
		else { fprintf(stderr, "error: unknown option '%s'\n", a); usage(argv[0]); return 2; }
	}

	if (targetPartials > maxPartials) {
		fprintf(stderr, "error: --partials %u exceeds --max-partials %u\n",
			targetPartials, maxPartials);
		return 2;
	}
	if (structure < 0 || structure > 12) {
		fprintf(stderr, "error: --structure must be 0..12\n"); return 2;
	}
	if (blockFrames == 0) { fprintf(stderr, "error: --block must be > 0\n"); return 2; }

	printf("### SYNTHETIC ROMS -- NOT AN MT-32 MEASUREMENT ###\n");
	printf("mt32emu %s, harness rtf_synth\n", Synth::getLibraryVersionString());

	/* ------------------------------------------------------ fabricate ROMs */

	const ROMInfo *ctrlInfo = findRomInfo("ctrl_mt32_1_07");
	if (!ctrlInfo) ctrlInfo = findRomInfo("ctrl_mt32_1_06");
	if (!ctrlInfo) ctrlInfo = findRomInfo("ctrl_mt32_1_05");
	const ROMInfo *pcmInfo = findRomInfo("pcm_mt32");
	if (!ctrlInfo || !pcmInfo) {
		fprintf(stderr, "error: this mt32emu build knows no MT-32 ROM descriptors\n");
		return 1;
	}

	Bit8u *ctrlData = makeControlRom(ctrlInfo);
	if (ctrlData == NULL) {
		fprintf(stderr, "error: no control ROM map for '%s' -- was mt32emu built "
		                "without -DMT32EMU_WITH_TESTING?\n", ctrlInfo->shortName);
		return 1;
	}
	Bit8u *pcmData = new Bit8u[pcmInfo->fileSize];
	memset(pcmData, 0, pcmInfo->fileSize);

	const ROMImage *ctrlImage = ROMImage::makeROMImage(
		new ArrayFile(ctrlData, ctrlInfo->fileSize, ctrlInfo->sha1Digest));
	const ROMImage *pcmImage = ROMImage::makeROMImage(
		new ArrayFile(pcmData, pcmInfo->fileSize, pcmInfo->sha1Digest));
	if (!ctrlImage || !ctrlImage->getROMInfo() || !pcmImage || !pcmImage->getROMInfo()) {
		fprintf(stderr, "error: the library refused the fabricated ROM images\n");
		return 1;
	}
	printf("fabricated ROMs: %s (%u B) + %s (%u B)  [all data synthetic]\n",
		ctrlInfo->shortName, (unsigned)ctrlInfo->fileSize,
		pcmInfo->shortName, (unsigned)pcmInfo->fileSize);

	/* ------------------------------------------------------------ the synth */

	SynthReportHandler handler;
	handler.verbose = verbose;
	Synth synth(&handler);
	synth.selectRendererType(rendererType);
	synth.preallocateReverbMemory(true);

	if (!synth.open(*ctrlImage, *pcmImage, maxPartials, analogMode)) {
		fprintf(stderr, "error: Synth::open() failed\n");
		return 1;
	}
	/* NB: setReverbEnabled() is deliberately NOT called here. Writing the
	 * System Area below calls Synth::refreshSystemReverbParameters(), which
	 * reinstates the reverb model from mt32ram unless reverbOverridden is set
	 * (Synth.cpp:1992-2024) -- so an early setReverbEnabled(false) is silently
	 * undone by our own sysex. Measured: with the call here, --reverb off and
	 * --reverb on produced armv7 instruction counts identical to 0.03 insn per
	 * frame out of 16860, which is what a silently ignored switch looks like.
	 * Reverb is turned off the way the hardware does it instead: reverb time
	 * and level both zero in the System Area, which Synth.cpp:2003 documents as
	 * the real device's way of disabling wet output. */

	const double nativeRate = double(Synth::getStereoOutputSampleRate(analogMode));
	double outRate = wantRate > 0.0 ? wantRate : nativeRate;
	SampleRateConverter *src = NULL;
	if (outRate != nativeRate) {
		double supported = SampleRateConverter::getSupportedOutputSampleRate(outRate);
		if (supported != outRate) {
			printf("note: %.0f Hz unsupported exactly, using %.0f Hz\n", outRate, supported);
			outRate = supported;
		}
		src = new SampleRateConverter(synth, outRate, srcQuality);
	}

	/* ------------------------------- program the machine into a known state */

	/* System area: give every part its own MIDI channel and reserve four
	 * partials each, so that partial allocation is deterministic and no part
	 * can steal from another.  A fabricated control ROM supplies zeroes for
	 * all of this, which would put every part on channel 1. */
	{
		Bit8u sys[23];
		memset(sys, 0, sizeof(sys));
		sys[SYSTEM_MASTER_TUNE_OFF]  = 64;
		sys[SYSTEM_REVERB_MODE_OFF]  = 0;   /* room */
		/* Time and level both 0 is how a real MT-32 disables wet reverb, and
		 * mt32emu takes the same shortcut (Synth.cpp:2003). */
		sys[SYSTEM_REVERB_TIME_OFF]  = Bit8u(reverb ? 5 : 0);
		sys[SYSTEM_REVERB_LEVEL_OFF] = Bit8u(reverb ? 5 : 0);
		for (int p = 0; p < 8; p++) sys[SYSTEM_RESERVE_SETTINGS_START_OFF + p] = 4;
		sys[SYSTEM_RESERVE_SETTINGS_START_OFF + 8] = 0;   /* rhythm reserves none */
		for (int p = 0; p < 8; p++) sys[SYSTEM_CHAN_ASSIGN_START_OFF + p] = Bit8u(p);
		sys[SYSTEM_CHAN_ASSIGN_START_OFF + 8] = 16;       /* rhythm part off */
		sys[SYSTEM_MASTER_VOL_OFF] = 100;
		sendDt1(synth, MT32EMU_MEMADDR(0x100000), sys, sizeof(sys));
	}

	/* Patch Temp for parts 0..7.  Written BEFORE the timbres, because a write
	 * here calls Part::resetTimbre(), which copies the (all-zero) ROM timbre
	 * over Timbre Temp (Synth.cpp:1758, Part.cpp:215). */
	for (int p = 0; p < 8; p++) {
		Bit8u pt[16];
		memset(pt, 0, sizeof(pt));
		pt[0] = 2;    /* timbreGroup: 2 = Memory */
		pt[1] = 0;    /* timbreNum */
		pt[2] = 24;   /* keyShift 0-48, 24 = 0 */
		pt[3] = 50;   /* fineTune 0-100, 50 = 0 */
		pt[4] = 12;   /* benderRange */
		pt[5] = 3;    /* assignMode: POLY4, so no note-steal games */
		pt[6] = Bit8u(reverb ? 1 : 0);
		pt[7] = 0;
		pt[8] = 100;  /* outputLevel */
		pt[9] = 7;    /* panpot 0-14, 7 = centre */
		sendDt1(synth, MT32EMU_MEMADDR(0x030000) + Bit32u(p) * 16, pt, sizeof(pt));
	}

	/* Timbre Temp for parts 0..7.  Each part gets a timbre whose partialMute
	 * asks for exactly as many partials as we still need. */
	unsigned int remaining = targetPartials;
	unsigned int partsUsed = 0;
	for (int p = 0; p < 8; p++) {
		unsigned int want = remaining > 4 ? 4 : remaining;
		TimbreShape shape;
		shape.waveform    = waveform;
		shape.structure12 = structure;
		shape.structure34 = structure;
		shape.partialMute = int((1u << want) - 1u);
		remaining -= want;
		if (want > 0) partsUsed++;

		TimbreParam t;
		buildTimbre(t, shape);
		sendDt1(synth, MT32EMU_MEMADDR(0x040000) + Bit32u(p) * Bit32u(sizeof(TimbreParam)),
		        reinterpret_cast<const Bit8u *>(&t), Bit32u(sizeof(TimbreParam)));
	}
	if (remaining != 0) {
		fprintf(stderr, "error: cannot reach %u partials with 8 parts of 4\n", targetPartials);
		return 1;
	}

	/* ------------------------------------------------------- strike the notes */

	for (unsigned int p = 0; p < partsUsed; p++) {
		synth.playMsgNow(Bit32u(0x90 | p) | (Bit32u(key) << 8) | (Bit32u(velocity) << 16));
	}

	/* Let the allocation settle and confirm it.  One block is enough: the TVA
	 * attack time is zero, so the partials are at sustain immediately. */
	{
		std::vector<Bit16s> probe(size_t(blockFrames) * 2);
		synth.render(&probe[0], blockFrames);
	}

	std::vector<PartialState> partialStates(maxPartials);
	unsigned int activeAfterStrike = 0;
	synth.getPartialStates(&partialStates[0]);
	for (unsigned int i = 0; i < maxPartials; i++)
		if (partialStates[i] != PartialState_INACTIVE) activeAfterStrike++;

	printf("workload: %u parts, structure %d/%d, waveform %s, key %u vel %u\n",
		partsUsed, structure, structure, waveform ? "saw" : "square", key, velocity);
	printf("partials requested %u, ACTIVE AFTER STRIKE %u\n", targetPartials, activeAfterStrike);
	if (activeAfterStrike != targetPartials) {
		fprintf(stderr,
			"WARNING: asked for %u active partials, got %u. The measurement below\n"
			"         does NOT describe the workload you asked for.\n",
			targetPartials, activeAfterStrike);
	}
	if (synth.isReverbEnabled() != reverb) {
		fprintf(stderr, "error: asked for reverb %s but the synth reports %s. "
			"Refusing to measure a configuration that is not the one requested.\n",
			reverb ? "on" : "off", synth.isReverbEnabled() ? "on" : "off");
		return 1;
	}
	printf("synth: renderer %s, reverb %s, analog %s, native %.0f Hz, out %.0f Hz%s\n",
		rendererType == RendererType_FLOAT ? "float" : "int16",
		reverb ? "on" : "off",
		analogMode == AnalogOutputMode_DIGITAL_ONLY ? "digital" :
		analogMode == AnalogOutputMode_COARSE ? "coarse" :
		analogMode == AnalogOutputMode_ACCURATE ? "accurate" : "oversampled",
		nativeRate, outRate, src ? " (resampled)" : " (direct)");

	/* ------------------------------------------------------------ render loop */

	unsigned long long totalFrames = (unsigned long long)(seconds * outRate);
	unsigned long long warmupFrames = (unsigned long long)(warmup * outRate);
	unsigned long long retriggerFrames =
		retriggerMs ? (unsigned long long)(double(retriggerMs) * 1e-3 * outRate) : 0;

	std::vector<Bit16s> buffer(size_t(blockFrames) * 2);

	FILE *csv = NULL;
	if (csvPath && !countMode) {
		csv = fopen(csvPath, "w");
		if (csv) fprintf(csv, "block,audio_start_s,render_s,block_rtf,active_partials\n");
	}

	unsigned long long framesDone = 0, blockIndex = 0, blocksCounted = 0;
	unsigned long long nextRetrigger = retriggerFrames;
	double totalRenderSeconds = 0.0, worstBlockSeconds = 0.0, worstBlockRtf = 0.0;
	unsigned long long worstBlockIndex = 0;
	unsigned int peakPartials = 0, minPartials = maxPartials + 1;
	unsigned long retriggers = 0;

	const double runStart = countMode ? 0.0 : monotonicSeconds();

	while (framesDone < totalFrames) {
		unsigned int thisBlock = blockFrames;
		if (totalFrames - framesDone < thisBlock)
			thisBlock = (unsigned int)(totalFrames - framesDone);

		if (retriggerFrames && framesDone >= nextRetrigger) {
			/* All Sound Off (CC 120) kills the old partials outright rather than
			 * releasing them, so the active count never overshoots. */
			for (unsigned int p = 0; p < partsUsed; p++)
				synth.playMsgNow(Bit32u(0xb0 | p) | (120u << 8));
			for (unsigned int p = 0; p < partsUsed; p++)
				synth.playMsgNow(Bit32u(0x90 | p) | (Bit32u(key) << 8) | (Bit32u(velocity) << 16));
			nextRetrigger += retriggerFrames;
			retriggers++;
		}

		if (countMode) {
			if (src) src->getOutputSamples(&buffer[0], thisBlock);
			else     synth.render(&buffer[0], thisBlock);
		} else {
			const double t0 = monotonicSeconds();
			if (src) src->getOutputSamples(&buffer[0], thisBlock);
			else     synth.render(&buffer[0], thisBlock);
			const double t1 = monotonicSeconds();

			const double blockSeconds = t1 - t0;
			const double blockAudio = double(thisBlock) / outRate;
			const double blockRtf = blockSeconds / blockAudio;

			unsigned int active = 0;
			synth.getPartialStates(&partialStates[0]);
			for (unsigned int i = 0; i < maxPartials; i++)
				if (partialStates[i] != PartialState_INACTIVE) active++;

			if (framesDone >= warmupFrames) {
				blocksCounted++;
				totalRenderSeconds += blockSeconds;
				if (blockSeconds > worstBlockSeconds) {
					worstBlockSeconds = blockSeconds;
					worstBlockRtf = blockRtf;
					worstBlockIndex = blockIndex;
				}
				if (active > peakPartials) peakPartials = active;
				if (active < minPartials) minPartials = active;
				if (csv) fprintf(csv, "%llu,%.6f,%.9f,%.6f,%u\n",
					(unsigned long long)blockIndex, double(framesDone) / outRate,
					blockSeconds, blockRtf, active);
			}
		}

		framesDone += thisBlock;
		blockIndex++;
	}

	const double runEnd = countMode ? 0.0 : monotonicSeconds();
	if (csv) fclose(csv);

	/* Confirm the workload survived the whole render. */
	unsigned int activeAtEnd = 0;
	synth.getPartialStates(&partialStates[0]);
	for (unsigned int i = 0; i < maxPartials; i++)
		if (partialStates[i] != PartialState_INACTIVE) activeAtEnd++;

	const double audioSeconds = double(totalFrames - warmupFrames) / outRate;

	printf("\n=== result (SYNTHETIC ROMS) ===\n");
	printf("audio rendered            : %.4f s (%llu frames @ %.0f Hz, %llu blocks of %u)\n",
		audioSeconds, (unsigned long long)(totalFrames - warmupFrames), outRate,
		(unsigned long long)blockIndex, blockFrames);
	printf("active partials at end    : %u (requested %u)\n", activeAtEnd, targetPartials);
	if (retriggers) printf("retriggers                : %lu\n", retriggers);

	if (countMode) {
		printf("count mode: no host timing taken. Instruction count comes from outside.\n");
		/* Same machine-readable line, with the timing fields marked absent. */
		printf("SYNTHRESULT partials=%u active=%u frames=%llu blocks=%llu block_frames=%u "
		       "out_rate=%.0f audio_s=%.6f cpu_s=-1 rtf=-1 worst_rtf=-1 "
		       "ns_per_frame=-1 renderer=%s reverb=%s waveform=%s structure=%d\n",
			targetPartials, activeAtEnd, (unsigned long long)(totalFrames - warmupFrames),
			(unsigned long long)blockIndex, blockFrames, outRate, audioSeconds,
			rendererType == RendererType_FLOAT ? "float" : "int",
			reverb ? "on" : "off", waveform ? "saw" : "square", structure);
	} else {
		const double rtf = audioSeconds > 0.0 ? totalRenderSeconds / audioSeconds : 0.0;
		printf("render CPU time           : %.6f s\n", totalRenderSeconds);
		printf("wall clock incl. overhead : %.6f s\n", runEnd - runStart);
		printf("real-time factor (overall): %.6f\n", rtf);
		printf("worst-case block RTF      : %.6f  (block %llu)\n",
			worstBlockRtf, (unsigned long long)worstBlockIndex);
		printf("ns per output frame       : %.2f\n",
			audioSeconds > 0.0 ? totalRenderSeconds * 1e9 / (audioSeconds * outRate) : 0.0);
		printf("active partials min/peak  : %u/%u over %llu counted blocks\n",
			minPartials > maxPartials ? 0 : minPartials, peakPartials,
			(unsigned long long)blocksCounted);
		/* One machine-readable line, for bench/estimate.sh. */
		printf("SYNTHRESULT partials=%u active=%u frames=%llu blocks=%llu block_frames=%u "
		       "out_rate=%.0f audio_s=%.6f cpu_s=%.6f rtf=%.6f worst_rtf=%.6f "
		       "ns_per_frame=%.4f renderer=%s reverb=%s waveform=%s structure=%d\n",
			targetPartials, activeAtEnd, (unsigned long long)(totalFrames - warmupFrames),
			(unsigned long long)blocksCounted, blockFrames,
			outRate, audioSeconds, totalRenderSeconds, rtf, worstBlockRtf,
			audioSeconds > 0.0 ? totalRenderSeconds * 1e9 / (audioSeconds * outRate) : 0.0,
			rendererType == RendererType_FLOAT ? "float" : "int",
			reverb ? "on" : "off", waveform ? "saw" : "square", structure);
	}

	printf("\n!! SYNTHETIC ROMS. This is a cost model input, not an MT-32 RTF.\n");
	printf("!! It does not close docs/PLAN.md section 0's gate. See bench/ANALYSIS.md section 9.\n");

	synth.close();
	delete src;
	ROMImage::freeROMImage(ctrlImage);
	ROMImage::freeROMImage(pcmImage);
	return 0;
}
