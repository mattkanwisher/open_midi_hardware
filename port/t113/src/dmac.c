/* dmac.c - the DMA controller, one channel, circular descriptor list.
 *
 * port/DESIGN.md 2.3 asks for exactly this: "Build three descriptors, one per
 * block, in a circular list -- `next` of the last pointing at the first --
 * and start it once. From then on the DMA engine runs for ever with no CPU
 * involvement except the per-descriptor completion interrupt."
 *
 * Everything here is [V-D1] from linux drivers/dma/sun6i-dma.c, which the
 * shared dtsi binds through compatible "allwinner,sun20i-d1-dma"
 * (sunxi-d1s-t113.dtsi:501) and which maps that compatible onto
 * sun50i_a100_dma_cfg (sun6i-dma.c:1297). The register offsets and the
 * descriptor layout are in include/t113_soc.h with line numbers.
 *
 * TWO THINGS THAT ARE INFERENCES AND NOT READS.
 *
 * 1. WHICH COMPLETION INTERRUPT. The per-channel IRQ enable has three bits:
 *    HALF (BIT 0), PKG (BIT 1), QUEUE (BIT 2). Linux picks PKG for a cyclic
 *    transfer and QUEUE otherwise (sun6i-dma.c:461):
 *        vchan->irq_type = vchan->cyclic ? DMA_IRQ_PKG : DMA_IRQ_QUEUE;
 *    We are cyclic, so we use PKG, and we rely on it firing once per
 *    descriptor. That is what makes Linux's cyclic DMA period callback work,
 *    so it is a strong inference -- but the user manual's definition of
 *    "package end" versus "queue end" is what would state it. If PKG turns
 *    out to fire at a different granularity, the symptom is unmistakable:
 *    mtp_audio_queued() walks monotonically in the wrong direction and the
 *    underrun counter runs away. Bring-up step 6 in port/T113.md checks it.
 *
 * 2. THE LAST DESCRIPTOR'S `next`. A terminating list writes 0xfffff800
 *    (LLI_LAST_ITEM, sun6i-dma.c:105); a cyclic one writes the first
 *    descriptor's physical address (sun6i-dma.c, prep_dma_cyclic:
 *    "prev->p_lli_next = txd->p_lli;  /" "* cyclic list *" "/"). We do the
 *    latter and never write LLI_LAST at all, so the engine has no way to
 *    stop. That is the intent -- an I2S output that stops is a click -- and
 *    it is why t113_dmac_stop() has to write the channel enable register
 *    rather than just letting the list run out.
 *
 * ADDRESSES ARE PHYSICAL. The MMU maps 1:1 (src/mmu.c) so a pointer is its
 * own physical address, but the descriptors and the ring must still live in
 * the non-cacheable window, because the engine does not snoop. The `para`
 * field's high-address bits (SRC_HIGH_ADDR/DST_HIGH_ADDR, sun6i-dma.c:99-100)
 * are left zero: the a100 config sets has_high_addr, but everything this port
 * touches is under 4 GB by construction -- link.ld's RAM region starts at
 * 0x40200000 and is 64 MB long.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include "t113_soc.h"
#include "t113.h"
#include "mtp_log.h"

static volatile uint32_t *dm(uint32_t off)
{ return (volatile uint32_t *)(uintptr_t)(T113_DMAC_BASE + off); }

static t113_irq_handler g_ch_handler[T113_DMAC_CHANNELS];

static void dmac_isr(void)
{
    unsigned reg, ch;

    for (reg = 0; reg < T113_DMAC_CHANNELS / DMAC_IRQ_CHAN_NR; reg++) {
        uint32_t status = *dm(DMAC_IRQ_STAT(reg));
        if (status == 0u) continue;
        /* Write-one-to-clear, the whole word at once, before calling any
         * handler. sun6i-dma.c:~1100 does the same and the ordering is what
         * stops a completion that arrives during the handler from being
         * cleared without being seen. */
        *dm(DMAC_IRQ_STAT(reg)) = status;
        __asm__ volatile("dmb" ::: "memory");

        for (ch = 0; ch < DMAC_IRQ_CHAN_NR && status != 0u; ch++) {
            unsigned id = reg * DMAC_IRQ_CHAN_NR + ch;
            if ((status & (DMAC_IRQ_HALF | DMAC_IRQ_PKG | DMAC_IRQ_QUEUE)) &&
                id < T113_DMAC_CHANNELS && g_ch_handler[id])
                g_ch_handler[id]();
            status >>= DMAC_IRQ_CHAN_WIDTH;
        }
    }
}

