/* i2s.c - I2S1 as clock master, Philips I2S, stereo, 16-bit in 32-bit slots.
 *
 * This is the driver boot/BRINGUP.md 5.4 says nobody has: "the two drivers
 * that stand between us and a working module -- I2S with DMA and SD with FAT
 * -- are supplied by neither RTOS". The register knowledge comes from
 * mainline's sun4i-i2s.c, read rather than copied: this file is 0BSD and that
 * one is GPL-2.0, and boot/BRINGUP.md 5.4 already sets the rule -- read the
 * driver "for understanding rather than copying it, which sidesteps the
 * licence question". What transfers is the register map, which is a fact
 * about the silicon.
 *
 * THE VARIANT MATTERS. "allwinner,sun20i-d1-i2s" binds
 * sun50i_r329_i2s_quirks (sun4i-i2s.c:1481-1500), and three things in that
 * quirk set are NOT the sun4i defaults a casual reading would give you:
 *
 *   - the TX FIFO is at 0x20, not 0x0c  (.reg_offset_txdata)
 *   - MCLK_EN is bit 8 of CLK_DIV, not bit 7 (.field_clkdiv_mclk_en =
 *     REG_FIELD(CLK_DIV, 8, 8))
 *   - WSS and SR are 3-bit fields at [2:0] and [6:4] with the sun8i encoding
 *     (8b=1 .. 32b=7), not the 2-bit sun4i ones
 *
 * Each of those, got wrong, gives silence with every register looking
 * plausible.
 *
 * THE CLOCK CHAIN, end to end, for the design's 48 kHz:
 *
 *   DCXO 24 MHz
 *     -> PLL_AUDIO0 (fractional N = 40.96, M = 10)   = 98.304 MHz  (4x)
 *     -> pll-audio0 = 4x / 4                         = 24.576 MHz
 *     -> i2s1 module clock, M = 1, P = 0             = 24.576 MHz
 *     -> BCLK = mod / 8                              =  3.072 MHz
 *     -> LRCK = BCLK / (32 slots x 2 channels)       = 48.000 kHz
 *
 * The /8 comes from sun4i_i2s_get_bclk_div()'s arithmetic, which this file
 * reproduces: div = parent / rate / slot_bits / channels
 *             = 24576000 / 48000 / 32 / 2 = 8, encoded as 5 in the sun8i
 * divider table (sun4i-i2s.c:259-274). port/DESIGN.md 2.1's "48 kHz x 32 bits
 * x 2 channels = 3.072 MHz BCLK" is the same number from the other end.
 *
 * MCLK IS NOT DRIVEN. The PCM5102A runs from its internal PLL on BCLK alone,
 * which port/DESIGN.md 2.1 gives as the reason for choosing it. If a board
 * ever fits a DAC that needs MCLK, T113_I2S_MCLK_OUT turns it on -- and hits
 * a deliberate link error unless T113_I2S_MCLK_HZ is also defined, because
 * the required MCLK rate is a property of a DAC we have not chosen.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include "t113_soc.h"
#include "t113_board.h"
#include "t113.h"
#include "t113_unverified.h"
#include "mtp_log.h"
#include "mtp_time.h"

static volatile uint32_t *i2s(uint32_t off)
{ return (volatile uint32_t *)(uintptr_t)(T113_I2S_BASE + off); }

static void rmw(uint32_t off, uint32_t mask, uint32_t val)
{
    uint32_t v = *i2s(off);
    v = (v & ~mask) | (val & mask);
    *i2s(off) = v;
}

static uint32_t g_bclk_hz;
static uint32_t g_mod_hz;

uint32_t t113_i2s_fifo_addr(void) { return T113_I2S_BASE + I2S_FIFO_TX; }
uint32_t t113_i2s_bclk_hz(void)   { return g_bclk_hz; }

/* sun8i divider table, sun4i-i2s.c:259-274. Returns the encoded value or 0,
 * which is not a legal encoding (the table starts at 1). */
static uint32_t encode_div(uint32_t div)
{
    static const uint16_t d[] = { 1, 2, 4, 6, 8, 12, 16, 24,
                                  32, 48, 64, 96, 128, 176, 192 };
    unsigned i;
    for (i = 0; i < sizeof d / sizeof d[0]; i++)
        if (d[i] == div) return (uint32_t)i + 1u;
    return 0u;
}

