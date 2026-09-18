/* gic.c - GIC-400, enough of it for three SPIs.
 *
 * The register programming here is architectural (GICv2, ARM IHI 0048B) and
 * is the same code emu/src/gic.c runs under QEMU. Everything T113-specific is
 * two addresses and three interrupt numbers, and all five come from
 * include/t113_soc.h with citations.
 *
 * WHAT IS DIFFERENT FROM emu/. Three things, and each is a real T113 fact:
 *
 *  1. The distributor is at 0x03021000 and the CPU interface at 0x03022000,
 *     not at the generic 0x08000000/0x08010000 -- and note that both sit
 *     inside the 0x03020000 block U-Boot calls SUNXI_GIC400_BASE, at the
 *     GIC-400's own standard +0x1000/+0x2000 frame offsets.
 *
 *  2. The interrupts we care about are SPIs (DMAC 82, UART0 34, UART2 36),
 *     not the PPI 30 the emulator uses, so ITARGETSR matters: an SPI that
 *     targets no CPU never fires, and that is a silent failure that looks
 *     exactly like a dead peripheral.
 *
 *  3. We may be running secure or non-secure and do not yet know which
 *     (boot/BRINGUP.md 4.4). This matters for the GIC because GICD_CTLR has
 *     two enable bits in the secure view (group 0 and group 1) and one in the
 *     non-secure view. Writing 3 sets both when secure and is harmlessly
 *     truncated when not, so we write 3 and leave every interrupt in group 0,
 *     which is the configuration that works in both worlds. The cost is that
 *     nothing here uses interrupt grouping, which we do not need.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include "t113_soc.h"
#include "t113.h"

#define GICD_CTLR       0x000
#define GICD_TYPER      0x004
#define GICD_ISENABLER  0x100
#define GICD_ICENABLER  0x180
#define GICD_ICPENDR    0x280
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR  0x800
#define GICD_ICFGR      0xC00

#define GICC_CTLR       0x000
#define GICC_PMR        0x004
#define GICC_BPR        0x008
#define GICC_IAR        0x00C
#define GICC_EOIR       0x010

/* [V-T113] SyterKit's T113 DTS declares allwinner,irq-count = <223>
 * (quoted in boot/BRINGUP.md 4.3), and the highest SPI mainline's shared
 * dtsi uses is 172/173 for the PMU (sun8i-t113s.dtsi:58-59). 256 covers
 * both with room, and GICD_TYPER is read at init and logged so the real
 * number appears in the boot log rather than in a comment. */
#define MAX_INTID       256u

static volatile uint32_t *gicd(uint32_t o)
{ return (volatile uint32_t *)(uintptr_t)(T113_GICD_BASE + o); }
static volatile uint32_t *gicc(uint32_t o)
{ return (volatile uint32_t *)(uintptr_t)(T113_GICC_BASE + o); }

static t113_irq_handler g_handlers[MAX_INTID];
static uint32_t g_irqs;
static uint32_t g_spurious;
static uint32_t g_lines;

uint32_t t113_gic_lines(void) { return g_lines; }

void t113_gic_init(void)
{
    unsigned i;
    uint32_t typer;

    *gicd(GICD_CTLR) = 0u;
    *gicc(GICC_CTLR) = 0u;

    /* GICD_TYPER[4:0] ITLinesNumber: the distributor supports
     * 32*(ITLinesNumber+1) interrupts. ARM IHI 0048B 4.3.2. */
    typer = *gicd(GICD_TYPER);
    g_lines = 32u * ((typer & 0x1Fu) + 1u);
    if (g_lines > MAX_INTID) g_lines = MAX_INTID;

    /* We may be entered from U-Boot, which has certainly left the controller
     * configured for itself. Disable and clear everything we can see. */
    for (i = 0; i < g_lines; i += 32u)
        *gicd(GICD_ICENABLER + (i / 32u) * 4u) = 0xFFFFFFFFu;
    for (i = 0; i < g_lines; i += 32u)
        *gicd(GICD_ICPENDR + (i / 32u) * 4u) = 0xFFFFFFFFu;
    for (i = 0; i < g_lines; i += 4u)
        *gicd(GICD_IPRIORITYR + i) = 0xA0A0A0A0u;
    /* SPIs target CPU0. SGI/PPI targets (0..31) are read-only and banked. */
    for (i = 32u; i < g_lines; i += 4u)
        *gicd(GICD_ITARGETSR + i) = 0x01010101u;

    for (i = 0; i < MAX_INTID; i++) g_handlers[i] = 0;

    *gicc(GICC_PMR)  = 0xF0u;   /* accept anything more urgent than 0xF0    */
    *gicc(GICC_BPR)  = 0x03u;   /* no preemption grouping                    */
    *gicc(GICC_CTLR) = 1u;
    *gicd(GICD_CTLR) = 3u;      /* see header note 3                         */
    __asm__ volatile("dsb\n\tisb" ::: "memory");
}

void t113_gic_set_handler(unsigned intid, t113_irq_handler h)
{
    if (intid < MAX_INTID) g_handlers[intid] = h;
}

void t113_gic_enable(unsigned intid, unsigned priority)
{
    if (intid >= MAX_INTID) return;
    {
        volatile uint8_t *pri =
            (volatile uint8_t *)(uintptr_t)(T113_GICD_BASE + GICD_IPRIORITYR);
        pri[intid] = (uint8_t)priority;
    }
    /* Every Allwinner peripheral interrupt in the shared dtsi is declared
     * IRQ_TYPE_LEVEL_HIGH, so leave GICD_ICFGR at its level-sensitive reset
     * state rather than writing it: ICFGR for SPIs is implementation-defined
     * in how much of it is writable, and a wrong edge/level setting turns a
     * working DMA completion into a one-shot. */
    *gicd(GICD_ISENABLER + (intid / 32u) * 4u) = 1u << (intid % 32u);
    __asm__ volatile("dsb\n\tisb" ::: "memory");
}

void t113_gic_disable(unsigned intid)
{
    if (intid >= MAX_INTID) return;
    *gicd(GICD_ICENABLER + (intid / 32u) * 4u) = 1u << (intid % 32u);
    __asm__ volatile("dsb\n\tisb" ::: "memory");
}

/* Called from the IRQ vector in start.S, in SVC mode, on the SVC stack. */
void t113_irq_dispatch(void)
{
    unsigned handled = 0;

    for (;;) {
        uint32_t iar = *gicc(GICC_IAR);
        uint32_t id  = iar & 0x3FFu;
        if (id >= 1020u) {
            /* 1023 = nothing pending. Reading it once at the end of the drain
             * loop is normal; only an entry that finds nothing is spurious. */
            if (handled == 0u) g_spurious++;
            return;
        }
        handled++;
        g_irqs++;
        if (id < MAX_INTID && g_handlers[id]) g_handlers[id]();
        *gicc(GICC_EOIR) = iar;
    }
}

uint32_t t113_irq_count(void)      { return g_irqs; }
uint32_t t113_spurious_count(void) { return g_spurious; }
