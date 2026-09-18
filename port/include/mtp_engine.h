/* mtp_engine.h - the seam between the render loop and the synthesiser.
 *
 * This is not a "sound engine abstraction layer"; it is the six calls the
 * render loop in src/mtp_render.c actually makes. It exists for exactly one
 * reason: so the whole port -- audio ring, MIDI parser, timing, storage --
 * can be built and run before mt32emu is linked in and before any ROMs exist.
 * src/engine_fake.c is the proof -- it is portable, and emu/ compiles it
 * unmodified for bare metal.
 *
 * Everything maps onto mt32emu 2.8 without adaptation:
 *   short_msg()        -> Synth::playMsg(msg, timestamp)          (Synth.h:384)
 *   sysex()            -> Synth::playSysex(data, len, timestamp)  (Synth.h:387)
 *   render()           -> Synth::render(Bit16s*, len)             (Synth.h:565)
 *   rendered_samples() -> Synth::getInternalRenderedSampleCount() (Synth.h:372)
 *
 * Timestamps are in *engine-native* samples, which for mt32emu is 32000 Hz
 * regardless of the output rate (mt32emu/globals.h:94, Synth.h:370-377). The
 * render loop converts; the engine never sees wall-clock time.
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef MTP_ENGINE_H
#define MTP_ENGINE_H

#include "mtp_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mtp_engine mtp_engine;

typedef struct {
    const char *control_rom_path;  /* NULL for engines that need no ROMs */
    const char *pcm_rom_path;
    /* Second half of a split ROM image, or NULL for a whole one.
     *
     * mt32emu knows about Mux0/Mux1 and FirstHalf/SecondHalf pairs
     * (ROMInfo.h:37-48) and merges them with the two-argument
     * ROMImage::makeROMImage(File*, File*) (ROMInfo.h:108). A 32 kB control
     * ROM dump is one half of such a pair and is useless on its own, and
     * dumps in the wild come that way, so DESIGN.md 4.3 promises to support
     * them. It cannot be done with one path per ROM, which is why these
     * exist -- two pointers, no allocation, no extra call. */
    const char *control_rom_path2;
    const char *pcm_rom_path2;
    uint32_t    output_rate;       /* Hz the engine must produce          */
    uint32_t    max_partials;      /* 32 = a real MT-32                   */
    int         reverb_enabled;
} mtp_engine_config;

typedef struct {
    const char *name;

    mtp_status (*open)(const mtp_engine_config *cfg, mtp_engine **out);
    void       (*close)(mtp_engine *e);

    /* Native sample rate of the engine's internal clock, for timestamps.
     * 32000 for mt32emu. */
    uint32_t   (*timebase_rate)(mtp_engine *e);

    /* Sample count on that clock since open(). Monotonic. */
    uint32_t   (*rendered_samples)(mtp_engine *e);

    /* Enqueue. msg is status | data1<<8 | data2<<16, as mt32emu wants it.
     * Returns MTP_ERR_AGAIN if the engine's queue is full -- the render loop
     * retries next block rather than dropping. */
    mtp_status (*short_msg)(mtp_engine *e, uint32_t msg, uint32_t t_samples);
    mtp_status (*sysex)(mtp_engine *e, const uint8_t *data, uint32_t len,
                        uint32_t t_samples);

    /* Produce exactly `frames` interleaved stereo int16 frames at
     * cfg->output_rate. Must not allocate, must not block, must not fail. */
    void       (*render)(mtp_engine *e, int16_t *stereo, uint32_t frames);

    /* Optional, may be NULL: 20-char MT-32 LCD text, for a display later. */
    void       (*get_display)(mtp_engine *e, char *dst21);

    /* Master output gain, linear, 1.0 = unity. Maps onto
     * Synth::setOutputGain(float) (Synth.h:452). Optional, may be NULL for an
     * engine that has no such control -- callers must check.
     *
     * It lives here rather than in mtp_audio.h on purpose: mt32emu applies
     * gain inside the analogue-circuit emulation, where it belongs, and a
     * multiply in the ring would be a second volume control fighting the
     * first. mtp_audio.h says as much already ("mixing, volume -- the
     * application's problem"). */
    void       (*set_gain)(mtp_engine *e, float gain);

    /* Panic: silence everything now. All notes off, all sound off, every
     * pending event discarded. Optional, may be NULL.
     *
     * Anything that can be stopped needs this. A module with a power switch
     * gets away without it; a desktop build that catches Ctrl-C mid-note, or
     * any future build with a display and an encoder, does not. It is not
     * "send CC 123 from the caller", because the caller cannot flush events
     * that are already queued inside the engine, and a queued Note On behind
     * the All Notes Off would restart the note.
     *
     * Must be callable from the render loop's context, between blocks. Must
     * not allocate and must not block -- same contract as render(). */
    void       (*panic)(mtp_engine *e);
} mtp_engine_vtable;

/* Engines available in this build. */
extern const mtp_engine_vtable mtp_engine_fake;     /* src/engine_fake.c     */
#ifdef MTP_WITH_MT32EMU
extern const mtp_engine_vtable mtp_engine_mt32emu;  /* host/engine_mt32emu.cpp */
#endif

#ifdef __cplusplus
}
#endif

#endif /* MTP_ENGINE_H */
