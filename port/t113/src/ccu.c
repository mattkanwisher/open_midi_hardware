/* ccu.c - the clocks this port needs, and the one place where a number had to
 * be inferred rather than read.
 *
 * WHAT WE DO NOT DO. PLL_CPUX, PLL_DDR0, PSI/AHB, APB0 and APB1 are left
 * exactly as the bootloader set them. boot/BRINGUP.md 4.1 has us entered by
 * U-Boot `bootm`, and U-Boot has already run clock_init() and the DRAM
 * training that depends on it; re-programming the bus clocks under our own
 * feet on a live DRAM controller is a class of bug with no upside. We read
 * them (to derive the UART divisor) and we log them.
 *
 * WHAT WE DO. Four things, all of them peripherals U-Boot leaves off:
 *   - ungate and de-reset DMAC, I2S1, UART0, UART2, MMC0
 *   - program PLL_AUDIO0 for the 48 kHz family
 *   - set the I2S1 module clock from it
 *   - set the MMC0 module clock from the 24 MHz oscillator
 *
 * ================== THE AUDIO PLL: THE ONE INFERENCE =====================
 *
 * port/DESIGN.md 2.1 requires exactly 48000 Hz out of the DAC, because
 * AnalogOutputMode_ACCURATE makes mt32emu produce exactly 48000 with no
 * resampler. 48 kHz with 32-bit slots needs BCLK = 3.072 MHz, which needs an
 * audio clock in the 24.576 MHz family.
 *
 * (A second disagreement, about which bits of this register enable the PLL,
 * is written out in include/t113_soc.h next to PLL_AUDIO0_ENABLE. It is a
 * different question from the one below and has a different failure mode.)
 *
 * Mainline Linux CANNOT produce that family on this SoC. The reasons, in
 * order, each checked:
 *
 *  1. The I2S module clock's only parents are pll-audio0, pll-audio0-4x,
 *     pll-audio1-div2 and pll-audio1-div5 (ccu-sun20i-d1.c:535-540). There is
 *     no path from PLL_PERIPH0.
 *  2. PLL_AUDIO1 is integer-N off the 24 MHz DCXO (ccu-sun20i-d1.c:205-217,
 *     no .sdm member). 24.576 / 24 = 1.024 = 128/125, so an exact multiple of
 *     24.576 MHz needs N to be a multiple of 125; the smallest is 3072 MHz,
 *     above the driver's own .max_rate of 3000 MHz. So PLL_AUDIO1 cannot do
 *     it at all. (Its header comment says "usually 614.4 MHz", which the same
 *     arithmetic says is not reachable either -- 614.4/24 = 25.6.)
 *  3. PLL_AUDIO0 is fractional-N, but mainline drives its sigma-delta
 *     modulator from a *lookup table*, and the D1 table has exactly one
 *     entry: { .rate = 90316800, .pattern = 0xc001288d, .m = 6, .n = 22 }
 *     (ccu-sun20i-d1.c:171-173). 90.3168 MHz is the 44.1 kHz family.
 *
 * So the number we need -- the SDM pattern for the 48 kHz family -- is not in
 * any T113 or D1 source. Here is how it was derived instead, and it is an
 * inference, tagged as one.
 *
 * Read the table entries across every sunxi CCU that has one:
 *
 *   SoC    rate        m   n   pattern      24e6*(n+f)/m = rate  =>  f
 *   D1     90316800    6   22  0xc001288d   24*22.5792/6         0.5792
 *   A100   180633600   3   22  0xc001288d   24*22.5792/3         0.5792
 *   A100   45158400   18   33  0xc001bcd3   24*33.8688/18        0.8688
 *   A100   49152000   20   40  0xc001eb85   24*40.96/20          0.96
 *   A100   196608000   5   40  0xc001eb85   24*40.96/5           0.96
 *   H616   90316800    3   22  0xc001288d   (with a fixed /2)    0.5792
 *   H616   98304000    5   40  0xc001eb85   (with a fixed /2)    0.96
 *
 * The pattern depends only on the fractional part of N, and never on M: the
 * same 0xc001288d appears with m=6, m=3 and m=3, and the same 0xc001eb85
 * appears with m=20, m=5 and m=5. That is four independent (SoC, m) pairs
 * agreeing, which is why this is an inference worth acting on rather than a
 * guess.
 *
 * Therefore, for pll-audio0-4x = 98.304 MHz on the D1/T113 register layout:
 *     N = 40, fraction 0.96  ->  pattern 0xc001eb85
 *     M = 10                 ->  24 MHz * 40.96 / 10 = 98.304 MHz
 *     pll-audio0 = 98.304 / 4 = 24.576 MHz = 512 x 48000
 * M = 10 is even, which ccu-sun20i-d1.c:168 says is required for a 50% duty
 * cycle. N = 40 clears the driver's stated minimum of 12.
 *
 * Register field values are effective-minus-one: both _SUNXI_CCU_MULT_MIN and
 * _SUNXI_CCU_DIV default .offset = 1 (ccu_mult.h:26-27, ccu_div.h:63-64), so
 * we write N-1 = 39 and M-1 = 9.
 *
 * WHAT WOULD SETTLE IT: the T113/D1 user manual, CCU chapter, the
 * "PLL_AUDIO0 Control Register" and "PLL_AUDIO0 Pattern Register" tables --
 * specifically the encoding of the pattern register's wave-step and
 * wave-bottom fields. Failing that, one board and one scope on BCLK: at 48
 * kHz with 32-bit slots it must read 3.0720 MHz, and if the inference is
 * wrong the most likely wrong answers are 3.0720 x 0.96/0.5792 = 5.0912 MHz
 * (fraction ignored, N=40 integer) or a PLL that never reports lock.
 *
 * THE FALLBACK IF IT IS WRONG. t113_ccu_audio_pll_init() returns 0 rather
 * than lying, and the caller refuses to open the audio sink. port/T113.md 6
 * step 5 says what to do next; the short version is that 44.1 kHz is exactly
 * reachable with the mainline pattern, but port/DESIGN.md 2.1 rules out the
 * resampler that 44.1 kHz would need, so the real fallback is 32 kHz
 * (AnalogOutputMode_COARSE) -- which is in the *same* 24.576 MHz family and
 * so does not help. Getting this family is not optional.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include "t113_soc.h"
#include "t113.h"
#include "t113_unverified.h"
#include "mtp_log.h"
#include "mtp_time.h"

static volatile uint32_t *ccu(uint32_t off)
{ return (volatile uint32_t *)(uintptr_t)(T113_CCU_BASE + off); }

void t113_ccu_gate_and_reset(uint32_t bgr_reg, unsigned gate_bit,
                             unsigned reset_bit)
{
    uint32_t v;
    /* Order matters and is the sunxi convention everywhere: assert reset,
     * enable the bus gate, then de-assert reset. A peripheral clocked while
     * held in reset is fine; one released from reset with no clock is not. */
    v = *ccu(bgr_reg);
    v &= ~(1u << reset_bit);
    *ccu(bgr_reg) = v;
    v |= (1u << gate_bit);
    *ccu(bgr_reg) = v;
    mtp_time_delay_us(10u);
    v |= (1u << reset_bit);
    *ccu(bgr_reg) = v;
    mtp_time_delay_us(10u);
}

