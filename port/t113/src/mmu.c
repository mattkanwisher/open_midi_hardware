/* mmu.c - flat 1:1 mapping, caches on, built from 1 MB sections.
 *
 * Same shape as emu/src/mmu.c, different map, and the difference is the
 * point: on `-M virt` the device window is "whatever QEMU put below DRAM",
 * while on the T113 it is a real and specific set of peripherals.
 *
 *   0x00000000..0x3FFFFFFF   Device, XN     every peripheral this port
 *                                           touches: PIO 0x02000000, CCU
 *                                           0x02001000, UART 0x02500000,
 *                                           I2S1 0x02033000, DMAC 0x03002000,
 *                                           GIC 0x03020000, SMHC 0x04020000
 *   0x40000000..0x7FFFFFFF   Normal WB/WA, shareable, executable   DRAM
 *   __dma_start..__dma_end   Normal Non-cacheable, shareable, XN
 *   everything else          faults
 *
 * THE NON-CACHEABLE WINDOW IS THE WHOLE REASON THIS FILE MATTERS ON HARDWARE.
 * port/DESIGN.md 2.3 requires the audio ring to be non-cacheable normal
 * memory because the CPU writes it and the I2S DMA engine reads it, and
 * emu/src/dma.c's header says the same thing about the descriptor list: "a
 * descriptor list left in write-back memory is the classic silent failure".
 * QEMU cannot catch that mistake -- TCG models no caches at all -- so on the
 * board this one descriptor is what you check first when audio is glitchy.
 *
 * The DRAM mapping is 1 GB wide regardless of how much is fitted. That is
 * safe because nothing walks it: the linker script places everything inside
 * the first 64 MB. Mapping more than exists would only matter if something
 * speculatively fetched into it, and the region above __image_end is never
 * pointed at.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include "t113.h"

extern uint32_t __pagetable_start[];
extern char __dma_start[], __dma_end[];

/* ARMv7 short-descriptor section, ARM ARM B3.5.1:
 *   [1:0]=0b10 section, [2]=B, [3]=C, [4]=XN, [8:5]=domain,
 *   [11:10]=AP[1:0], [14:12]=TEX, [15]=AP[2], [16]=S, [17]=nG, [31:20]=base */
#define SECT            (2u <<  0)
#define B_BIT           (1u <<  2)
#define C_BIT           (1u <<  3)
#define XN_BIT          (1u <<  4)
#define AP_RW           (3u << 10)
#define TEX(x)          (((x) & 7u) << 12)
#define S_BIT           (1u << 16)

#define MAP_NORMAL_WB   (SECT | AP_RW | TEX(1) | C_BIT | B_BIT | S_BIT)
#define MAP_NORMAL_NC   (SECT | AP_RW | TEX(1)                 | S_BIT | XN_BIT)
#define MAP_DEVICE      (SECT | AP_RW | TEX(0)         | B_BIT | S_BIT | XN_BIT)

#define DRAM_BASE       0x40000000u
#define DRAM_TOP        0x80000000u

static inline void dsb_isb(void)
{
    __asm__ volatile("dsb\n\tisb" ::: "memory");
}

/* Called from start.S with the MMU off, BSS cleared and the FPU enabled.
 * Must not touch anything not yet initialised: no printf, no heap. */
void t113_mmu_enable(void)
{
    volatile uint32_t *tt = __pagetable_start;
    uint32_t i;

    for (i = 0; i < 4096u; i++) {
        uint32_t base = i << 20;
        if (base >= DRAM_BASE && base < DRAM_TOP)
            tt[i] = base | MAP_NORMAL_WB;
        else if (base < DRAM_BASE)
            tt[i] = base | MAP_DEVICE;
        else
            tt[i] = 0u;                 /* fault: catch a wild pointer      */
    }

    {
        uint32_t lo = (uint32_t)(uintptr_t)__dma_start >> 20;
        uint32_t hi = ((uint32_t)(uintptr_t)__dma_end + 0xFFFFFu) >> 20;
        for (i = lo; i < hi && i < 4096u; i++)
            tt[i] = (i << 20) | MAP_NORMAL_NC;
    }

    __asm__ volatile(
        "mov  r0, #0\n"
        "mcr  p15, 0, r0, c2, c0, 2\n"          /* TTBCR = 0: TTBR0 only    */
        "ldr  r1, =0x55555555\n"
        "mcr  p15, 0, r1, c3, c0, 0\n"          /* DACR: all client         */
        "orr  r0, %0, #0x6a\n"                  /* IRGN=WBWA, S, RGN=WBWA   */
        "mcr  p15, 0, r0, c2, c0, 0\n"          /* TTBR0                    */
        "mcr  p15, 0, r0, c8, c7, 0\n"          /* TLBIALL                  */
        "dsb\n\tisb\n"
        "mrc  p15, 0, r0, c1, c0, 0\n"
        "orr  r0, r0, #(1 << 0)\n"              /* M   MMU                  */
        "orr  r0, r0, #(1 << 2)\n"              /* C   D-cache              */
        "orr  r0, r0, #(1 << 11)\n"             /* Z   branch prediction    */
        "orr  r0, r0, #(1 << 12)\n"             /* I   I-cache              */
        "mcr  p15, 0, r0, c1, c0, 0\n"
        "dsb\n\tisb\n"
        :
        : "r" (tt)
        : "r0", "r1", "memory");
}

/* Not used by the audio path -- the ring is in the non-cacheable window --
 * but this is what a cached ring would need, so the alternative stays one
 * line away. 32-byte lines: Cortex-A7 L1 D-cache line length is 64 bytes per
 * the TRM, and 32 is a safe under-estimate that costs one extra MCR. */
void t113_dcache_clean_range(const void *addr, uint32_t len)
{
    uint32_t p = (uint32_t)(uintptr_t)addr & ~31u;
    uint32_t end = (uint32_t)(uintptr_t)addr + len;
    for (; p < end; p += 32u)
        __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(p) : "memory");
    dsb_isb();
}
