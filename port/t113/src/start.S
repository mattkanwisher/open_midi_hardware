/* start.S - ARMv7-A reset path for the T113 payload.
 *
 * ENTRY CONTRACT. U-Boot `bootm` (boot/BRINGUP.md 4.1, the
 * cleanup_before_linux() row) hands over with:
 *
 *     MMU off, D-cache off and flushed, I-cache state unspecified,
 *     IRQ and FIQ masked, ARM state. The mode may be SVC *or non-secure Hyp*
 *     -- mainline U-Boot's sunxi default is Hyp. Step 3 normalises it.
 *
 * Secure or non-secure is NOT part of that contract and is the one thing
 * boot/BRINGUP.md 4.4 says to pin down on hardware. Step 2 below records the
 * registers that answer it; main() prints them; bring-up step 2 in
 * port/T113.md reads that print. Nothing in this file depends on the answer.
 *
 * This is emu/src/start.S with three T113-specific changes, and they are the
 * interesting part of the diff:
 *   - step 1 parks CPU1 if a loader ever released it. On the T113 the BROM
 *     releases CPU0 only (boot/BRINGUP.md 4.3 [V]), so this normally never
 *     fires -- but U-Boot's PSCI can start CPU1 and a payload that assumes
 *     otherwise is a fault you only see once.
 *   - step 8b sets ACTLR.SMP. On a Cortex-A7 the data cache does not
 *     participate in coherency until SMPEN is set, and cacheable accesses
 *     "may not behave as expected" without it (Cortex-A7 MPCore TRM 4.3.28).
 *     U-Boot's sunxi SPL already sets it (MACH_SUN8I_R528 selects
 *     SPL_ARMV7_SET_CORTEX_SMPEN, arch/arm/mach-sunxi/Kconfig:487), so this
 *     is belt and braces -- and it is ignored rather than faulting at
 *     non-secure PL1.
 *   - there is no semihosting exit; a fatal exception prints and spins.
 *
 * SPDX-License-Identifier: 0BSD
 */

    .syntax unified
    .arm
    /* elr_hyp and eret in step 3 need the virtualisation extension enabled
     * for the assembler; the A7 implements it. */
    .arch_extension virt

/* ---------------------------------------------------------------- vectors */
    .section .vectors, "ax"
    .align 5
    .global _vectors
_vectors:
    b   _start
    b   vec_undef
    b   vec_svc
    b   vec_pabt
    b   vec_dabt
    b   vec_hyp
    b   vec_irq
    b   vec_fiq

/* ------------------------------------------------------------------ reset */
    .section .text.start, "ax"
    .global _start
    .type _start, %function
_start:
    /* 1. Park every core but 0. */
    mrc     p15, 0, r4, c0, c0, 5       /* MPIDR                            */
    ands    r4, r4, #0xFF               /* Aff0                             */
    beq     1f
0:  wfi
    b       0b
