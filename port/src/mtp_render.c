/* mtp_render.c - the render loop. See DESIGN.md, "How the render loop stays
 * ahead of DMA". SPDX-License-Identifier: 0BSD */

#include "mtp_render.h"
#include "mtp_audio.h"
#include "mtp_midi.h"
#include "mtp_time.h"
#include "mtp_log.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* MIDI -> engine                                                     */

static uint32_t engine_now_plus(mtp_render_ctx *ctx)
{
    uint32_t now = ctx->engine->rendered_samples(ctx->inst);
    if (ctx->lookahead_frames == 0u) return now;
    {
        /* lookahead is expressed in output frames; convert to engine ticks */
        uint64_t rate = ctx->engine->timebase_rate(ctx->inst);
        uint64_t ahead = (uint64_t)ctx->lookahead_frames * rate
                       / (ctx->output_rate ? ctx->output_rate : 1u);
        return now + (uint32_t)ahead;
    }
}

static mtp_sink_result on_message(const mtp_midi_msg *m, void *user)
{
    mtp_render_ctx *ctx = (mtp_render_ctx *)user;
    uint32_t ts = engine_now_plus(ctx);

    switch (m->kind) {
    case MTP_MSG_REALTIME:
        /* Clock, start, stop, active sensing. The MT-32 ignores all of them
         * except that Active Sensing timeout is a real feature of hardware
         * units; we do not emulate it. Pass them through so the engine can
         * count them, but do not let them occupy a queue slot on failure --
         * and never stall the stream on one. A refused real-time byte is
         * dropped on purpose: it carries no stream position, and an MPU-401
         * sends Active Sensing every 300 ms for ever. */
        (void)ctx->engine->short_msg(ctx->inst, m->msg, ts);
        break;

    case MTP_MSG_SHORT:
        ctx->stats.short_msgs++;
        if (ctx->engine->short_msg(ctx->inst, m->msg, ts) != MTP_OK) {
            /* Hold it and stop. Dropping a short message is not the small
             * loss it looks like: a lost note-on is a missing note, but a
             * lost All Notes Off is a note that sounds until the box is
             * power-cycled. */
            ctx->stats.engine_backpressure++;
            ctx->retry_kind = MTP_RETRY_SHORT;
            ctx->retry_msg  = m->msg;
            ctx->retry_ts   = ts;
            return MTP_SINK_STOP;
        }
        break;

    case MTP_MSG_SYSEX:
        ctx->stats.sysex_msgs++;
        if (ctx->engine->sysex(ctx->inst, m->sysex, m->sysex_len, ts) != MTP_OK) {
            /* The engine's sysex storage is full. Hold it and stop rather
             * than dropping a patch dump on the floor. One slot is enough
             * because stopping guarantees nothing else is emitted until this
             * one is accepted. */
            ctx->stats.engine_backpressure++;
            if (m->sysex_len <= sizeof(ctx->retry_buf)) {
                memcpy(ctx->retry_buf, m->sysex, m->sysex_len);
                ctx->retry_len  = m->sysex_len;
                ctx->retry_ts   = ts;
                ctx->retry_kind = MTP_RETRY_SYSEX;
                return MTP_SINK_STOP;
            }
            /* Unreachable while retry_buf is MTP_SYSEX_MAX, which is also the
             * parser's cap -- but a shrunken buffer must not corrupt memory. */
            MTP_LOGW("sysex dropped (%u bytes)", (unsigned)m->sysex_len);
        }
        break;
    }
    return MTP_SINK_CONTINUE;
}

/* Re-offer the one held message. True if the engine took it (or there was
 * none), false if it is still refusing and we must not parse another byte. */
static int retry_flush(mtp_render_ctx *ctx)
{
    mtp_status st;

    if (ctx->retry_kind == MTP_RETRY_NONE) return 1;

    if (ctx->retry_kind == MTP_RETRY_SHORT) {
        st = ctx->engine->short_msg(ctx->inst, ctx->retry_msg, ctx->retry_ts);
    } else {
        st = ctx->engine->sysex(ctx->inst, ctx->retry_buf, ctx->retry_len,
                                ctx->retry_ts);
    }
    if (st != MTP_OK) return 0;

    ctx->retry_kind = MTP_RETRY_NONE;
    ctx->retry_len  = 0;
    return 1;
}