/* ------------------------------------------------------ reading the tree - */

uint32_t t113_ccu_pll_periph0_hz(void)
{
    uint32_t r = *ccu(CCU_PLL_PERIPH0_CTRL);
    uint32_t n = ((r >> PLL_PERIPH0_N_SHIFT) & PLL_PERIPH0_N_MASK) + 1u;
    uint32_t m = ((r >> PLL_PERIPH0_M_SHIFT) & PLL_PERIPH0_M_MASK) + 1u;
    uint32_t d2 = ((r >> PLL_PERIPH0_2X_SHIFT) & PLL_PERIPH0_2X_MASK) + 1u;
    /* pll-periph0-4x = 24 MHz * N / M; pll-periph0-2x = 4x / d2;
     * pll-periph0 (1x) = 2x / 2  (a CLK_FIXED_FACTOR, ccu-sun20i-d1.c:90-92) */
    uint64_t four_x = (uint64_t)T113_HOSC_HZ * n / m;
    return (uint32_t)(four_x / d2 / 2u);
}

uint32_t t113_ccu_apb1_hz(void)
{
    uint32_t r = *ccu(CCU_APB1_CFG);
    uint32_t m = (r & CCU_APB_M_MASK) + 1u;
    uint32_t p = 1u << ((r >> 8) & CCU_APB_P_MASK);
    uint32_t mux = (r >> 24) & CCU_APB_MUX_MASK;
    uint32_t parent;

    switch (mux) {
    case 0: parent = T113_HOSC_HZ; break;
    case 1: parent = 32768u; break;         /* losc                          */
    case 3: parent = t113_ccu_pll_periph0_hz(); break;
    default:
        /* mux 2 is psi-ahb, itself an MP clock off one of four parents. We
         * do not model it, because no board configuration we know of parents
         * APB1 there -- and guessing a UART divisor is how you get a console
         * that prints mojibake and eats an afternoon. Say so instead. */
        MTP_LOGW("ccu: APB1 mux = %u (psi-ahb), rate not modelled; "
                 "assuming %u Hz for the UART divisor", mux, T113_HOSC_HZ);
        return T113_HOSC_HZ;
    }
    return parent / m / p;
}

/* -------------------------------------------------------- the audio PLL -- */

