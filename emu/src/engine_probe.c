/* engine_probe.c - an engine that exists to catch the render loop lying.
 *
 * `port/host/engine_fake.c` is a synthesiser that is not one. This is not even
 * that: it is an instrument clipped onto the seam, and it is here because
 * every assertion `emu/` could make before it existed was a *count*. A count
 * cannot tell you that the 254-byte patch dump the synth received is the same
 * 254 bytes that went onto the wire -- only that one sysex arrived. The whole
 * point of the `rtsysex` vector is that real-time bytes wedged into the middle
 * of a dump must not change the dump, and no counter can express that.
 *
 * So this engine hashes what it is given (FNV-1a, order-sensitive, over every
 * sysex payload byte including the F0 and the F7) and main() compares that
 * hash against the one `tools/gen_vectors.py` computed from an independent
 * model of the parser's written contract. Two implementations agreeing on a
 * 32-bit hash of 16 kB of patch data is a much stronger statement than
 * "sysex messages 64".
 *
 * It also does the opposite job: it can misbehave on purpose.
 *
 *   --engine-queue N   pretend the synth's event queue holds only N events and
 *                      drains one per render() call. mt32emu's real queue is
 *                      1024 entries by default (Synth.cpp, MidiEventQueue) and
 *                      `port/host/engine_fake.c`'s is 64; both are large
 *                      enough that the render loop's back-pressure path is
 *                      almost never taken, which means it is almost never
 *                      tested. This makes it take it.
 *
 * WHAT THIS IS NOT. It renders a cheap deterministic ramp, not audio. Nothing
 * about it says anything about mt32emu's behaviour, and the PCM cross-checks
 * in test.sh deliberately stay on `--engine fake`, whose output is the same
 * bytes on x86-64 and on ARM.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include <stddef.h>

#include "mtp_engine.h"
#include "mtp_log.h"
#include "mtp_time.h"

#define PROBE_RATE 32000u       /* same as MT32EMU_SAMPLE_RATE */

struct mtp_engine {
    uint32_t out_rate;
    uint32_t rendered;          /* in PROBE_RATE ticks */
    uint32_t frac;              /* output-rate to PROBE_RATE accumulator */
    uint16_t phase;
};

static struct mtp_engine g_inst;   /* static: open() must not allocate here */

/* ---- what it saw ------------------------------------------------------- */
static uint32_t g_sysex_hash;
static uint32_t g_sysex_bytes;
static uint32_t g_sysex_msgs;
static uint32_t g_sysex_longest;
static uint32_t g_short_msgs;
static uint32_t g_short_hash;
/* Counted apart from short messages, because they arrive through the same
 * call. mtp_render.c hands MTP_MSG_REALTIME to engine->short_msg() and does
 * NOT count it in stats.short_msgs -- so on a real synth every Active Sensing
 * byte takes an event-queue slot that no counter above the seam accounts for.
 * That is worth knowing when sizing the queue: an interface emitting Active
 * Sensing at the usual 300 ms interval adds ~3.3 events/s, and a sequencer
 * sending MIDI Clock at 120 bpm adds 48/s. */
static uint32_t g_realtime_msgs;
/* Controller messages that must never be lost. 0xBn 78/79/7B: All Sound Off,
 * Reset All Controllers, All Notes Off. A dropped one of these is a note that
 * hangs until the box is power-cycled, which is the single most user-visible
 * failure this whole layer can have. */
static uint32_t g_panic_msgs;

/* ---- how it misbehaves ------------------------------------------------- */
static uint32_t g_queue_cap;       /* 0 = unlimited */
static uint32_t g_queue_used;
static uint32_t g_refusals;

void probe_set_queue_cap(uint32_t n) { g_queue_cap = n; }

uint32_t probe_sysex_hash(void)    { return g_sysex_hash; }
uint32_t probe_sysex_bytes(void)   { return g_sysex_bytes; }
uint32_t probe_sysex_msgs(void)    { return g_sysex_msgs; }
uint32_t probe_sysex_longest(void) { return g_sysex_longest; }
uint32_t probe_short_msgs(void)    { return g_short_msgs; }
uint32_t probe_realtime_msgs(void) { return g_realtime_msgs; }
uint32_t probe_short_hash(void)    { return g_short_hash; }
uint32_t probe_panic_msgs(void)    { return g_panic_msgs; }
uint32_t probe_refusals(void)      { return g_refusals; }

