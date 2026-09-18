/* t113_time.c - mtp_time.h on the ARM architected generic timer.
 *
 * This is the one driver in the whole port that transfers from emu/ with no
 * change at all, and port/include/mtp_time.h says why it was chosen for
 * exactly that reason: "the ARM architectural generic timer (CNTVCT), which
 * is 64-bit, always runs, and does not stop when a core WFIs -- unlike a
 * peripheral timer, which is also a perfectly good source but one more thing
 * to configure."
 *
 * The T113 has a perfectly good peripheral timer at 0x02050000
 * (sunxi-d1s-t113.dtsi:295-299) and this port does not touch it.
 *
 * NO INTERRUPT IS USED. emu/ drives its fake audio sink from the generic
 * timer's PL1 physical timer interrupt; here the audio deadline comes from
 * the DMA engine, so the timer is a counter and nothing else. That removes
 * the one thing about the generic timer that would have depended on our
 * security state: the secure and non-secure physical timers are different
 * interrupts (PPI 13 -> INTID 29 and PPI 14 -> INTID 30,
 * sun8i-t113s.dtsi:50-53), so a payload that used the timer interrupt would
 * have to know which world it is in -- the question boot/BRINGUP.md 4.4 says
 * is open. Reading CNTPCT works either way.
 *
 * CNTFRQ is read, not assumed. boot/BRINGUP.md 4.3 gives 24 MHz [V], and
 * U-Boot programs CNTFRQ from CONFIG_COUNTER_FREQUENCY -- but a register that
 * says zero because nobody wrote it is a real failure mode on a board brought
 * up over FEL with no bootloader, and dividing by it would be a hang at the
 * first log line.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include "mtp_time.h"
#include "mtp_log.h"
#include "t113_soc.h"
#include "t113.h"

static uint64_t g_freq;
static uint64_t g_base;

static inline uint64_t cntpct(void)
{
    uint32_t lo, hi;
    /* ISB before the read: without it the counter read can be reordered
     * ahead of preceding instructions, which turns a delay loop into an
     * approximation. ARM ARM D7.2.2 on ordering of System counter reads. */
    __asm__ volatile("isb\n\tmrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint32_t cntfrq(void)
{
    uint32_t f;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(f));
    return f;
}

uint32_t t113_timer_frequency(void) { return (uint32_t)g_freq; }

static uint64_t ticks_to_us(uint64_t t)
{
    /* Split so the multiply cannot overflow: t * 1000000 alone wraps after
     * about five minutes at 24 MHz. */
    return (t / g_freq) * 1000000ull + ((t % g_freq) * 1000000ull) / g_freq;
}

mtp_status mtp_time_init(void)
{
    g_freq = cntfrq();
    if (g_freq == 0u) {
        /* No source but boot/BRINGUP.md 4.3's 24 MHz, so say loudly that we
         * are using it rather than silently being right. */
        g_freq = T113_HOSC_HZ;
    }
    g_base = cntpct();
    return MTP_OK;
}

uint64_t mtp_time_us64(void) { return ticks_to_us(cntpct() - g_base); }
uint32_t mtp_time_us(void)   { return (uint32_t)mtp_time_us64(); }

void mtp_time_delay_us(uint32_t us)
{
    uint64_t target;
    if (g_freq == 0u) {
        /* Called before mtp_time_init(): src/ccu.c's gate sequence runs
         * before the timebase exists on purpose, because the console UART
         * needs a clock before it can complain about anything. A short
         * instruction-count spin is honest here in a way that a fabricated
         * frequency would not be; at 1.2 GHz this is tens of microseconds. */
        volatile uint32_t i;
        for (i = 0; i < us * 200u + 200u; i++) { }
        return;
    }
    target = cntpct() + (g_freq * (uint64_t)us) / 1000000ull;
    while (cntpct() < target) { }
}

void t113_time_report(void)
{
    MTP_LOGI("time: CNTFRQ = %u Hz%s", (unsigned)g_freq,
             cntfrq() == 0u ? " (register was 0; assumed, see src/t113_time.c)"
                            : " (read from CNTFRQ)");
}