uint32_t t113_ccu_audio_pll_init(uint32_t sample_rate)
{
    uint32_t reg, pat, n_reg, m_reg, tries;
    uint32_t pll_4x_hz;

    /* Both families the design can use live on a 24.576 MHz pll-audio0:
     * 48000 * 512 and 32000 * 768. 44100 would need 22.5792 MHz, which is the
     * one family mainline documents -- and which port/DESIGN.md 2.1 rules out
     * because reaching 44.1 kHz from mt32emu needs a resampler. */
    if (sample_rate != 48000u && sample_rate != 32000u) {
        MTP_LOGE("ccu: no audio PLL recipe for %u Hz", sample_rate);
        return 0u;
    }

    /* N = 40 (register 39), M = 10 (register 9), fraction 0.96. See the file
     * header for the derivation and for what would settle it. */
    n_reg = MTP_T113_UNVERIFIED(pll_audio0_n, 39u,
        "PLL_AUDIO0 N-1 for 98.304 MHz; inferred, see src/ccu.c header");
    m_reg = MTP_T113_UNVERIFIED(pll_audio0_m, 9u,
        "PLL_AUDIO0 M-1 for 98.304 MHz; M must be even per "
        "linux ccu-sun20i-d1.c:168");
    pat   = MTP_T113_UNVERIFIED(pll_audio0_pattern, 0xc001eb85u,
        "sigma-delta pattern for fractional N = 0.96; appears with four "
        "different M values across A100/H616 in mainline, never for D1");

    /* Program with the PLL disabled, then enable, then wait for lock. The
     * order is the sunxi convention: the pattern register is only sampled
     * while SDM_EN is set, and SDM_EN is only meaningful while the PLL runs. */
    reg = *ccu(CCU_PLL_AUDIO0_CTRL);
    /* Disable the PLL *and* its output gate before touching N, M or the SDM.
     * See t113_soc.h on why PLL_AUDIO0_ENABLE is four bits and not one. */
    reg &= ~PLL_AUDIO0_ENABLE;
    *ccu(CCU_PLL_AUDIO0_CTRL) = reg;
    mtp_time_delay_us(10u);

    *ccu(CCU_PLL_AUDIO0_PAT0) = pat | PLL_AUDIO0_PAT_EN;

    reg &= ~((uint32_t)PLL_AUDIO0_N_MASK << PLL_AUDIO0_N_SHIFT);
    reg &= ~((uint32_t)PLL_AUDIO0_M_MASK << PLL_AUDIO0_M_SHIFT);
    reg |= (n_reg & PLL_AUDIO0_N_MASK) << PLL_AUDIO0_N_SHIFT;
    reg |= (m_reg & PLL_AUDIO0_M_MASK) << PLL_AUDIO0_M_SHIFT;
    reg |= PLL_AUDIO0_SDM_EN;
    *ccu(CCU_PLL_AUDIO0_CTRL) = reg;

    /* PLL enable, LDO enable, lock-detect enable and the output gate, all at
     * once: three sources, two readings, and the union is safe (t113_soc.h). */
    reg |= PLL_AUDIO0_ENABLE;
    *ccu(CCU_PLL_AUDIO0_CTRL) = reg;

    /* No source states the lock time. 10 ms is two orders of magnitude more
     * than any PLL in this class takes and costs nothing once at boot; the
     * point of the loop is to fail loudly rather than to be quick. */
    for (tries = 0; tries < 1000u; tries++) {
        if (*ccu(CCU_PLL_AUDIO0_CTRL) & PLL_AUDIO0_LOCK) break;
        mtp_time_delay_us(10u);
    }
    if (!(*ccu(CCU_PLL_AUDIO0_CTRL) & PLL_AUDIO0_LOCK)) {
        MTP_LOGE("ccu: PLL_AUDIO0 did not lock (ctrl=0x%08x pat=0x%08x). "
                 "This is the first thing the audio-clock inference in "
                 "src/ccu.c would get wrong.",
                 (unsigned)*ccu(CCU_PLL_AUDIO0_CTRL),
                 (unsigned)*ccu(CCU_PLL_AUDIO0_PAT0));
        return 0u;
    }

    pll_4x_hz = 98304000u;
    MTP_LOGI("ccu: PLL_AUDIO0 locked, 4x = %u Hz (inferred), 1x = %u Hz",
             pll_4x_hz, pll_4x_hz / 4u);
    return pll_4x_hz / 4u;      /* pll-audio0, ccu-sun20i-d1.c:197-198 */
}

