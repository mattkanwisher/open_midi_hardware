/* engine_fluidsynth.c - the General MIDI / GS engine behind mtp_engine.
 *
 * mt32emu covers the LA half of the module; this is the other half: a SoundFont
 * sampler, so that a game's "SC-55 / GS" mode has something to talk to. It is
 * not an SC-55 -- docs/background.md costs Nuked-SC55 at a whole extra core and
 * rules it out -- it is FluidSynth with a GS-flavoured bank such as GeneralUser
 * GS, which is what mt32-pi does in its GM mode too.
 *
 * FluidSynth has no per-event sample timestamp the way mt32emu's playMsg has,
 * so timing is done here, the way host/engine_fake.c does it: a fixed-size
 * event queue stamped in output frames, drained inside render() in sub-blocks
 * so that a note lands within SUB_FRAMES of where the render loop put it. The
 * queue, the sysex store and the synth are all allocated at open() and never
 * again -- the same contract the other two engines are held to.
 *
 * What is deliberately not here: the sequencer API (it wants milliseconds and
 * its own thread), the audio driver (we own the device), and the MIDI router.
 * Just the synth object, driven by the six calls in mtp_engine.h.
 *
 * SPDX-License-Identifier: 0BSD */

#include "mtp_engine.h"
#include "mtp_log.h"

#include <fluidsynth.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QUEUE_SIZE    512        /* events in flight; a GS reset burst is ~50 */
#define SYSEX_STORAGE 65536      /* same as engine_mt32emu's sysex ring       */
#define SUB_FRAMES    32         /* event timing granularity inside a block   */
#define POLYPHONY     128        /* SC-55 is 24 voices; 128 is FluidSynth's   */
                                 /* comfortable default at this sample rate   */

typedef struct {
    uint32_t t_frames;
    uint32_t msg;          /* 0 => sysex */
    uint32_t sysex_off;
    uint32_t sysex_len;
} event;

struct mtp_engine {
    fluid_settings_t *settings;
    fluid_synth_t    *synth;
    int               sfont_id;
    uint32_t          out_rate;
    uint32_t          rendered;         /* output frames since open()       */
    event             q[QUEUE_SIZE];
    unsigned          q_head, q_tail;
    uint8_t           sysex[SYSEX_STORAGE];
    uint32_t          sysex_used;
    char              lcd[21];
};

static unsigned q_next(unsigned i) { return (i + 1u) % QUEUE_SIZE; }

/* FluidSynth's own diagnostics through mtp_log, not stderr. */
static void fs_log(int level, const char *msg, void *user)
{
    (void)user;
    switch (level) {
    case FLUID_PANIC:
    case FLUID_ERR:  mtp_log(MTP_LOG_ERROR, "fluidsynth: %s", msg); break;
    case FLUID_WARN: mtp_log(MTP_LOG_WARN,  "fluidsynth: %s", msg); break;
    case FLUID_INFO: mtp_log(MTP_LOG_INFO,  "fluidsynth: %s", msg); break;
    default:         mtp_log(MTP_LOG_DEBUG, "fluidsynth: %s", msg); break;
    }
}