static void drain_midi(mtp_render_ctx *ctx)
{
    mtp_midi_byte batch[MTP_MIDI_BATCH];
    size_t n, used;

    /* Order is the whole point of this function. Three sources of bytes, and
     * they must be offered in exactly this sequence:
     *
     *   1. the message the engine refused last time,
     *   2. the bytes that arrived after it and have never been parsed,
     *   3. whatever is new on the wire.
     *
     * Getting this wrong does not fail loudly -- it silently reorders the
     * stream, so a patch dump lands after the notes it was supposed to
     * change. See DESIGN.md 3.5. */

    if (!retry_flush(ctx)) return;          /* still refusing: parse nothing */

    if (ctx->held_n != 0u) {
        used = mtp_midi_parser_feed(&ctx->parser, ctx->held, ctx->held_n);
        if (used < ctx->held_n) {
            /* Stalled again inside the held bytes. Keep the remainder. */
            memmove(ctx->held, ctx->held + used,
                    (ctx->held_n - used) * sizeof(ctx->held[0]));
            ctx->held_n -= (uint32_t)used;
            return;
        }
        ctx->held_n = 0u;
    }

    for (;;) {
        n = mtp_midi_read(batch, MTP_MIDI_BATCH);
        if (n == 0u) break;
        ctx->stats.midi_bytes += (uint32_t)n;
        used = mtp_midi_parser_feed(&ctx->parser, batch, n);
        if (used < n) {
            /* The engine refused something mid-batch. Everything the parser
             * has not seen waits here, in order, until it is accepted. */
            ctx->held_n = (uint32_t)(n - used);
            memcpy(ctx->held, batch + used,
                   ctx->held_n * sizeof(ctx->held[0]));
            return;
        }
        if (n < MTP_MIDI_BATCH) break;
    }
}

/* ------------------------------------------------------------------ */

mtp_status mtp_render_init(mtp_render_ctx *ctx,
                           const mtp_engine_vtable *engine,
                           mtp_engine *inst,
                           uint32_t frames_per_block,
                           uint32_t output_rate,
                           uint32_t target_queued,
                           uint32_t lookahead_frames)
{
    if (!ctx || !engine || !inst || frames_per_block == 0u)
        return MTP_ERR_INVAL;

    memset(ctx, 0, sizeof(*ctx));
    ctx->engine           = engine;
    ctx->inst             = inst;
    ctx->frames_per_block = frames_per_block;
    ctx->output_rate      = output_rate;
    ctx->target_queued    = target_queued;
    ctx->lookahead_frames = lookahead_frames;
    ctx->stats.min_queued = 0xFFFFFFFFu;
    mtp_midi_parser_init(&ctx->parser, ctx->sysex_buf, MTP_SYSEX_MAX,
                         on_message, ctx);
    return MTP_OK;
}

unsigned mtp_render_pump(mtp_render_ctx *ctx, unsigned max_blocks)
{
    unsigned produced = 0;

    while (produced < max_blocks) {
        int16_t *block;
        uint32_t t0, t1, t2;
        unsigned queued;

        /* MIDI first, always: an event that arrives one microsecond before we
         * render a block should land in that block, not the next one. */
        t0 = mtp_time_us();
        drain_midi(ctx);

        block = mtp_audio_acquire();
        if (block == NULL) break;            /* ring full: we are ahead */

        t1 = mtp_time_us();
        ctx->engine->render(ctx->inst, block, ctx->frames_per_block);
        t2 = mtp_time_us();

        mtp_audio_commit();
        produced++;
        ctx->stats.blocks++;

        if ((t2 - t1) > ctx->stats.worst_render_us)
            ctx->stats.worst_render_us = t2 - t1;
        if ((t2 - t0) > ctx->stats.worst_block_us)
            ctx->stats.worst_block_us = t2 - t0;

        queued = mtp_audio_queued();
        if (queued < ctx->stats.min_queued) ctx->stats.min_queued = queued;

        /* Once we are the configured distance ahead of the DMA, stop. Filling
         * the ring to the brim would only add latency; leaving a block free
         * means an interrupt can be serviced without us spinning. */
        if (queued >= ctx->target_queued) break;
    }

    ctx->stats.underruns = mtp_audio_underruns();
    return produced;
}

void mtp_render_run(mtp_render_ctx *ctx, uint32_t max_blocks)
{
    uint32_t idle_blocks = 0;

    while (ctx->stats.blocks < max_blocks) {
        unsigned n = mtp_render_pump(ctx, 8);
        if (n == 0u) {
            /* Ring is full. On the target this is a WFI; on the host the stub
             * returns immediately and we simply loop. */
            if (mtp_audio_wait(5000u) != MTP_OK) {
                MTP_LOGW("audio sink stalled");
                break;
            }
            continue;
        }
        if (mtp_midi_eof()) {
            /* Source exhausted: keep rendering for a while so releases and
             * reverb tails finish, then stop. 1 second at block granularity. */
            idle_blocks += n;
            if (idle_blocks * ctx->frames_per_block > ctx->output_rate) break;
        } else {
            idle_blocks = 0;
        }
    }
}