int t113_i2s_open(uint32_t sample_rate, unsigned channels,
                  unsigned sample_bits, unsigned slot_bits)
{
    uint32_t pll_hz, mod_hz, bclk_div, bclk_code;
    uint32_t lrck_period;

    if (channels != 2u) {
        /* mtp_audio.h makes channels a field precisely so that a mono build
         * is a compile-time-visible error rather than a half-speed bug. */
        MTP_LOGE("i2s: only stereo is wired (asked for %u channels)", channels);
        return -1;
    }

    /* 1. Clocks. */
    pll_hz = t113_ccu_audio_pll_init(sample_rate);
    if (pll_hz == 0u) return -1;

    /* The module clock has to be an integer multiple of
     * rate * slot_bits * channels, and that multiple has to be in the
     * divider table. 24.576 MHz with 48 kHz / 32 / 2 gives 8, which is. */
    mod_hz = pll_hz;
    if (t113_ccu_i2s1_clk_init(mod_hz) == 0u) return -1;
    g_mod_hz = mod_hz;

    bclk_div = mod_hz / (sample_rate * slot_bits * channels);
    if (bclk_div == 0u ||
        mod_hz != bclk_div * sample_rate * slot_bits * channels) {
        MTP_LOGE("i2s: %u Hz mod clock does not divide to %u Hz x %u x %u",
                 mod_hz, sample_rate, slot_bits, channels);
        return -1;
    }
    bclk_code = encode_div(bclk_div);
    if (bclk_code == 0u) {
        MTP_LOGE("i2s: BCLK divider %u is not in the sun8i divider table",
                 bclk_div);
        return -1;
    }
    g_bclk_hz = mod_hz / bclk_div;

    /* 2. Bus gate and reset, then pins. Pins last, so that nothing is driven
     * by a block that is still in reset -- an I2S output stuck at a DC level
     * into a DAC is at best a thump. */
    t113_ccu_gate_and_reset(CCU_I2S_BGR, T113_I2S_BGR_GATE_BIT,
                            T113_I2S_BGR_RESET_BIT);

    /* 3. Global enable, and the first output line. sun4i-i2s.c's
     * runtime_resume does exactly these two, in this order, before anything
     * else is programmed. */
    rmw(I2S_CTRL, I2S_CTRL_GL_EN, I2S_CTRL_GL_EN);
    rmw(I2S_CTRL, I2S_CTRL_SDO_EN_MASK, I2S_CTRL_SDO_EN(0));

    /* 4. Format: Philips I2S, we are the clock provider.
     * sun50i_h6_i2s_set_soc_fmt(), SND_SOC_DAIFMT_I2S branch:
     *   LRCK polarity START_LOW, mode LEFT, channel offset 1
     * plus the BP_FP branch: BCLK_OUT | LRCK_OUT. */
    rmw(I2S_CTRL, I2S_CTRL_MODE_MASK, I2S_CTRL_MODE_LEFT);
    rmw(I2S_TX_CHAN_SEL(0), I2S_TX_CHAN_SEL_OFFSET(3u),
        I2S_TX_CHAN_SEL_OFFSET(1u));
    rmw(I2S_RX_CHAN_SEL, I2S_TX_CHAN_SEL_OFFSET(3u),
        I2S_TX_CHAN_SEL_OFFSET(1u));
    rmw(I2S_FMT0, I2S_FMT0_LRCK_POL_MASK | I2S_FMT0_BCLK_POL_MASK,
        I2S_FMT0_LRCK_POL_LOW | I2S_FMT0_BCLK_POL_NORMAL);
    rmw(I2S_CTRL, I2S_CTRL_BCLK_OUT | I2S_CTRL_LRCK_OUT,
        I2S_CTRL_BCLK_OUT | I2S_CTRL_LRCK_OUT);
    /* Sign extension 0 = pad the unused LSBs with zeros. With 16-bit samples
     * in 32-bit slots the low 16 bits are ours to define, and zeros are what
     * a DAC expects; the alternative (sign extension at the MSB) is for
     * right-justified formats. */
    rmw(I2S_FMT1, I2S_FMT1_SEXT_MASK, I2S_FMT1_SEXT(0));

    /* 5. Channel configuration. For I2S (as opposed to DSP/PCM) the LRCK
     * period is the slot width, not slot_width * slots -- sun50i_h6_i2s_
     * set_chan_cfg(), the SND_SOC_DAIFMT_I2S branch. So a 32-bit slot gives
     * LRCK high for 32 BCLKs and low for 32, i.e. 64 BCLKs per frame. */
    *i2s(I2S_TX_CHAN_MAP0(0)) = 0xFEDCBA98u;
    *i2s(I2S_TX_CHAN_MAP1(0)) = 0x76543210u;
    rmw(I2S_TX_CHAN_SEL(0), 0xFu << 16, I2S_TX_CHAN_SEL_N(channels));
    rmw(I2S_RX_CHAN_SEL,    0xFu << 16, I2S_TX_CHAN_SEL_N(channels));
    rmw(I2S_CHAN_CFG, 0xFFu,
        I2S_CHAN_CFG_TX_SLOTS(channels) | I2S_CHAN_CFG_RX_SLOTS(channels));
    lrck_period = slot_bits;
    rmw(I2S_FMT0, I2S_FMT0_LRCK_PERIOD_MASK,
        I2S_FMT0_LRCK_PERIOD(lrck_period));
    rmw(I2S_TX_CHAN_SEL(0), 0xFFFFu, I2S_TX_CHAN_EN(channels));

    /* 6. FIFO mode and sample/slot widths.
     * FIFO_CTRL TX_MODE(1) and RX_MODE(1) is what sun4i_i2s_hw_params writes
     * unconditionally for every variant and every width. */
    rmw(I2S_FIFO_CTRL, I2S_FIFO_CTRL_TX_MODE_MASK | I2S_FIFO_CTRL_RX_MODE_MASK,
        I2S_FIFO_CTRL_TX_MODE(1) | I2S_FIFO_CTRL_RX_MODE(1));
    rmw(I2S_FMT0, (uint32_t)I2S_FMT0_WSS_MASK << I2S_FMT0_WSS_SHIFT,
        I2S_WIDTH_CODE(slot_bits) << I2S_FMT0_WSS_SHIFT);
    rmw(I2S_FMT0, (uint32_t)I2S_FMT0_SR_MASK << I2S_FMT0_SR_SHIFT,
        I2S_WIDTH_CODE(sample_bits) << I2S_FMT0_SR_SHIFT);

    /* 7. Dividers. A full write, not a read-modify-write: that is what
     * sun4i_i2s_set_clk_rate does, and it matters because the BCLK field's
     * documented width differs between the sun4i map (3 bits, [6:4]) and what
     * the sun8i divider table needs (values up to 15, so 4 bits). Our value
     * is 5, which fits either reading -- but writing the whole register means
     * the ambiguity cannot bite. */
#ifdef T113_I2S_MCLK_OUT
    {
        uint32_t mclk_hz = MTP_T113_UNKNOWN(i2s1_mclk_rate_hz);
        uint32_t mclk_div = encode_div(mod_hz / mclk_hz);
        *i2s(I2S_CLK_DIV) = I2S_CLK_DIV_BCLK(bclk_code) |
                            I2S_CLK_DIV_MCLK(mclk_div) | I2S_CLK_DIV_MCLK_EN;
    }
#else
    *i2s(I2S_CLK_DIV) = I2S_CLK_DIV_BCLK(bclk_code);
#endif

    /* 8. Pins, now that the block is configured and not driving nonsense. */
    t113_pio_set_function(T113_I2S_BCLK_BANK, T113_I2S_BCLK_PIN,
                          T113_I2S_PIN_FN);
    t113_pio_set_function(T113_I2S_LRCK_BANK, T113_I2S_LRCK_PIN,
                          T113_I2S_PIN_FN);
    t113_pio_set_function(T113_I2S_DOUT_BANK, T113_I2S_DOUT_PIN,
                          T113_I2S_PIN_FN);

    MTP_LOGI("i2s: %u Hz, %u ch, %u-bit in %u-bit slots, BCLK %u Hz "
             "(mod %u / %u), LRCK period %u",
             sample_rate, channels, sample_bits, slot_bits, g_bclk_hz,
             mod_hz, bclk_div, lrck_period);
    return 0;
}