void t113_dmac_init(void)
{
    unsigned i;

    for (i = 0; i < T113_DMAC_CHANNELS; i++) g_ch_handler[i] = 0;

    /* Bus gate + reset, and the MBUS gate: this controller needs both a bus
     * clock and an MBUS clock (sunxi-d1s-t113.dtsi:503-504, clock-names
     * "bus", "mbus"; sun6i-dma.c sun50i_a100_dma_cfg .has_mbus_clk = true).
     * Forgetting the MBUS gate gives a controller that accepts register
     * writes and never moves a byte. */
    t113_ccu_gate_and_reset(CCU_DMA_BGR, 0u, 16u);
    *((volatile uint32_t *)(uintptr_t)(T113_CCU_BASE + CCU_MBUS_GATE)) |= 1u << 0;

    /* "DMA MCLK interface circuit auto gating bit ... should be set up when
     * initializing the DMA controller" -- sun6i-dma.c:125-133 quoting the
     * user manual, implemented as writel(0x4, base + 0x28) in
     * sun6i_enable_clock_autogate_h3 (:~250), which is the callback the a100
     * config selects. */
    *dm(DMAC_GATE) = DMAC_GATE_ENABLE;

    /* Mask everything until a channel asks for it. */
    for (i = 0; i < T113_DMAC_CHANNELS / DMAC_IRQ_CHAN_NR; i++) {
        *dm(DMAC_IRQ_EN(i)) = 0u;
        *dm(DMAC_IRQ_STAT(i)) = 0xFFFFFFFFu;
    }

    t113_gic_set_handler(T113_IRQ_DMAC, dmac_isr);
    /* Priority 0x40: more urgent than the MIDI UART's 0x60. On a GIC a lower
     * number is higher priority. The audio block-completion interrupt is the
     * only hard deadline in the system; a MIDI byte can wait 320 us. */
    t113_gic_enable(T113_IRQ_DMAC, 0x40u);
}

void t113_dmac_set_handler(unsigned ch, t113_irq_handler h)
{
    if (ch < T113_DMAC_CHANNELS) g_ch_handler[ch] = h;
}

void t113_dmac_start(unsigned ch, const t113_dma_lli *first, uint32_t irq_mask)
{
    unsigned reg = ch / DMAC_IRQ_CHAN_NR;
    unsigned off = ch % DMAC_IRQ_CHAN_NR;
    uint32_t v;

    if (ch >= T113_DMAC_CHANNELS) return;

    v = *dm(DMAC_IRQ_EN(reg));
    v &= ~((DMAC_IRQ_HALF | DMAC_IRQ_PKG | DMAC_IRQ_QUEUE)
           << (off * DMAC_IRQ_CHAN_WIDTH));
    v |= (irq_mask & 7u) << (off * DMAC_IRQ_CHAN_WIDTH);
    *dm(DMAC_IRQ_EN(reg)) = v;

    /* The descriptor list is in non-cacheable memory, so there is nothing to
     * clean -- but the stores must be visible before the engine is told to
     * fetch, and on a weakly ordered core that is a barrier, not a hope. */
    __asm__ volatile("dsb" ::: "memory");

    *dm(DMAC_CHAN_BASE(ch) + DMAC_CH_LLI_ADDR) = (uint32_t)(uintptr_t)first;
    *dm(DMAC_CHAN_BASE(ch) + DMAC_CH_ENABLE)   = 1u;   /* START, sun6i:59   */
}

void t113_dmac_stop(unsigned ch)
{
    unsigned reg = ch / DMAC_IRQ_CHAN_NR;
    unsigned off = ch % DMAC_IRQ_CHAN_NR;
    uint32_t v;

    if (ch >= T113_DMAC_CHANNELS) return;
    *dm(DMAC_CHAN_BASE(ch) + DMAC_CH_ENABLE) = 0u;     /* STOP, sun6i:60    */
    v = *dm(DMAC_IRQ_EN(reg));
    v &= ~((DMAC_IRQ_HALF | DMAC_IRQ_PKG | DMAC_IRQ_QUEUE)
           << (off * DMAC_IRQ_CHAN_WIDTH));
    *dm(DMAC_IRQ_EN(reg)) = v;
    __asm__ volatile("dsb" ::: "memory");
}

/* The address the engine is currently reading from. Not used by the audio
 * path, which counts interrupts instead -- but it is the register that
 * answers "is the DMA actually running?" on a board with no sound, and that
 * is bring-up step 6. */
uint32_t t113_dmac_cur_src(unsigned ch)
{
    if (ch >= T113_DMAC_CHANNELS) return 0u;
    return *dm(DMAC_CHAN_BASE(ch) + DMAC_CH_CUR_SRC);
}
