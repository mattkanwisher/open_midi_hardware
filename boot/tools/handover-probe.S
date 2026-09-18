/* handover-probe.S -- report the CPU state a U-Boot command hands a payload.
 *
 * This is boot/BRINGUP.md section 4.4's "10-line mrs/mrc dump", written so it
 * can be run TODAY, without T113 silicon.  It is built for an Allwinner H3
 * (qemu-system-arm -M orangepi-pc) because that is the only Allwinner part
 * QEMU models that U-Boot also boots -- but what it measures is
 * arch/arm/lib/bootm.c, arch/arm/cpu/armv7/cpu.c and
 * arch/arm/cpu/armv7/nonsec_virt.S, which are architecture code shared by
 * every ARMv7 sunxi target including MACH_SUN8I_R528.  The SoC-specific part
 * of this file is four lines: the UART base and its register offsets.
 *
 * Link/entry 0x40200000 (DRAM base + 2 MB), the same address emu/link.ld uses.
 * Build and run with tools/handover-probe.sh.
 *
 * SPDX-License-Identifier: 0BSD
 */

	.arch armv7-a
	.text
	.global _start
_start:
	/* Registers first: bootm's ABI is kernel_entry(0, machid, dtb),
	   arch/arm/lib/bootm.c:357. */
	mov	r8, r0
	mov	r9, r1
	mov	r10, r2

	adr	r11, 1f			/* where we are really executing */

	ldr	r4, =0x01C28000		/* H3 UART0 */

	adr	r0, s_cpsr
	bl	puts
	mrs	r0, cpsr
	bl	puthex
	bl	putnl

	adr	r0, s_sctlr
	bl	puts
	mrc	p15, 0, r0, c1, c0, 0
	bl	puthex
	bl	putnl

	adr	r0, s_r0
	bl	puts
	mov	r0, r8
	bl	puthex
	bl	putnl

	adr	r0, s_r1
	bl	puts
	mov	r0, r9
	bl	puthex
	bl	putnl

	adr	r0, s_r2
	bl	puts
	mov	r0, r10
	bl	puthex
	bl	putnl

	adr	r0, s_pc
	bl	puts
	mov	r0, r11
	bl	puthex
	bl	putnl

	adr	r0, s_done
	bl	puts
1:
2:	b	2b

/* r4 = uart base.  LSR is register 5, and sunxi UARTs are reg-shift 2. */
putc:
3:	ldr	r1, [r4, #0x14]
	tst	r1, #0x20
	beq	3b
	str	r0, [r4, #0x00]
	bx	lr

puts:
	push	{r5, lr}
	mov	r5, r0
4:	ldrb	r0, [r5], #1
	cmp	r0, #0
	beq	5f
	bl	putc
	b	4b
5:	pop	{r5, lr}
	bx	lr

puthex:
	push	{r5, r6, r7, lr}
	mov	r5, r0
	mov	r6, #8
6:	mov	r0, r5, lsr #28
	cmp	r0, #9
	addle	r0, r0, #'0'
	addgt	r0, r0, #('a' - 10)
	bl	putc
	mov	r5, r5, lsl #4
	subs	r6, r6, #1
	bne	6b
	pop	{r5, r6, r7, lr}
	bx	lr

putnl:
	push	{lr}
	mov	r0, #13
	bl	putc
	mov	r0, #10
	bl	putc
	pop	{lr}
	bx	lr

	.align 2
s_cpsr:  .asciz "PROBE cpsr  = 0x"
	.align 2
s_sctlr: .asciz "PROBE sctlr = 0x"
	.align 2
s_r0:    .asciz "PROBE r0    = 0x"
	.align 2
s_r1:    .asciz "PROBE r1    = 0x"
	.align 2
s_r2:    .asciz "PROBE r2    = 0x"
	.align 2
s_pc:    .asciz "PROBE pc    = 0x"
	.align 2
s_done:  .asciz "PROBE end\r\n"
	.align 2