void t113_i2s_start_tx(void)
{
    /* Flush the TX FIFO and zero the TX counter before enabling, so the first
     * frame out of the DAC is the first frame we wrote and not whatever the
     * FIFO held. sun4i_i2s_start_playback() does the same four writes. */
    rmw(I2S_FIFO_CTRL, I2S_FIFO_CTRL_FLUSH_TX, I2S_FIFO_CTRL_FLUSH_TX);
    *i2s(I2S_TXCNT) = 0u;
    rmw(I2S_CTRL, I2S_CTRL_TX_EN, I2S_CTRL_TX_EN);
    rmw(I2S_DMA_INT_CTRL, I2S_DMA_INT_TX_DRQ_EN, I2S_DMA_INT_TX_DRQ_EN);
    __asm__ volatile("dsb" ::: "memory");
}

void t113_i2s_stop_tx(void)
{
    rmw(I2S_CTRL, I2S_CTRL_TX_EN, 0u);
    rmw(I2S_DMA_INT_CTRL, I2S_DMA_INT_TX_DRQ_EN, 0u);
    __asm__ volatile("dsb" ::: "memory");
}

/* TXCNT counts samples the block has shifted out. It is the cheapest proof
 * that BCLK is really running, and it is bring-up step 5: read it twice a
 * second apart and the difference must be sample_rate * channels. */
void t113_i2s_dump(void)
{
    MTP_LOGI("i2s: CTRL %08x FMT0 %08x FMT1 %08x FIFO_CTRL %08x",
             (unsigned)*i2s(I2S_CTRL), (unsigned)*i2s(I2S_FMT0),
             (unsigned)*i2s(I2S_FMT1), (unsigned)*i2s(I2S_FIFO_CTRL));
    MTP_LOGI("i2s: CLK_DIV %08x CHAN_CFG %08x TX_CHAN_SEL %08x",
             (unsigned)*i2s(I2S_CLK_DIV), (unsigned)*i2s(I2S_CHAN_CFG),
             (unsigned)*i2s(I2S_TX_CHAN_SEL(0)));
    MTP_LOGI("i2s: FIFO_STA %08x TXCNT %u DMA_INT %08x",
             (unsigned)*i2s(I2S_FIFO_STA), (unsigned)*i2s(I2S_TXCNT),
             (unsigned)*i2s(I2S_DMA_INT_CTRL));
}