static mtp_status fs_open(const mtp_engine_config *cfg, mtp_engine **out)
{
    mtp_engine *e;
    const char *sf = cfg ? cfg->soundfont_path : NULL;
    const char *name;

    if (!sf || !*sf) {
        MTP_LOGE("fluidsynth engine needs a SoundFont (--soundfont FILE)");
        return MTP_ERR_INVAL;
    }
    e = (mtp_engine *)calloc(1, sizeof(*e));
    if (!e) return MTP_ERR_NOMEM;
    e->out_rate = cfg->output_rate ? cfg->output_rate : 48000u;

    fluid_set_log_function(FLUID_PANIC, fs_log, NULL);
    fluid_set_log_function(FLUID_ERR,   fs_log, NULL);
    fluid_set_log_function(FLUID_WARN,  fs_log, NULL);
    fluid_set_log_function(FLUID_INFO,  fs_log, NULL);
    fluid_set_log_function(FLUID_DBG,   fs_log, NULL);

    e->settings = new_fluid_settings();
    if (!e->settings) { free(e); return MTP_ERR_NOMEM; }

    fluid_settings_setnum(e->settings, "synth.sample-rate", (double)e->out_rate);
    fluid_settings_setint(e->settings, "synth.polyphony",   POLYPHONY);
    /* GS semantics for bank select: CC0 is the bank, CC32 is ignored, and
     * channel 10 is drums unless a GS sysex says otherwise. This is what an
     * SC-55 does and what a game written for one expects. */
    fluid_settings_setstr(e->settings, "synth.midi-bank-select", "gs");
    /* 0x10 is the Roland device ID every GS sysex is addressed to. */
    fluid_settings_setint(e->settings, "synth.device-id", 0x10);
    fluid_settings_setint(e->settings, "synth.reverb.active", cfg->reverb_enabled ? 1 : 0);
    fluid_settings_setint(e->settings, "synth.chorus.active", cfg->reverb_enabled ? 1 : 0);
    /* FluidSynth's default gain of 0.2 is quiet next to mt32emu's output;
     * 0.5 puts the two engines in the same ballpark without clipping GS
     * banks that are mastered hot. */
    fluid_settings_setnum(e->settings, "synth.gain", 0.5);
    /* One render thread: ours. */
    fluid_settings_setint(e->settings, "synth.cpu-cores", 1);

    e->synth = new_fluid_synth(e->settings);
    if (!e->synth) {
        MTP_LOGE("new_fluid_synth failed");
        delete_fluid_settings(e->settings);
        free(e);
        return MTP_ERR_NOMEM;
    }

    e->sfont_id = fluid_synth_sfload(e->synth, sf, 1);
    if (e->sfont_id == FLUID_FAILED) {
        MTP_LOGE("SoundFont '%s': could not load", sf);
        delete_fluid_synth(e->synth);
        delete_fluid_settings(e->settings);
        free(e);
        return MTP_ERR_IO;
    }
    name = NULL;
    {
        fluid_sfont_t *s = fluid_synth_get_sfont_by_id(e->synth, e->sfont_id);
        if (s) name = fluid_sfont_get_name(s);
    }
    /* Start in GS mode, as an SC-55 does at power-on; a game that sends
     * GS Reset itself gets the same result. */
    fluid_synth_system_reset(e->synth);

    snprintf(e->lcd, sizeof(e->lcd), "%-20.20s", "GS  GeneralUser");
    MTP_LOGI("SoundFont '%s': %s", sf, name ? name : "(unnamed)");
    MTP_LOGI("engine: fluidsynth %s, GS bank select, %d voices, %u Hz out",
             FLUIDSYNTH_VERSION, POLYPHONY, e->out_rate);
    *out = e;
    return MTP_OK;
}

static void fs_close(mtp_engine *e)
{
    if (!e) return;
    delete_fluid_synth(e->synth);
    delete_fluid_settings(e->settings);
    free(e);
}

/* The engine's clock is the output clock: no 32 kHz / 48 kHz split here. */
static uint32_t fs_timebase(mtp_engine *e) { return e->out_rate; }
static uint32_t fs_rendered(mtp_engine *e) { return e->rendered; }

static mtp_status fs_short(mtp_engine *e, uint32_t msg, uint32_t ts)
{
    uint8_t status = (uint8_t)(msg & 0xFFu);
    /* System realtime and common: an SC-55 ignores them for our purposes,
     * and they must not take a queue slot (mtp_render.c, MTP_MSG_REALTIME). */
    if (status >= 0xF0u) return MTP_OK;
    if (q_next(e->q_tail) == e->q_head) return MTP_ERR_AGAIN;
    e->q[e->q_tail].t_frames  = ts;
    e->q[e->q_tail].msg       = msg;
    e->q[e->q_tail].sysex_len = 0;
    e->q_tail = q_next(e->q_tail);
    return MTP_OK;
}

static mtp_status fs_sysex(mtp_engine *e, const uint8_t *data, uint32_t len,
                           uint32_t ts)
{
    if (q_next(e->q_tail) == e->q_head) return MTP_ERR_AGAIN;
    if (e->sysex_used + len > SYSEX_STORAGE) {
        /* Storage is reclaimed when the queue drains (see apply). Until then
         * the render loop holds the message and retries: back-pressure, not
         * a dropped GS reset. */
        return MTP_ERR_AGAIN;
    }
    memcpy(e->sysex + e->sysex_used, data, len);
    e->q[e->q_tail].t_frames  = ts;
    e->q[e->q_tail].msg       = 0;
    e->q[e->q_tail].sysex_off = e->sysex_used;
    e->q[e->q_tail].sysex_len = len;
    e->sysex_used += len;
    e->q_tail = q_next(e->q_tail);
    return MTP_OK;
}

