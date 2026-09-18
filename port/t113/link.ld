/* link.ld - the T113 payload image.
 *
 * Load and entry address 0x40200000: the T113's DRAM base (0x40000000,
 * boot/BRINGUP.md 4.2 [V]) plus the conventional 2 MB, which leaves U-Boot's
 * low memory alone and stays well clear of the top of DRAM, where U-Boot
 * relocates itself (boot/BRINGUP.md 4.2 [I]). It is the same address emu/
 * links at, deliberately: the two images are the same code above the seam and
 * wrapping either with `mkimage -a 0x40200000 -e 0x40200000` produces
 * something `bootm` will start.
 *
 * LENGTH is 64 MB, which is a statement about what we are willing to use, not
 * about what is fitted. hw/HARDWARE.md fits one x16 4 Gbit DDR3L = 512 MB and
 * docs/HISTORY.md 2 measured MT-32's working set at 1.49 MiB; the arena below
 * is sized from port/PORTING.md 7 and has room for the CM-32L pair with both
 * ROM staging buffers resident.
 *
 * The .dma region is the one that is structurally different from a hosted
 * program and the one to look at first when audio is glitchy: src/mmu.c maps
 * it Normal Non-cacheable, and the DMAC reads both the audio ring and its
 * descriptor list out of it (port/DESIGN.md 2.3).
 *
 * SPDX-License-Identifier: 0BSD
 */

ENTRY(_start)

HEAP_SIZE  = 12M;
SVC_STACK  = 64K;
IRQ_STACK  = 8K;
FIQ_STACK  = 1K;
ABT_STACK  = 4K;
UND_STACK  = 4K;

MEMORY
{
    RAM (rwx) : ORIGIN = 0x40200000, LENGTH = 64M
}

SECTIONS
{
    . = ORIGIN(RAM);

    .text : ALIGN(32)
    {
        __image_start = .;
        KEEP(*(.vectors))
        *(.text .text.*)
        *(.glue_7 .glue_7t)
        . = ALIGN(4);
    } > RAM

    .rodata : ALIGN(8)
    {
        *(.rodata .rodata.*)
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        /* Every MTP_T113_UNVERIFIED() in the build lands here, and main()
         * walks the table and prints it. See include/t113_unverified.h. */
        . = ALIGN(4);
        __mtp_assumptions_start = .;
        KEEP(*(.mtp_assumptions))
        __mtp_assumptions_end = .;
        . = ALIGN(8);
    } > RAM

    .ARM.exidx : { *(.ARM.exidx*) *(.gnu.linkonce.armexidx.*) } > RAM

    .data : ALIGN(8)
    {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(8);
        __data_end = .;
    } > RAM

    /* One thread's TLS block. glibc's libm.a declares errno as __thread, so
     * without a TLS segment the link fails with "TLS reference ... mismatches
     * non-TLS definition"; emu/FINDINGS.md records that discovery. */
    .tdata : ALIGN(8)
    {
        __tls_start = .;
        *(.tdata .tdata.*)
        . = ALIGN(8);
        __tdata_end = .;
    } > RAM

    .tbss (NOLOAD) : ALIGN(8)
    {
        *(.tbss .tbss.*)
        *(.tcommon)
    } > RAM

    .tls_reserve (NOLOAD) : ALIGN(8)
    {
        . = __tdata_end + 512;
        __tls_end = .;
    } > RAM

    .bss (NOLOAD) : ALIGN(8)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM

    .pagetable (NOLOAD) : ALIGN(16384)
    {
        __pagetable_start = .;
        . += 16K;
        __pagetable_end = .;
    } > RAM

    .stacks (NOLOAD) : ALIGN(16)
    {
        . += FIQ_STACK; __stack_fiq_top = .;
        . += UND_STACK; __stack_und_top = .;
        . += ABT_STACK; __stack_abt_top = .;
        . += IRQ_STACK; __stack_irq_top = .;
        . += SVC_STACK; __stack_svc_top = .;
        __stack_limit = . - SVC_STACK;
    } > RAM

    .heap (NOLOAD) : ALIGN(4096)
    {
        __heap_start = .;
        . += HEAP_SIZE;
        __heap_end = .;
    } > RAM

    /* One 1 MB section, so mmu.c retypes exactly one first-level descriptor.
     * Holds the audio ring (1536 B at the design's 3 x 128 frames) and the
     * DMA descriptor list. */
    .dma (NOLOAD) : ALIGN(1M)
    {
        __dma_start = .;
        . += 1M;
        __dma_end = .;
    } > RAM

    __image_end = .;

    ASSERT(__tls_end - __tls_start <= 1024,
           "TLS block outgrew its reservation in link.ld")

    /DISCARD/ : { *(.comment) *(.note.*) *(.ARM.attributes) }
}