static mtp_status probe_open(const mtp_engine_config *cfg, mtp_engine **out)
{
    g_inst.out_rate = cfg && cfg->output_rate ? cfg->output_rate : 48000u;
    g_inst.rendered = 0u;
    g_inst.frac     = 0u;
    g_inst.phase    = 0u;
    g_sysex_hash = 2166136261u;
    g_short_hash = 2166136261u;
    g_sysex_bytes = g_sysex_msgs = g_sysex_longest = 0u;
    g_short_msgs = g_panic_msgs = g_queue_used = g_refusals = 0u;
    g_realtime_msgs = 0u;
    *out = &g_inst;
    MTP_LOGI("engine: probe (hashes what it is handed; queue cap %u)",
             g_queue_cap);
    return MTP_OK;
}

static void probe_close(mtp_engine *e) { (void)e; }

static uint32_t probe_timebase(mtp_engine *e) { (void)e; return PROBE_RATE; }
static uint32_t probe_rendered(mtp_engine *e) { return e->rendered; }

/* The bounded-queue model: one event leaves per render() call, which is the
 * worst case for a synth that applies its queue at block granularity. */
static int queue_take(void)
{
    if (g_queue_cap == 0u) return 1;
    if (g_queue_used >= g_queue_cap) { g_refusals++; return 0; }
    g_queue_used++;
    return 1;
}

static mtp_status probe_short(mtp_engine *e, uint32_t msg, uint32_t ts)
{
    (void)e; (void)ts;
    if (!queue_take()) return MTP_ERR_AGAIN;
    if ((msg & 0xFFu) >= 0xF8u) { g_realtime_msgs++; return MTP_OK; }
    g_short_msgs++;
    g_short_hash = (g_short_hash ^ (msg & 0xFFFFFFu)) * 16777619u;
    if ((msg & 0xF0u) == 0xB0u) {
        uint32_t cc = (msg >> 8) & 0x7Fu;
        if (cc == 120u || cc == 121u || cc == 123u) g_panic_msgs++;
    }
    return MTP_OK;
}

static mtp_status probe_sysex(mtp_engine *e, const uint8_t *data, uint32_t len,
                              uint32_t ts)
{
    uint32_t i, h;
    (void)e; (void)ts;
    if (!queue_take()) return MTP_ERR_AGAIN;
    h = g_sysex_hash;
    for (i = 0; i < len; i++) h = (h ^ (uint32_t)data[i]) * 16777619u;
    g_sysex_hash = h;
    g_sysex_bytes += len;
    g_sysex_msgs++;
    if (len > g_sysex_longest) g_sysex_longest = len;
    return MTP_OK;
}

static void probe_render(mtp_engine *e, int16_t *stereo, uint32_t frames)
{
    uint32_t i;
    /* A deterministic ramp. Cheap on purpose: this engine is used for the
     * failure-mode cases, where the thing under test is the loop around it. */
    for (i = 0; i < frames; i++) {
        int16_t v = (int16_t)((int32_t)(e->phase << 6) - 32768);
        stereo[2 * i + 0] = v;
        stereo[2 * i + 1] = (int16_t)-v;
        e->phase = (uint16_t)((e->phase + 7u) & 0x1FFu);
    }
    /* Advance the engine's 32 kHz clock by the right number of ticks for
     * `frames` output frames, exactly as engine_fake.c does, so timestamps
     * behave the same. */
    {
        uint64_t t = (uint64_t)frames * PROBE_RATE + g_inst.frac;
        e->rendered += (uint32_t)(t / e->out_rate);
        g_inst.frac  = (uint32_t)(t % e->out_rate);
    }
    if (g_queue_used) g_queue_used--;
}

static void probe_display(mtp_engine *e, char *dst21)
{
    static const char T[] = "probe               ";
    int i;
    (void)e;
    for (i = 0; i < 20; i++) dst21[i] = T[i];
    dst21[20] = 0;
}

const mtp_engine_vtable mtp_engine_probe = {
    "probe",
    probe_open,
    probe_close,
    probe_timebase,
    probe_rendered,
    probe_short,
    probe_sysex,
    probe_render,
    probe_display
};
