/* mtp_render.h - the render loop. Portable; the same code runs on the host
 * stub and on the T113. SPDX-License-Identifier: 0BSD */
#ifndef MTP_RENDER_H
#define MTP_RENDER_H

#include "mtp_platform.h"
#include "mtp_engine.h"
#include "mtp_midi_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t blocks;              /* audio blocks committed                */
    uint32_t underruns;           /* from the audio sink                   */
    uint32_t midi_bytes;
    uint32_t short_msgs;
    uint32_t sysex_msgs;
    uint32_t engine_backpressure; /* times the engine queue was full       */
    /* Real-time bytes (Clock, Start, Stop, Active Sensing) the engine refused.
     * They are single bytes with no retry -- a refused one is gone. Nothing
     * counted them before, so a synth losing MIDI Clock under load looked
     * healthy; this is the counter that says otherwise. */
    uint32_t realtime_dropped;
    uint32_t worst_render_us;     /* worst single-block render time        */
    uint32_t worst_block_us;      /* worst whole iteration: MIDI drain plus
                                   * render. The one to watch against the
                                   * block period -- see DESIGN.md 2.4      */
    /* Lowest ring occupancy seen after a commit -- the real safety margin.
     *
     * Sampled ONLY once the ring has first reached target_queued. The ring
     * necessarily passes through occupancy 1 while it fills from empty at
     * start-up, so a counter that included those commits read 1 on every run
     * that ever started and said nothing at all about the steady state. If it
     * is still MTP_RENDER_MIN_QUEUED_NONE at the end of a run, the ring never
     * got as far as target_queued -- which is itself the answer, and means
     * either a sink that drains instantly (a free-running harness) or a loop
     * that never caught up. startup_blocks says how long filling took. */
    uint32_t min_queued;
    uint32_t startup_blocks;      /* blocks committed before the ring first
                                   * reached target_queued                  */
    /* Times mtp_audio_wait() timed out with the ring still full: the sink did
     * not ask for a block within the timeout. Should be 0. See the note on
     * mtp_render_run() below for what happens when it is not. */
    uint32_t sink_stalls;
} mtp_render_stats;

/* min_queued when the ring never reached target_queued. */
#define MTP_RENDER_MIN_QUEUED_NONE 0xFFFFFFFFu

/* THIS STRUCTURE IS ~64 kB AND MUST NOT GO ON A STACK. It holds two
 * MTP_SYSEX_MAX (32768 B) buffers inline: one for the parser to reassemble
 * into, one to stage a sysex the engine refused. Make it static, or put it in
 * an arena -- port/host/main.c, desktop/src/main.c and emu/src/main.c all
 * declare it `static` for this reason.
 *
 * It matters more on the target than it looks. PORTING.md 4.4 measures
 * mt32emu putting 16-96 kB on the stack depending on configuration and budgets
 * 32 kB for the render context; a task stack that also had to hold this would
 * be a bad day, found at run time, on hardware. */
typedef struct {
    const mtp_engine_vtable *engine;
    mtp_engine              *inst;
    uint32_t                 frames_per_block;
    uint32_t                 output_rate;
    uint32_t                 target_queued; /* blocks to stay ahead by     */
    /* Schedule events this many output frames into the future so that the
     * relative arrival times of bytes within one batch survive. 0 = enqueue
     * at the engine's current position (lowest latency). See DESIGN.md. */
    uint32_t                 lookahead_frames;
    /* 1 once the ring has reached target_queued at least once. Until then
     * min_queued is not sampled; see mtp_render_stats. */
    uint8_t                  primed;
    mtp_midi_parser          parser;
    uint8_t                  sysex_buf[MTP_SYSEX_MAX];
    /* staging for a sysex the engine refused: retried next block */
    uint8_t                  retry_buf[MTP_SYSEX_MAX];
    uint32_t                 retry_len;
    uint32_t                 retry_ts;
    mtp_render_stats         stats;
} mtp_render_ctx;

mtp_status mtp_render_init(mtp_render_ctx *ctx,
                           const mtp_engine_vtable *engine,
                           mtp_engine *inst,
                           uint32_t frames_per_block,
                           uint32_t output_rate,
                           uint32_t target_queued,
                           uint32_t lookahead_frames);

/* One iteration: drain MIDI, fill and commit as many free audio blocks as the
 * ring will take, up to max_blocks. Returns the number committed. Never
 * blocks -- the caller decides whether to wait or to do something else. */
unsigned mtp_render_pump(mtp_render_ctx *ctx, unsigned max_blocks);

/* Panic: silence the engine now, if it can be silenced. Safe to call with an
 * engine whose vtable has no panic(); does nothing then, and says so by
 * returning MTP_ERR_INVAL so a caller can tell "stopped" from "could not".
 * Call it between blocks, never from an interrupt. */
mtp_status mtp_render_panic(mtp_render_ctx *ctx);

/* THIS IS A HARNESS LOOP, NOT A DEVICE MAIN LOOP -- despite the name. It runs
 * until the MIDI source reports EOF and the engine has gone quiet, or until
 * max_blocks have been produced, and then it returns. A device has no EOF and
 * no block budget, so the T113 build calls mtp_render_pump() inside its own
 * superloop instead (DESIGN.md 6.4) and never calls this.
 *
 * A stalled sink does NOT end the run. It used to: one mtp_audio_wait()
 * timeout broke the loop and logged a line. That is wrong even here -- a
 * desktop device can legitimately stall on a suspend, a sample-rate change or
 * an interface being unplugged, and it is worse on a device, where it would
 * mean audio stops for ever because of one late interrupt, with one line on a
 * console nobody is watching. So a timeout is counted in stats.sink_stalls,
 * logged once, and the loop keeps trying.
 *
 * It gives up only when the sink has been silent for MTP_RENDER_STALL_GIVEUP_US
 * with no block consumed at all, which is a dead sink rather than a late one;
 * it logs an error naming the number and returns, because a harness that hangs
 * is worse than a harness that stops. */
void mtp_render_run(mtp_render_ctx *ctx, uint32_t max_blocks);

/* How long mtp_render_run() keeps trying a sink that never asks for a block.
 * Two seconds: far longer than any scheduling hiccup, far shorter than a
 * test-suite timeout. */
#define MTP_RENDER_STALL_GIVEUP_US 2000000u

#ifdef __cplusplus
}
#endif

#endif /* MTP_RENDER_H */