static void apply(mtp_engine *e, const event *ev)
{
    if (ev->sysex_len) {
        /* fluid_synth_sysex wants the payload without F0 and F7
         * (fluidsynth/synth.h); the parser hands us the whole frame. */
        const uint8_t *d = e->sysex + ev->sysex_off;
        uint32_t n = ev->sysex_len;
        int handled = 0;
        if (n >= 2u && d[0] == 0xF0u) { d++; n--; }
        if (n >= 1u && d[n - 1u] == 0xF7u) n--;
        fluid_synth_sysex(e->synth, (const char *)d, (int)n, NULL, NULL,
                          &handled, 0);
        if (e->q_head == e->q_tail) e->sysex_used = 0;
        return;
    }
    {
        int st = (int)(ev->msg & 0xF0u);
        int ch = (int)(ev->msg & 0x0Fu);
        int d1 = (int)((ev->msg >> 8) & 0x7Fu);
        int d2 = (int)((ev->msg >> 16) & 0x7Fu);
        switch (st) {
        case 0x80: fluid_synth_noteoff(e->synth, ch, d1); break;
        case 0x90: if (d2) fluid_synth_noteon(e->synth, ch, d1, d2);
                   else    fluid_synth_noteoff(e->synth, ch, d1);
                   break;
        case 0xA0: fluid_synth_key_pressure(e->synth, ch, d1, d2); break;
        case 0xB0: fluid_synth_cc(e->synth, ch, d1, d2); break;
        case 0xC0: fluid_synth_program_change(e->synth, ch, d1); break;
        case 0xD0: fluid_synth_channel_pressure(e->synth, ch, d1); break;
        case 0xE0: fluid_synth_pitch_bend(e->synth, ch, d1 | (d2 << 7)); break;
        default: break;
        }
    }
}

static void fs_render(mtp_engine *e, int16_t *stereo, uint32_t frames)
{
    uint32_t done = 0;
    while (done < frames) {
        uint32_t n = frames - done;
        if (n > SUB_FRAMES) n = SUB_FRAMES;
        /* Everything due at or before this sub-block goes in first. */
        while (e->q_head != e->q_tail) {
            int32_t delta = (int32_t)(e->q[e->q_head].t_frames - e->rendered);
            if (delta > 0) break;
            apply(e, &e->q[e->q_head]);
            e->q_head = q_next(e->q_head);
            if (e->q_head == e->q_tail) e->sysex_used = 0;
        }
        fluid_synth_write_s16(e->synth, (int)n,
                              stereo + done * 2u, 0, 2,
                              stereo + done * 2u, 1, 2);
        e->rendered += n;
        done += n;
    }
}

static void fs_display(mtp_engine *e, char *dst21)
{
    memcpy(dst21, e->lcd, 21);
}

/* Unity here is the 0.5 chosen at open(), so that --gain 1.0 means the same
 * loudness whichever engine is behind the seam. fluid_synth_set_gain is the
 * synth's own master, applied before reverb and chorus, so there is no second
 * volume control in the ring fighting it (mtp_engine.h). */
static void fs_set_gain(mtp_engine *e, float gain)
{
    fluid_synth_set_gain(e->synth, 0.5f * gain);
}

/* Panic: drop what has not been applied yet, then stop what has. all_notes_off
 * lets releases ring; all_sounds_off cuts them (-1 = every channel). */
static void fs_panic(mtp_engine *e)
{
    e->q_head = e->q_tail;
    e->sysex_used = 0;
    fluid_synth_all_notes_off(e->synth, -1);
    fluid_synth_all_sounds_off(e->synth, -1);
}

const mtp_engine_vtable mtp_engine_fluidsynth = {
    "fluidsynth",
    fs_open,
    fs_close,
    fs_timebase,
    fs_rendered,
    fs_short,
    fs_sysex,
    fs_render,
    fs_display,
    fs_set_gain,
    fs_panic
};
