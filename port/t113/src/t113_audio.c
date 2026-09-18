/* t113_audio.c - mtp_audio.h on I2S1 + DMAC: the real thing the emulator was
 * a stand-in for.
 *
 * emu/src/emu_audio.c has the same ring, the same two monotonic counters and
 * the same barriers, driven by a generic-timer interrupt because `-M virt`
 * has no I2S. Here the interrupt comes from the DMA engine finishing a
 * descriptor, which is the event that file says it was modelling. The code
 * above the seam cannot tell the difference, and that is the whole design.
 *
 * SHAPE (port/DESIGN.md 2.2, 2.3, 2.4):
 *   - `block_count` blocks of `frames_per_block` stereo int16, contiguous, in
 *     the linker's .dma region, which src/mmu.c maps Normal Non-cacheable
 *   - one DMA descriptor per block, in a circular list, `next` of the last
 *     pointing at the first, started once and never stopped
 *   - one PKG completion interrupt per descriptor; its only jobs are to
 *     advance the play cursor and count underruns
 *   - acquire/commit from the render loop only; the ISR never writes
 *     g_committed and the loop never writes g_played, so there is no shared
 *     read-modify-write anywhere and no interrupt is ever disabled
 *
 * WHY THE DMA IS NEVER STOPPED. An I2S engine that stops mid-stream leaves
 * the data line at whatever level it last drove, which is a DC step into the
 * DAC and a thump out of the speaker. So the descriptor list is circular and
 * runs for ever from the first commit to reset. When the renderer falls
 * behind, the hardware replays the stale block -- which is audible, and is
 * counted, and is exactly what mtp_audio_underruns() is defined to mean: "the
 * DMA played a block the application had not committed".
 *
 * THE UNCACHED RING IS NOT AN OPTIMISATION QUESTION. port/DESIGN.md 2.3 works
 * it out: 1536 bytes at 48 kHz through write-combining stores is cheap, and
 * the alternative -- a cached ring with a clean-by-MVA after each block -- is
 * "a class of bug that only appears under load". The descriptors have no such
 * choice: the engine reads them without snooping, so they must be uncached.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "mtp_audio.h"
#include "mtp_log.h"
#include "mtp_time.h"
#include "t113_soc.h"
#include "t113_board.h"
#include "t113.h"

extern char __dma_start[], __dma_end[];

#define MAX_BLOCKS 8u

static mtp_audio_config g_cfg;
static int16_t         *g_ring;
static t113_dma_lli    *g_lli;
static uint32_t         g_block_samples;
static uint32_t         g_block_bytes;

/* Monotonic, never wrapped, each written by exactly one context. */
static volatile uint32_t g_committed;    /* render loop only */
static volatile uint32_t g_played;       /* DMA ISR only     */
static volatile uint32_t g_underruns;    /* DMA ISR only     */
static volatile uint32_t g_irqs;         /* DMA ISR only     */

static int g_open;
static int g_started;

/* The DMA block-completion interrupt. This is the place emu/src/emu_audio.c's
 * comment points at: "where the I2S DMA engine's block-completion interrupt
 * goes on the board". */
static void audio_dma_isr(void)
{
    g_irqs++;
    if (g_committed == g_played) {
        /* The engine has just replayed a block we never refilled. */
        g_underruns++;
        return;
    }
    __asm__ volatile("dmb" ::: "memory");
    g_played++;
}