uint32_t t113_ccu_i2s1_clk_init(uint32_t want_hz)
{
    uint32_t parent = 24576000u;   /* pll-audio0, as programmed above */
    uint32_t div, m, p, reg;

    if (want_hz == 0u || parent % want_hz != 0u) {
        MTP_LOGE("ccu: i2s1 wants %u Hz from a %u Hz parent; not an integer "
                 "divide", want_hz, parent);
        return 0u;
    }
    div = parent / want_hz;

    /* M is 5 bits and P is a shift of 2 bits: divider = M * 2^P, with M
     * written as M-1. Prefer the smallest P, because a module clock divided
     * only by M has the same duty cycle as its parent. */
    for (p = 0; p < 4u; p++) {
        if ((div >> p) <= 32u && ((uint32_t)1u << p) * (div >> p) == div) break;
    }
    if (p >= 4u) {
        MTP_LOGE("ccu: i2s1 divider %u is not M*2^P with M<=32", div);
        return 0u;
    }
    m = div >> p;

    reg = 0u;
    reg |= (I2S_PARENT_PLL_AUDIO0 & CCU_MOD_MUX_MASK) << CCU_MOD_MUX_SHIFT;
    reg |= ((m - 1u) & CCU_MOD_M_MASK) << CCU_MOD_M_SHIFT;
    reg |= (p & CCU_MOD_P_MASK) << CCU_MOD_P_SHIFT;
    *ccu(CCU_I2S1_CLK) = reg;              /* configure with the gate closed */
    mtp_time_delay_us(5u);
    *ccu(CCU_I2S1_CLK) = reg | CCU_MOD_GATE;

    MTP_LOGI("ccu: i2s1 mod clk = %u Hz (parent pll-audio0 %u, M=%u, P=%u)",
             want_hz, parent, m, p);
    return want_hz;
}

void t113_ccu_mmc0_clk_init(uint32_t card_hz)
{
    uint32_t m, p, div, reg, achieved;

    /* Source the 24 MHz oscillator, always. Two reasons: it needs no PLL we
     * have to reason about, and it bounds the card clock at 24 MHz, which is
     * inside SD default speed (25 MHz) with no timing tuning at all.
     *
     * The divider is chosen so that the card clock is at most `card_hz`,
     * never more. This is deliberate under-shooting: Linux models a fixed
     * post-divider of 2 on this register (ccu-sun20i-d1.c:415-423) and
     * U-Boot's comment (drivers/mmc/sunxi_mmc.c:96-100) applies it only on
     * the PLL path, so the two sources disagree by a factor of two about what
     * comes out. Under-shooting means the disagreement costs boot time and
     * never costs correctness -- port/T113.md 5.3. */
    if (card_hz == 0u) card_hz = 400000u;
    div = (T113_HOSC_HZ + card_hz - 1u) / card_hz;
    for (p = 0; p < 4u; p++) {
        uint32_t mm = (div + (1u << p) - 1u) >> p;
        if (mm <= 16u) { m = mm; goto found; }
    }
    m = 16u; p = 3u;
found:
    reg = 0u;
    reg |= (MMC_PARENT_HOSC & 0x7u) << CCU_MOD_MUX_SHIFT;
    reg |= ((m - 1u) & CCU_MMC_M_MASK) << CCU_MOD_M_SHIFT;
    reg |= (p & CCU_MMC_P_MASK) << CCU_MOD_P_SHIFT;
    *ccu(CCU_MMC0_CLK) = reg;
    mtp_time_delay_us(5u);
    *ccu(CCU_MMC0_CLK) = reg | CCU_MOD_GATE;

    achieved = T113_HOSC_HZ / m / (1u << p) / CCU_MMC_POSTDIV;
    MTP_LOGI("ccu: mmc0 mod clk M=%u P=%u -> %u Hz by linux's model "
             "(<= the %u Hz asked for; see src/ccu.c)", m, p, achieved,
             card_hz);
}

void t113_ccu_dump(void)
{
    MTP_LOGI("ccu: PLL_CPUX   0x%08x", (unsigned)*ccu(CCU_PLL_CPUX_CTRL));
    MTP_LOGI("ccu: PLL_PERIPH0 0x%08x -> %u Hz (1x)",
             (unsigned)*ccu(CCU_PLL_PERIPH0_CTRL),
             (unsigned)t113_ccu_pll_periph0_hz());
    MTP_LOGI("ccu: PLL_AUDIO0 0x%08x pat 0x%08x",
             (unsigned)*ccu(CCU_PLL_AUDIO0_CTRL),
             (unsigned)*ccu(CCU_PLL_AUDIO0_PAT0));
    MTP_LOGI("ccu: APB0 0x%08x  APB1 0x%08x -> %u Hz",
             (unsigned)*ccu(CCU_APB0_CFG), (unsigned)*ccu(CCU_APB1_CFG),
             (unsigned)t113_ccu_apb1_hz());
    MTP_LOGI("ccu: PSI/AHB 0x%08x  CPUX_AXI 0x%08x",
             (unsigned)*ccu(CCU_PSI_AHB_CFG), (unsigned)*ccu(CCU_CPUX_AXI_CFG));
}