1:

    /* 2. Record the entry state before touching it. .data, not .bss: BSS is
     *    not cleared yet. */
    ldr     r5, =t113_entry_state
    mrs     r6, cpsr
    str     r6, [r5, #0]
    mrc     p15, 0, r6, c1, c0, 0       /* SCTLR                            */
    str     r6, [r5, #4]
    mrc     p15, 0, r6, c1, c0, 1       /* ACTLR                            */
    str     r6, [r5, #8]
    mrc     p15, 0, r6, c12, c0, 0      /* VBAR                             */
    str     r6, [r5, #12]
    mrc     p15, 0, r6, c0, c0, 0       /* MIDR: is this really an A7?      */
    str     r6, [r5, #16]
    mrc     p15, 0, r6, c0, c1, 1       /* ID_PFR1: security/virt extensions */
    str     r6, [r5, #20]
    mrc     p15, 0, r6, c14, c0, 0      /* CNTFRQ: 24 MHz if U-Boot set it  */
    str     r6, [r5, #24]
    str     r0, [r5, #28]               /* loader r0                        */
    str     r1, [r5, #32]               /* loader r1 (machine id)           */
    str     r2, [r5, #36]               /* loader r2 (DTB / ATAGS)          */
    adr     r6, _start
    str     r6, [r5, #40]               /* where we are actually running    */
    mrc     p15, 0, r6, c0, c0, 5       /* MPIDR                            */
    str     r6, [r5, #44]

    /* 3. Known CPSR: SVC, interrupts masked, alignment faults off.
     *
     *    We may arrive in Hyp mode (0x1a), not SVC. Mainline U-Boot defaults
     *    ARMV7_NONSEC=y and ARMV7_VIRT=y for sunxi -- ARMV7_BOOT_SEC_DEFAULT
     *    is `default y if ARCH_TEGRA`, so n here -- and boot_jump_linux()
     *    goes through _do_nonsec_entry and the secure monitor, which selects
     *    HYP_MODE on a virtualisation-capable core. The A7 is one.
     *    Measured on a real sunxi U-Boot under QEMU: bootm gives CPSR
     *    0x600001da, mode 0x1a. See boot/BRINGUP.md 4.4.
     *
     *    A `msr cpsr_c` cannot leave Hyp -- the ARM ARM makes a mode write
     *    that would exit Hyp ignored, so the old code here fell through with
     *    the mode unchanged, vectored through HVBAR instead of VBAR, and gave
     *    all five "per-mode" stacks the same Hyp stack. Nothing faults and
     *    nothing works, which is the worst way to be wrong.
     *
     *    U-Boot's own armv7/start.S:93-99 guards the same instruction with
     *    `teq r1, #0x1a` for this exact reason. We go further and leave Hyp
     *    properly, by ERET, so the payload is correct whether the loader
     *    hands us Hyp or SVC and no bootloader config is load-bearing. */
    cpsid   if
    mrs     r0, cpsr
    and     r1, r0, #0x1F
    cmp     r1, #0x1A                   /* Hyp?                             */
    bne     1f
    bic     r0, r0, #0x1F
    orr     r0, r0, #0xD3               /* SVC, I and F masked              */
    msr     spsr_cxsf, r0
    adr     r0, 1f
    msr     elr_hyp, r0
    eret                                /* Hyp -> SVC, lands at 1:          */
1:
    mrs     r0, cpsr
    bic     r0, r0, #0x1F
    orr     r0, r0, #0x13
    msr     cpsr_c, r0
    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #(1 << 1)           /* SCTLR.A = 0                      */
    bic     r0, r0, #(1 << 13)          /* SCTLR.V = 0: low vectors + VBAR  */
    mcr     p15, 0, r0, c1, c0, 0
    isb

    /* 4. A stack for every mode. An abort with no ABT stack is a silent
     *    lockup, which is the worst possible first bug on new silicon. */
    msr     cpsr_c, #(0x11 | 0xC0)      /* FIQ */
    ldr     sp, =__stack_fiq_top
    msr     cpsr_c, #(0x1B | 0xC0)      /* UND */
    ldr     sp, =__stack_und_top
    msr     cpsr_c, #(0x17 | 0xC0)      /* ABT */
    ldr     sp, =__stack_abt_top
    msr     cpsr_c, #(0x12 | 0xC0)      /* IRQ */
    ldr     sp, =__stack_irq_top
    msr     cpsr_c, #(0x13 | 0xC0)      /* SVC */
    ldr     sp, =__stack_svc_top

    /* 5. VBAR. */
    ldr     r0, =_vectors
    mcr     p15, 0, r0, c12, c0, 0
    isb

    /* 6. Caches arrive off with undefined contents: invalidate, do not
     *    clean. */
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0       /* ICIALLU                          */
    mcr     p15, 0, r0, c7, c5, 6       /* BPIALL                           */
    mcr     p15, 0, r0, c8, c7, 0       /* TLBIALL                          */
    dsb
    isb
    bl      v7_dcache_invalidate_all

    /* 7a. TLS: .tdata is loaded, everything after it is .tbss and must be
     *     zero before libm touches errno. */
    ldr     r0, =__tdata_end
    ldr     r1, =__tls_end
    mov     r2, #0
5:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     5b

    /* 7b. BSS. */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
2:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     2b

    /* 8. VFP/NEON, before the first C call: everything here is built
     *    -mfpu=neon-vfpv4 -mfloat-abi=hard to match bench/ and emu/
     *    (boot/BRINGUP.md 6.1), so GCC emits VFP saves in prologues. */
    mrc     p15, 0, r0, c1, c0, 2       /* CPACR                            */
    orr     r0, r0, #(0xF << 20)        /* cp10, cp11: full access          */
    bic     r0, r0, #(1 << 31)          /* ASEDIS = 0                       */
    bic     r0, r0, #(1 << 30)          /* D32DIS = 0                       */
    mcr     p15, 0, r0, c1, c0, 2
    isb
    mov     r0, #(1 << 30)              /* FPEXC.EN                         */
    vmsr    fpexc, r0
    mov     r0, #0
    vmsr    fpscr, r0

    /* 8b. ACTLR.SMP -- see the header comment. */
    mrc     p15, 0, r0, c1, c0, 1
    orr     r0, r0, #(1 << 6)
    mcr     p15, 0, r0, c1, c0, 1
    isb

    /* 9. MMU and caches. T113_MMU=0 builds the exact bootm handover state
     *    and never changes it, which is the configuration to fall back to
     *    when something is wrong and you want one variable less. */
#if defined(T113_MMU) && T113_MMU
    bl      t113_mmu_enable
#endif

    /* 10. C++ static constructors, then main(). */
    ldr     r4, =__init_array_start
    ldr     r5, =__init_array_end
3:  cmp     r4, r5
    bhs     4f
    ldr     r3, [r4], #4
    blx     r3
    b       3b
4:
    mov     r0, #0
    mov     r1, #0
    bl      main
    mov     r0, #0
    bl      t113_exit

    .size _start, . - _start

/* ------------------------------------------------- D-cache invalidate all */
/* The standard ARMv7 set/way walk, ARM ARM B2.2.4. Reset only: invalidate,
 * not clean, because the caches are off and hold whatever the loader left. */
    .global v7_dcache_invalidate_all
    .type v7_dcache_invalidate_all, %function
v7_dcache_invalidate_all:
    push    {r4-r11, lr}
    dmb
    mrc     p15, 1, r0, c0, c0, 1       /* CLIDR                            */
    ands    r3, r0, #0x07000000
    lsr     r3, r3, #23                 /* LoC * 2                          */
    beq     .Lfinished
    mov     r10, #0
.Lloop_level:
    add     r2, r10, r10, lsr #1
    lsr     r1, r0, r2
    and     r1, r1, #7
    cmp     r1, #2
    blt     .Lnext_level
    mcr     p15, 2, r10, c0, c0, 0      /* CSSELR                           */
    isb
    mrc     p15, 1, r1, c0, c0, 0       /* CCSIDR                           */
    and     r2, r1, #7
    add     r2, r2, #4
    ubfx    r4, r1, #3, #10             /* ways - 1                         */
    clz     r5, r4
    ubfx    r7, r1, #13, #15            /* sets - 1                         */
.Lloop_set:
    mov     r9, r4
.Lloop_way:
    lsl     r6, r9, r5
    orr     r11, r10, r6
    lsl     r6, r7, r2
    orr     r11, r11, r6
    mcr     p15, 0, r11, c7, c6, 2      /* DCISW                            */
    subs    r9, r9, #1
    bge     .Lloop_way
    subs    r7, r7, #1
    bge     .Lloop_set
.Lnext_level:
    add     r10, r10, #2
    cmp     r3, r10
    bgt     .Lloop_level
.Lfinished:
    mov     r10, #0
    mcr     p15, 2, r10, c0, c0, 0
    dsb
    isb
    pop     {r4-r11, pc}
    .size v7_dcache_invalidate_all, . - v7_dcache_invalidate_all

/* ----------------------------------------------------------- IRQ dispatch */
    .type vec_irq, %function
vec_irq:
    sub     lr, lr, #4
    srsdb   sp!, #0x13                  /* save LR_irq/SPSR on the SVC stack */
    cps     #0x13
    push    {r0-r3, r12, lr}
    and     r1, sp, #4                  /* 8-byte align for the AAPCS        */
    sub     sp, sp, r1
    push    {r1}
    bl      t113_irq_dispatch
    pop     {r1}
    add     sp, sp, r1
    pop     {r0-r3, r12, lr}
    rfeia   sp!
    .size vec_irq, . - vec_irq

/* -------------------------------------------------------- fatal exceptions */
.macro FATAL name, num, adj
    .type \name, %function
\name:
    sub     lr, lr, #\adj
    mov     r1, lr
    mov     r0, #\num
    cps     #0x13
    ldr     sp, =__stack_svc_top
    b       t113_fatal_exception
    .size \name, . - \name
.endm

FATAL vec_undef, 1, 4
FATAL vec_svc,   2, 0
FATAL vec_pabt,  3, 4
FATAL vec_dabt,  4, 8
FATAL vec_hyp,   5, 4
FATAL vec_fiq,   6, 4

/* ---------------------------------------------------------------- helpers */
/* AEABI requires __aeabi_read_tp to preserve r1-r3 and the flags, which is
 * why it is assembly. One thread, one statically allocated TLS block; the
 * 8-byte TCB in front of it only fixes the offset the linker bakes into every
 * TPOFF relocation. */
    .global __aeabi_read_tp
    .type __aeabi_read_tp, %function
__aeabi_read_tp:
    ldr     r0, =__tls_start
    sub     r0, r0, #8
    bx      lr
    .size __aeabi_read_tp, . - __aeabi_read_tp

    .global t113_wfi
    .type t113_wfi, %function
t113_wfi:
    dsb
    wfi
    bx      lr
    .size t113_wfi, . - t113_wfi

    .global t113_irq_enable
t113_irq_enable:
    cpsie   i
    bx      lr

    .global t113_irq_disable
t113_irq_disable:
    cpsid   i
    bx      lr

    .section .data
    .align 2
    .global t113_entry_state
t113_entry_state:
    .space 48, 0

    .section .note.GNU-stack,"",%progbits