mtp_status mtp_audio_open(const mtp_audio_config *cfg)
{
    uint32_t total, i;
    uint32_t fifo = 0;
    uint32_t cfgword;

    if (g_open) return MTP_ERR_STATE;
    if (!cfg || cfg->channels != 2u || cfg->frames_per_block == 0u ||
        cfg->block_count == 0u || cfg->block_count > MAX_BLOCKS ||
        cfg->sample_rate == 0u)
        return MTP_ERR_INVAL;

    g_cfg = *cfg;
    g_block_samples = (uint32_t)cfg->frames_per_block * cfg->channels;
    g_block_bytes   = g_block_samples * 2u;
    total = g_block_bytes * cfg->block_count;

    if (t113_i2s_open(cfg->sample_rate, cfg->channels,
                      T113_I2S_SAMPLE_BITS, T113_I2S_SLOT_BITS) != 0)
        return MTP_ERR_IO;

    /* 64-byte alignment for the ring and 32 for the descriptors: one
     * Cortex-A7 cache line each. They are in non-cacheable memory so nothing
     * depends on it, but an aligned burst is what the DMA's 8-beat transfers
     * want, and it keeps the two objects from sharing a line if this is ever
     * built with a cached ring instead. */
    g_ring = (int16_t *)t113_dma_alloc(total, 64u);
    g_lli  = (t113_dma_lli *)t113_dma_alloc(
                 sizeof(t113_dma_lli) * cfg->block_count, 32u);
    if (!g_ring || !g_lli) {
        MTP_LOGE("audio: %u B ring + %u B descriptors do not fit the %u B "
                 "uncached window",
                 (unsigned)total,
                 (unsigned)(sizeof(t113_dma_lli) * cfg->block_count),
                 (unsigned)(__dma_end - __dma_start));
        return MTP_ERR_NOMEM;
    }
    memset(g_ring, 0, total);

    fifo = t113_i2s_fifo_addr();

    /* The transfer configuration word, the same shape Linux builds in
     * set_config() + set_drq() + set_mode() for a MEM_TO_DEV cyclic transfer:
     *   source      DRAM, linear mode, DRQ 1 (DRQ_SDRAM), 4-byte width,
     *               8-beat burst   -- set_config()'s defaults for MEM_TO_DEV
     *   destination the I2S TX FIFO, IO mode (address does not advance),
     *               DRQ 4 (i2s1), 2-byte width (the DAI's
     *               playback_dma_data.addr_width for a 16-bit sample),
     *               8-beat burst (sun4i-i2s.c:1586, maxburst = 8)
     * A 2-byte destination width is the one to get right: at 4 bytes the
     * engine would write two samples per FIFO access and the channels would
     * swap on every other frame. */
    cfgword = DMA_CFG_SRC_DRQ(DMA_DRQ_SDRAM)   | DMA_CFG_SRC_MODE(DMA_LINEAR_MODE) |
              DMA_CFG_SRC_WIDTH(DMA_WIDTH_4B)  | DMA_CFG_SRC_BURST(DMA_BURST_8)    |
              DMA_CFG_DST_DRQ(T113_I2S_DRQ)    | DMA_CFG_DST_MODE(DMA_IO_MODE)     |
              DMA_CFG_DST_WIDTH(DMA_WIDTH_2B)  | DMA_CFG_DST_BURST(DMA_BURST_8);

    for (i = 0; i < cfg->block_count; i++) {
        g_lli[i].cfg  = cfgword;
        g_lli[i].src  = (uint32_t)(uintptr_t)(g_ring + i * g_block_samples);
        g_lli[i].dst  = fifo;
        g_lli[i].len  = g_block_bytes;
        g_lli[i].para = DMA_NORMAL_WAIT;
        g_lli[i].next = (uint32_t)(uintptr_t)
                        &g_lli[(i + 1u) % cfg->block_count];   /* circular   */
        g_lli[i].pad[0] = 0u;
        g_lli[i].pad[1] = 0u;
    }

    t113_dmac_set_handler(T113_AUDIO_DMA_CHAN, audio_dma_isr);

    g_committed = 0u;
    g_played    = 0u;
    g_underruns = 0u;
    g_irqs      = 0u;
    g_open      = 1;
    g_started   = 0;

    /* The DMA does NOT start here. mtp_audio.h promises open() is safe to
     * call early -- "until the first block is committed the ring holds
     * silence" -- and an engine started on an empty ring plays a block of
     * silence and reports an underrun for it, which is an artefact of the
     * start-up order rather than a rendering failure. emu/src/emu_audio.c
     * makes the same choice for the same reason. */
    MTP_LOGI("audio: %u Hz, %u frames/block, ring %u (%u B), block period "
             "%u us", cfg->sample_rate, cfg->frames_per_block,
             cfg->block_count, (unsigned)total,
             (unsigned)((uint64_t)cfg->frames_per_block * 1000000ull
                        / cfg->sample_rate));
    MTP_LOGI("audio: ring at %p, descriptors at %p, both Normal "
             "Non-cacheable", (void *)g_ring, (void *)g_lli);
    return MTP_OK;
}

void mtp_audio_close(void)
{
    if (!g_open) return;
    t113_i2s_stop_tx();
    t113_dmac_stop(T113_AUDIO_DMA_CHAN);
    g_started = 0;
    g_open = 0;
}

const mtp_audio_config *mtp_audio_get_config(void) { return &g_cfg; }

int16_t *mtp_audio_acquire(void)
{
    if (!g_open) return NULL;
    if (g_committed - g_played >= g_cfg.block_count) return NULL;
    return g_ring + (g_committed % g_cfg.block_count) * g_block_samples;
}

void mtp_audio_commit(void)
{
    if (!g_open) return;

    /* Publish the payload before the index. On a single core against an ISR
     * this is belt and braces; against a DMA engine it is not, because the
     * engine's reads are not ordered against our stores by anything except
     * this barrier. port/DESIGN.md 3.3 says to write it as if the consumer
     * were another core; here the consumer is worse than another core. */
    __asm__ volatile("dsb" ::: "memory");
    g_committed++;

    if (!g_started) {
        g_started = 1;
        t113_dmac_start(T113_AUDIO_DMA_CHAN, g_lli, DMAC_IRQ_PKG);
        t113_i2s_start_tx();
        MTP_LOGI("audio: DMA started on channel %u, %u descriptors, circular",
                 (unsigned)T113_AUDIO_DMA_CHAN, g_cfg.block_count);
    }
}

unsigned mtp_audio_queued(void)
{
    return (unsigned)(g_committed - g_played);
}

mtp_status mtp_audio_wait(uint32_t timeout_us)
{
    uint32_t t0 = mtp_time_us();

    if (g_committed - g_played < g_cfg.block_count) return MTP_OK;
    if (timeout_us == 0u) return MTP_ERR_AGAIN;

    for (;;) {
        /* The superloop form from port/DESIGN.md 6.4. Under an RTOS this
         * becomes rt_sem_take() with the DMA ISR giving the semaphore, and
         * nothing else in this file changes. */
        t113_wfi();
        if (g_committed - g_played < g_cfg.block_count) return MTP_OK;
        if (mtp_time_us() - t0 >= timeout_us) return MTP_ERR_AGAIN;
    }
}

uint32_t mtp_audio_underruns(void) { return g_underruns; }

/* Not part of mtp_audio.h: the board's own evidence. main() prints these and
 * bring-up step 6 reads them. */
uint32_t t113_audio_dma_irqs(void)   { return g_irqs; }
uint32_t t113_audio_played(void)     { return g_played; }
uint32_t t113_audio_cur_src(void)    { return t113_dmac_cur_src(T113_AUDIO_DMA_CHAN); }
