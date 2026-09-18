/* t113_soc.h - every Allwinner T113 address and bit field this port touches,
 * with the primary source for each one.
 *
 * THE RULE THIS FILE EXISTS TO ENFORCE. No number below was written from
 * memory or from a blog post. Each carries a citation of the form
 *
 *     [V-T113] read from a source that names the T113 itself
 *     [V-D1]   read from a source that names the D1/sun20i/R528 and that
 *              mainline Linux *shares* with the T113 (see the note below)
 *     [I]      inferred, with the reasoning given and what would settle it
 *
 * WHY [V-D1] IS ALMOST AS GOOD AS [V-T113], AND WHERE IT IS NOT.
 * mainline Linux's ARM device tree for the T113-s,
 * arch/arm/boot/dts/allwinner/sun8i-t113s.dtsi, is nine lines of CPU and GIC
 * nodes on top of two #includes:
 *
 *     #include <riscv/allwinner/sunxi-d1s-t113.dtsi>     (line 8)
 *     #include <riscv/allwinner/sunxi-d1-t113.dtsi>      (line 9)
 *
 * so every peripheral node this port uses -- ccu, dma, i2s1, mmc0, uartN,
 * pio -- is literally the same node the T113 and the D1 share, in a file
 * whose *name* says "d1s-t113". That is a much stronger claim than "same die
 * family": it is mainline asserting the addresses are identical. What it does
 * NOT assert is that a given signal is bonded out on the T113-i's LFBGA
 * package, which is a datasheet question and is tracked in port/T113.md 7.
 *
 * Sources, with the exact commits read (2026-09-18):
 *   [L] torvalds/linux  5dd1818b15d98d4a20806cd00b1b40320b06004f
 *   [U] u-boot/u-boot   211de43d0f954a00a490220c1aac9db298287c40
 * Paths below are relative to those trees; they are cloned, gitignored, under
 * port/vendor/.
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef T113_SOC_H
#define T113_SOC_H

#include <stdint.h>

/* ===================================================================== */
/* Memory map                                                            */
/* ===================================================================== */

/* [V-T113] DRAM base. boot/BRINGUP.md 4.2 cites CFG_SYS_SDRAM_BASE and
 * "memory@40000000" in every T113 DTS. This port is linked at +2 MB. */
#define T113_DRAM_BASE          0x40000000u

/* [V-D1] [U] arch/arm/include/asm/arch-sunxi/cpu_sunxi_ncat2.h:10,11,18-22,
 * 30-32 -- the header CONFIG_SUNXI_GEN_NCAT2 selects, and
 * arch/arm/mach-sunxi/Kconfig:479-490 shows MACH_SUN8I_R528 (the T113's
 * U-Boot SoC symbol) selects SUNXI_GEN_NCAT2. */
#define T113_CCU_BASE           0x02001000u   /* [U] cpu_sunxi_ncat2.h:10   */
#define T113_TIMER_BASE         0x02050000u   /* [U] cpu_sunxi_ncat2.h:11   */
#define T113_SRAMC_BASE         0x03000000u   /* [U] cpu_sunxi_ncat2.h:18   */
#define T113_SID_BASE           0x03006000u   /* [U] cpu_sunxi_ncat2.h:20   */
#define T113_GIC400_BASE        0x03020000u   /* [U] cpu_sunxi_ncat2.h:22   */
#define T113_MMC0_BASE          0x04020000u   /* [U] cpu_sunxi_ncat2.h:30   */
#define T113_MMC1_BASE          0x04021000u   /* [U] cpu_sunxi_ncat2.h:31   */
#define T113_MMC2_BASE          0x04022000u   /* [U] cpu_sunxi_ncat2.h:32   */

/* [V-D1] [U] arch/arm/include/asm/arch-sunxi/serial.h:20-30. Confirmed
 * independently by [L] arch/riscv/boot/dts/allwinner/sunxi-d1s-t113.dtsi:
 * serial@2500000 (323), @2500400 (336), @2500800 (349), @2500c00 (362),
 * @2501000 (375), @2501400 (388). Two sources, same numbers. */
#define T113_UART_BASE(n)       (0x02500000u + (uint32_t)(n) * 0x400u)

/* [V-D1] [L] sunxi-d1s-t113.dtsi:37-39 "pio: pinctrl@2000000, reg = <0x2000000
 * 0x800>". [U] include/sunxi_gpio.h:22-24 agrees (SUNXI_GEN_NCAT2 branch). */
#define T113_PIO_BASE           0x02000000u

/* [V-D1] [L] sunxi-d1s-t113.dtsi:500-502 "dma: dma-controller@3002000,
 * reg = <0x3002000 0x1000>, dma-channels = <16>, dma-requests = <48>". */
#define T113_DMAC_BASE          0x03002000u
#define T113_DMAC_CHANNELS      16u
#define T113_DMAC_MAX_DRQ       48u

/* [V-D1] [L] sunxi-d1s-t113.dtsi:265-268 "i2s1: i2s@2033000" and 280-283
 * "i2s2: i2s@2034000". Both are compatible "allwinner,sun20i-d1-i2s",
 * "allwinner,sun50i-r329-i2s".
 *
 * I2S0 (0x02032000) is NOT in mainline: on the D1 it is wired to the
 * on-chip audio codec, which mainline does not support. The only source for
 * 0x02032000 is Allwinner's own HAL (SUNXI_DAUDIO_BASE in
 * sunxi-hal/.../platforms/daudio-sun8iw20.h, quoted in boot/BRINGUP.md 5.1),
 * a file with no licence grant. We use I2S1, which is mainline-documented and
 * whose pins are mainline-documented, and we do not depend on I2S0 at all. */
#define T113_I2S1_BASE          0x02033000u
#define T113_I2S2_BASE          0x02034000u

/* ===================================================================== */
/* Interrupts                                                            */
/* ===================================================================== */

/* [V-T113] [L] arch/arm/boot/dts/allwinner/sun8i-t113s.dtsi:4
 *     #define SOC_PERIPHERAL_IRQ(nr) GIC_SPI nr
 * so a peripheral's DT number n is SPI n, and INTID = 32 + n. That #define
 * lives in the *T113* file; the RISC-V side of the same silicon maps the same
 * numbers onto the PLIC instead. This is the one place where being on the
 * Cortex-A7 rather than the C906 changes the arithmetic. */
#define T113_SPI_INTID(n)       (32u + (uint32_t)(n))

#define T113_IRQ_UART(n)        T113_SPI_INTID(2u + (uint32_t)(n))
                                /* [V-D1] [L] dtsi:328 (uart0 -> SPI 2),
                                 * 341, 354, 367, 380, 393: consecutive.     */
#define T113_IRQ_DMAC           T113_SPI_INTID(50u)   /* [L] dtsi:505        */
#define T113_IRQ_I2S1           T113_SPI_INTID(27u)   /* [L] dtsi:269        */
#define T113_IRQ_I2S2           T113_SPI_INTID(28u)   /* [L] dtsi:284        */
#define T113_IRQ_MMC0           T113_SPI_INTID(40u)   /* [L] dtsi:556        */

/* [V-T113] [L] sun8i-t113s.dtsi:37-46, the gic node:
 *     compatible = "arm,gic-400";
 *     reg = <0x03021000 0x1000>,   distributor
 *           <0x03022000 0x2000>,   CPU interface
 *           <0x03024000 0x2000>,   virtual interface control
 *           <0x03026000 0x2000>;   virtual CPU interface
 * Note this is the *only* T113 fact in this header that comes from a file
 * that is not shared with the D1, because the D1 has no GIC. */
#define T113_GICD_BASE          0x03021000u
#define T113_GICC_BASE          0x03022000u

/* [V-T113] [L] sun8i-t113s.dtsi:48-54, the armv7-timer node, PPIs in DT
 * order: 13 (secure physical), 14 (non-secure physical), 11 (virtual),
 * 10 (hypervisor). INTID = 16 + PPI. */
#define T113_IRQ_TIMER_SEC_PHYS 29u
#define T113_IRQ_TIMER_PHYS     30u
#define T113_IRQ_TIMER_VIRT     27u

/* ===================================================================== */
/* CCU - clock control unit, offsets from T113_CCU_BASE                   */
/* ===================================================================== */
/* All [V-D1] from [L] drivers/clk/sunxi-ng/ccu-sun20i-d1.c, which is the
 * driver the shared dtsi binds (compatible "allwinner,sun20i-d1-ccu",
 * sunxi-d1s-t113.dtsi:203). Line numbers are that file's. */

#define CCU_PLL_CPUX_CTRL       0x000u  /* :36                              */
#define CCU_PLL_DDR0_CTRL       0x010u  /* :50                              */
#define CCU_PLL_PERIPH0_CTRL    0x020u  /* :65-85                           */
#define CCU_PLL_AUDIO0_CTRL     0x078u  /* :170-190                         */
#define CCU_PLL_AUDIO1_CTRL     0x080u  /* :205-227                         */
#define CCU_PLL_AUDIO0_PAT0     0x178u  /* :181, the sdm "tuning_reg"       */

#define CCU_CPUX_AXI_CFG        0x500u  /* :247-249                         */
#define CCU_PSI_AHB_CFG         0x510u  /* :257                             */
#define CCU_APB0_CFG            0x520u  /* :269                             */
#define CCU_APB1_CFG            0x524u  /* :275                             */

#define CCU_MBUS_GATE           0x804u  /* :391 (mbus-dma = BIT(0))         */
#define CCU_DMA_BGR             0x70cu  /* gate :346-347 BIT(0);
                                         * reset :1248 0x70c BIT(16)        */
#define CCU_MMC0_CLK            0x830u  /* :416                             */
#define CCU_MMC1_CLK            0x834u  /* :425                             */
#define CCU_MMC_BGR             0x84cu  /* gate :449 BIT(0..2);
                                         * reset :1257-1259 BIT(16..18)     */
#define CCU_UART_BGR            0x90cu  /* gate :456-463 BIT(0..5);
                                         * reset :1260-1265 BIT(16..21)     */
#define CCU_I2S0_CLK            0xa10u  /* :541                             */
#define CCU_I2S1_CLK            0xa14u  /* :548                             */
#define CCU_I2S2_CLK            0xa18u  /* :555                             */
#define CCU_I2S_BGR             0xa20u  /* gate :577 BIT(1) for i2s1;
                                         * reset :1278-1280 BIT(16..18)     */

/* PLL_AUDIO0 control register fields. [V-D1] ccu-sun20i-d1.c:173-190:
 *   .enable = BIT(27), .lock = BIT(28),
 *   .n = _SUNXI_CCU_MULT_MIN(8, 8, 12)   -> N at [15:8], minimum 12
 *   .m = _SUNXI_CCU_DIV(16, 6)           -> M at [21:16]
 *   .sdm = _SUNXI_CCU_SDM(table, BIT(24), 0x178, BIT(31))
 * "The M factor must be an even number to produce a 50% duty cycle output"
 * -- ccu-sun20i-d1.c:168. */
#define PLL_AUDIO0_N_SHIFT      8u
#define PLL_AUDIO0_N_MASK       0xFFu
#define PLL_AUDIO0_M_SHIFT      16u
#define PLL_AUDIO0_M_MASK       0x3Fu
#define PLL_AUDIO0_SDM_EN       (1u << 24)
#define PLL_AUDIO0_ENABLE       (1u << 27)
#define PLL_AUDIO0_LOCK         (1u << 28)
#define PLL_AUDIO0_PAT_EN       (1u << 31)

/* "MP with mux and gate" module clocks: i2s0/1/2 at 0xa10/0xa14/0xa18 with
 * M at [4:0], P at [9:8], mux at [26:24], gate BIT(31).
 * [V-D1] ccu-sun20i-d1.c:541-561. Parent list, :535-540:
 *   0 pll-audio0        (= pll_audio0_4x / 4, :197-198)
 *   1 pll-audio0-4x
 *   2 pll-audio1-div2
 *   3 pll-audio1-div5                                                     */
#define CCU_MOD_M_SHIFT         0u
#define CCU_MOD_M_MASK          0x1Fu
#define CCU_MOD_P_SHIFT         8u
#define CCU_MOD_P_MASK          0x3u
#define CCU_MOD_MUX_SHIFT       24u
#define CCU_MOD_MUX_MASK        0x7u
#define CCU_MOD_GATE            (1u << 31)

#define I2S_PARENT_PLL_AUDIO0       0u
#define I2S_PARENT_PLL_AUDIO0_4X    1u
#define I2S_PARENT_PLL_AUDIO1_DIV2  2u
#define I2S_PARENT_PLL_AUDIO1_DIV5  3u

/* APB0/APB1: M at [4:0], P at [9:8], mux at [25:24], parents
 * {hosc, losc, psi-ahb, pll-periph0}. [V-D1] ccu-sun20i-d1.c:263-279. */
#define CCU_APB_M_MASK          0x1Fu
#define CCU_APB_P_MASK          0x3u
#define CCU_APB_MUX_MASK        0x3u

/* PLL_PERIPH0: 4x output N at [15:8] (min 12), input divider M at [1:1];
 * 2x output divider at [18:16]; 800M divider at [22:20]; enable BIT(27),
 * lock BIT(28). pll-periph0 (1x) is a fixed /2 of pll-periph0-2x.
 * [V-D1] ccu-sun20i-d1.c:65-92. */
#define PLL_PERIPH0_N_SHIFT     8u
#define PLL_PERIPH0_N_MASK      0xFFu
#define PLL_PERIPH0_M_SHIFT     1u
#define PLL_PERIPH0_M_MASK      0x1u
#define PLL_PERIPH0_2X_SHIFT    16u
#define PLL_PERIPH0_2X_MASK     0x7u

/* MMC module clock, 0x830: M at [3:0] (divider = M+1), P at [9:8]
 * (divider = 2^P), mux at [26:24], gate BIT(31), plus a *fixed post-divider
 * of 2*. [V-D1] ccu-sun20i-d1.c:415-423
 *   SUNXI_CCU_MP_DATA_WITH_MUX_GATE_POSTDIV(mmc0_clk, ..., 0x830,
 *       0, 4, 8, 2, 24, 3, BIT(31), 2, 0)
 * Parents, :409-414: {hosc, pll-periph0, pll-periph0-2x, pll-audio1-div2}.
 *
 * SOURCES DISAGREE HERE, see port/T113.md 5.3. [U] drivers/mmc/sunxi_mmc.c:
 * 96-100 says "On the D1/R528/T113 mux source 1 refers to PLL_PERIPH0(1x)...
 * However we still have the hidden divider of 2x, so compensate for that
 * here" -- and then compensates only on the PLL path, not on the 24 MHz
 * oscillator path (:76-79). Linux applies the /2 unconditionally. We take
 * Linux's model, because the two differ only in whether the card ends up at
 * the requested clock or at half of it, and half is the safe direction for a
 * read-only boot-time loader. */
#define CCU_MMC_M_MASK          0xFu
#define CCU_MMC_P_MASK          0x3u
#define CCU_MMC_POSTDIV         2u
#define MMC_PARENT_HOSC         0u
#define MMC_PARENT_PLL_PERIPH0  1u

#define T113_HOSC_HZ            24000000u   /* [V-T113] boot/BRINGUP.md 4.3:
                                             * "Architected timer runs at
                                             * 24 MHz", and the CCU's "hosc"
                                             * is the same DCXO.             */

/* ===================================================================== */
/* PIO - pin controller                                                  */
/* ===================================================================== */
/* [V-D1] [U] drivers/gpio/sunxi_gpio.c:36-60 with CONFIG_SUNXI_NEW_PINCTRL
 * (selected by MACH_SUN8I_R528, arch/arm/mach-sunxi/Kconfig:488) and
 * include/sunxi_gpio.h:173-175 "SUNXI_PINCTRL_BANK_SIZE 0x30".
 *   bank base   = 0x02000000 + bank * 0x30
 *   CFGn        = bank + 0x00 + n*4,   4 bits per pin, 8 pins per register
 *   DATA        = bank + 0x10
 *   DRVn        = bank + 0x14 + n*4,   4 bits per pin (new pinctrl)
 *   PULLn       = bank + 0x24 + n*4,   2 bits per pin (new pinctrl) */
#define PIO_BANK_STRIDE         0x30u
#define PIO_CFG_OFF             0x00u
#define PIO_DATA_OFF            0x10u
#define PIO_DRV_OFF             0x14u
#define PIO_PULL_OFF            0x24u

#define PIO_BANK_A  0u
#define PIO_BANK_B  1u
#define PIO_BANK_C  2u
#define PIO_BANK_D  3u
#define PIO_BANK_E  4u
#define PIO_BANK_F  5u
#define PIO_BANK_G  6u

#define PIO_PULL_NONE   0u
#define PIO_PULL_UP     1u
#define PIO_PULL_DOWN   2u

/* ===================================================================== */
/* DMAC - the DMA controller at 0x03002000                               */
/* ===================================================================== */
/* [V-D1] [L] drivers/dma/sun6i-dma.c. The compatible
 * "allwinner,sun20i-d1-dma" binds sun50i_a100_dma_cfg (:1297, :1227-1243),
 * so: burst encoding "h3", DRQ encoding "h6", mode encoding "h6",
 * has_high_addr, has_mbus_clk. */
#define DMAC_IRQ_EN(x)          ((uint32_t)(x) * 4u)          /* :30        */
#define DMAC_IRQ_STAT(x)        ((uint32_t)(x) * 4u + 0x10u)  /* :38        */
#define DMAC_GATE               0x28u   /* SUNXI_H3_DMA_GATE, :53           */
#define DMAC_GATE_ENABLE        0x4u    /* SUNXI_H3_DMA_GATE_ENABLE, :54    */
#define DMAC_STAT               0x30u   /* :40                              */

#define DMAC_IRQ_HALF           (1u << 0)   /* :31                          */
#define DMAC_IRQ_PKG            (1u << 1)   /* :32                          */
#define DMAC_IRQ_QUEUE          (1u << 2)   /* :33                          */
#define DMAC_IRQ_CHAN_NR        8u          /* :35 -- 8 channels per status
                                             * register, 4 bits each        */
#define DMAC_IRQ_CHAN_WIDTH     4u          /* :36                          */

/* Per-channel block: base + 0x100 + ch*0x40. [L] sun6i-dma.c:1424. */
#define DMAC_CHAN_BASE(ch)      (0x100u + (uint32_t)(ch) * 0x40u)
#define DMAC_CH_ENABLE          0x00u   /* :58, START = BIT(0)              */
#define DMAC_CH_PAUSE           0x04u   /* :62                              */
#define DMAC_CH_LLI_ADDR        0x08u   /* :66                              */
#define DMAC_CH_CUR_CFG         0x0cu   /* :68                              */
#define DMAC_CH_CUR_SRC         0x10u   /* :84                              */
#define DMAC_CH_CUR_DST         0x14u   /* :86                              */
#define DMAC_CH_CUR_CNT         0x18u   /* :88                              */
#define DMAC_CH_CUR_PARA        0x1cu   /* :90                              */

/* LLI cfg word, "h6"/"h3" encodings. [L] sun6i-dma.c:70-83:
 *   SRC_DRQ  [5:0]    DMA_CHAN_CFG_SRC_DRQ_H6(x)   = x & 0x3f
 *   SRC_BURST[7:6]    DMA_CHAN_CFG_SRC_BURST_H3(x) = x << 6
 *   SRC_MODE [8]      DMA_CHAN_CFG_SRC_MODE_H6(x)  = x << 8
 *   SRC_WIDTH[10:9]   DMA_CHAN_CFG_SRC_WIDTH(x)    = x << 9
 *   the destination half is the same, shifted left by 16.                 */
#define DMA_CFG_SRC_DRQ(x)      ((uint32_t)(x) & 0x3Fu)
#define DMA_CFG_SRC_BURST(x)    (((uint32_t)(x) & 0x3u) << 6)
#define DMA_CFG_SRC_MODE(x)     (((uint32_t)(x) & 0x1u) << 8)
#define DMA_CFG_SRC_WIDTH(x)    (((uint32_t)(x) & 0x3u) << 9)
#define DMA_CFG_DST_DRQ(x)      (DMA_CFG_SRC_DRQ(x)   << 16)
#define DMA_CFG_DST_BURST(x)    (DMA_CFG_SRC_BURST(x) << 16)
#define DMA_CFG_DST_MODE(x)     (DMA_CFG_SRC_MODE(x)  << 16)
#define DMA_CFG_DST_WIDTH(x)    (DMA_CFG_SRC_WIDTH(x) << 16)

#define DMA_WIDTH_1B    0u      /* convert_buswidth() = ilog2(bytes), :~200 */
#define DMA_WIDTH_2B    1u
#define DMA_WIDTH_4B    2u
#define DMA_BURST_1     0u      /* convert_burst(), sun6i-dma.c:~185        */
#define DMA_BURST_4     1u
#define DMA_BURST_8     2u
#define DMA_BURST_16    3u

#define DMA_LINEAR_MODE 0u      /* :107                                     */
#define DMA_IO_MODE     1u      /* :108                                     */
#define DMA_DRQ_SDRAM   1u      /* :106                                     */
#define DMA_LLI_LAST    0xfffff800u /* :105                                 */
#define DMA_NORMAL_WAIT 8u      /* :104, written into lli->para             */

/* DRQ port for I2S1, both directions. [V-D1] [L] sunxi-d1s-t113.dtsi:273
 * "dmas = <&dma 4>, <&dma 4>; dma-names = "rx", "tx";" */
#define DMA_DRQ_I2S1            4u
#define DMA_DRQ_I2S2            5u

/* ===================================================================== */
/* I2S (the "daudio" block), offsets from T113_I2S1_BASE                  */
/* ===================================================================== */
/* [V-D1] [L] sound/soc/sunxi/sun4i-i2s.c. The D1/T113 compatible binds
 * sun50i_r329_i2s_quirks (:1481-1500, matched at :1670), which uses the
 * sun8i register map with the H6 channel-config and format helpers. Line
 * numbers below are sun4i-i2s.c's. */
#define I2S_CTRL                0x00u   /* :24                              */
#define I2S_FMT0                0x04u   /* :34                              */
#define I2S_FMT1                0x08u   /* :50                              */
#define I2S_INT_STA             0x0cu   /* :114 (SUN8I_I2S_INT_STA_REG)     */
#define I2S_FIFO_CTRL           0x14u   /* :57                              */
#define I2S_FIFO_STA            0x18u   /* :65                              */
#define I2S_DMA_INT_CTRL        0x1cu   /* :67                              */
#define I2S_TXCNT               0x28u   /* :80                              */
#define I2S_RXCNT               0x2cu   /* :81                              */
#define I2S_CLK_DIV             0x24u   /* :73                              */
#define I2S_FIFO_TX             0x20u   /* :115 (SUN8I_I2S_FIFO_TX_REG),
                                         * and quirks .reg_offset_txdata    */
#define I2S_CHAN_CFG            0x30u   /* :117                             */
#define I2S_TX_CHAN_SEL(pin)    (0x34u + 4u * (uint32_t)(pin))  /* :141     */
#define I2S_TX_CHAN_MAP0(pin)   (0x44u + 8u * (uint32_t)(pin))  /* :142     */
#define I2S_TX_CHAN_MAP1(pin)   (0x48u + 8u * (uint32_t)(pin))  /* :143     */
#define I2S_RX_CHAN_SEL         0x64u   /* :145                             */

#define I2S_CTRL_GL_EN          (1u << 0)   /* :32                          */
#define I2S_CTRL_RX_EN          (1u << 1)   /* :31                          */
#define I2S_CTRL_TX_EN          (1u << 2)   /* :30                          */
#define I2S_CTRL_MODE_PCM       (0u << 4)   /* :100                         */
#define I2S_CTRL_MODE_LEFT      (1u << 4)   /* :99                          */
#define I2S_CTRL_MODE_RIGHT     (2u << 4)   /* :98                          */
#define I2S_CTRL_MODE_MASK      (3u << 4)   /* :97                          */
#define I2S_CTRL_SDO_EN(n)      (1u << (8 + (n)))   /* :26                  */
#define I2S_CTRL_SDO_EN_MASK    (0xFu << 8)         /* :25                  */
#define I2S_CTRL_LRCK_OUT       (1u << 17)  /* :95                          */
#define I2S_CTRL_BCLK_OUT       (1u << 18)  /* :94                          */

#define I2S_FMT0_LRCK_POL_HIGH  (1u << 19)  /* :103                         */
#define I2S_FMT0_LRCK_POL_LOW   (0u << 19)  /* :104                         */
#define I2S_FMT0_LRCK_POL_MASK  (1u << 19)  /* :102                         */
#define I2S_FMT0_LRCK_PERIOD(p) ((uint32_t)((p) - 1u) << 8)  /* :106        */
#define I2S_FMT0_LRCK_PERIOD_MASK (0x3FFu << 8)             /* :105        */
#define I2S_FMT0_BCLK_POL_NORMAL (0u << 7)  /* :109                         */
#define I2S_FMT0_BCLK_POL_MASK   (1u << 7)  /* :107                         */
/* WSS and SR are 3-bit fields for this variant, NOT the 2-bit sun4i ones:
 * quirks .field_fmt_wss = REG_FIELD(FMT0, 0, 2), .field_fmt_sr =
 * REG_FIELD(FMT0, 4, 6)  (:1487-1488). Encoding from sun8i_i2s_get_sr_wss()
 * (:434-452): 8b=1, 12b=2, 16b=3, 20b=4, 24b=5, 28b=6, 32b=7. */
#define I2S_FMT0_WSS_SHIFT      0u
#define I2S_FMT0_WSS_MASK       0x7u
#define I2S_FMT0_SR_SHIFT       4u
#define I2S_FMT0_SR_MASK        0x7u
#define I2S_WIDTH_CODE(bits)    (((bits) / 4u) - 1u)  /* 16->3, 32->7       */

#define I2S_FMT1_SEXT_MASK      (3u << 4)   /* :111                         */
#define I2S_FMT1_SEXT(x)        ((uint32_t)(x) << 4) /* :112, 0 = pad LSB
                                             * with zeros (:~700)           */

#define I2S_FIFO_CTRL_FLUSH_TX  (1u << 25)  /* :58                          */
#define I2S_FIFO_CTRL_TX_MODE(m) ((uint32_t)(m) << 2)   /* :61              */
#define I2S_FIFO_CTRL_TX_MODE_MASK (1u << 2)            /* :60              */
#define I2S_FIFO_CTRL_RX_MODE(m) ((uint32_t)(m))        /* :62              */
#define I2S_FIFO_CTRL_RX_MODE_MASK (3u << 0)            /* :62              */

#define I2S_DMA_INT_TX_DRQ_EN   (1u << 7)   /* :68                          */
#define I2S_DMA_INT_RX_DRQ_EN   (1u << 3)   /* :69                          */

/* CLK_DIV: BCLK divider at [6:4], MCLK divider at [3:0], and -- for this
 * variant only -- MCLK_EN at bit 8, not bit 7:
 *     .field_clkdiv_mclk_en = REG_FIELD(SUN4I_I2S_CLK_DIV_REG, 8, 8)
 * (:1486). The sun4i MCLK_EN at bit 7 (:74) does NOT apply here. */
#define I2S_CLK_DIV_BCLK(x)     ((uint32_t)(x) << 4)
#define I2S_CLK_DIV_MCLK(x)     ((uint32_t)(x) << 0)
#define I2S_CLK_DIV_MCLK_EN     (1u << 8)

/* sun8i divider encoding, :259-274: value -> divider
 * 1->1 2->2 3->4 4->6 5->8 6->12 7->16 8->24 9->32 10->48 11->64 12->96
 * 13->128 14->176 15->192. Used for both BCLK and MCLK on this variant
 * (quirks .bclk_dividers = .mclk_dividers = sun8i_i2s_clk_div). */
#define I2S_DIV_1       1u
#define I2S_DIV_2       2u
#define I2S_DIV_4       3u
#define I2S_DIV_6       4u
#define I2S_DIV_8       5u
#define I2S_DIV_12      6u
#define I2S_DIV_16      7u
#define I2S_DIV_24      8u

/* Channel select / enable, H6 layout, :134-139:
 *   TX_CHAN_SEL_OFFSET [21:20], TX_CHAN_SEL [19:16], TX_CHAN_EN [15:0]. */
#define I2S_TX_CHAN_SEL_OFFSET(x) ((uint32_t)(x) << 20)
#define I2S_TX_CHAN_SEL_N(chan)   ((uint32_t)((chan) - 1u) << 16)
#define I2S_TX_CHAN_EN(chan)      ((uint32_t)((1u << (chan)) - 1u))

/* CHAN_CFG, :118-121: RX_SLOT_NUM [7:4], TX_SLOT_NUM [3:0], both n-1. */
#define I2S_CHAN_CFG_TX_SLOTS(n) ((uint32_t)((n) - 1u))
#define I2S_CHAN_CFG_RX_SLOTS(n) ((uint32_t)((n) - 1u) << 4)

/* Reset values, from the regmap defaults the driver declares for this
 * variant (:~1100, sun50i_h6_i2s_reg_defaults):
 *   CTRL 0x00060000, FMT0 0x00000033, FMT1 0x00000030,
 *   FIFO_CTRL 0x000400f0, DMA_INT_CTRL 0, CLK_DIV 0, CHAN_CFG 0,
 *   TX_CHAN_SEL 0, TX_CHAN_MAP0/1 0, RX_CHAN_SEL 0, RX_CHAN_MAP0/1 0. */
#define I2S_CTRL_RESET_VALUE        0x00060000u
#define I2S_FMT0_RESET_VALUE        0x00000033u
#define I2S_FMT1_RESET_VALUE        0x00000030u
#define I2S_FIFO_CTRL_RESET_VALUE   0x000400f0u

/* ===================================================================== */
/* SMHC - the SD/MMC host, offsets from T113_MMC0_BASE                    */
/* ===================================================================== */
/* [V-D1] [U] drivers/mmc/sunxi_mmc.h, struct sunxi_mmc (:15-57) and the
 * bit definitions after it. The CONFIG_SUNXI_GEN_NCAT2 branch (:49-54) is
 * the one that applies, which puts thldc at 0x100 and the FIFO at 0x200. */
#define SMHC_GCTRL      0x00u
#define SMHC_CLKCR      0x04u
#define SMHC_TIMEOUT    0x08u
#define SMHC_WIDTH      0x0cu
#define SMHC_BLKSZ      0x10u
#define SMHC_BYTECNT    0x14u
#define SMHC_CMD        0x18u
#define SMHC_ARG        0x1cu
#define SMHC_RESP0      0x20u
#define SMHC_RESP1      0x24u
#define SMHC_RESP2      0x28u
#define SMHC_RESP3      0x2cu
#define SMHC_IMASK      0x30u
#define SMHC_MINT       0x34u
#define SMHC_RINT       0x38u
#define SMHC_STATUS     0x3cu
#define SMHC_FTRGLEVEL  0x40u
#define SMHC_NTSR       0x5cu
#define SMHC_HWRST      0x78u
#define SMHC_DMAC       0x80u
#define SMHC_THLDC      0x100u
#define SMHC_SAMP_DL    0x144u
#define SMHC_FIFO       0x200u

#define SMHC_GCTRL_SOFT_RESET       (1u << 0)
#define SMHC_GCTRL_FIFO_RESET       (1u << 1)
#define SMHC_GCTRL_DMA_RESET        (1u << 2)
#define SMHC_GCTRL_RESET            (7u << 0)
#define SMHC_GCTRL_ACCESS_BY_AHB    (1u << 31)

#define SMHC_CLK_POWERSAVE          (1u << 17)
#define SMHC_CLK_ENABLE             (1u << 16)
#define SMHC_CLK_DIVIDER_MASK       0xFFu

#define SMHC_CMD_RESP_EXPIRE        (1u << 6)
#define SMHC_CMD_LONG_RESPONSE      (1u << 7)
#define SMHC_CMD_CHK_RESPONSE_CRC   (1u << 8)
#define SMHC_CMD_DATA_EXPIRE        (1u << 9)
#define SMHC_CMD_WRITE              (1u << 10)
#define SMHC_CMD_AUTO_STOP          (1u << 12)
#define SMHC_CMD_WAIT_PRE_OVER      (1u << 13)
#define SMHC_CMD_SEND_INIT_SEQ      (1u << 15)
#define SMHC_CMD_UPCLK_ONLY         (1u << 21)
#define SMHC_CMD_START              (1u << 31)

#define SMHC_RINT_RESP_ERROR        (1u << 1)
#define SMHC_RINT_COMMAND_DONE      (1u << 2)
#define SMHC_RINT_DATA_OVER         (1u << 3)
#define SMHC_RINT_RESP_CRC_ERROR    (1u << 6)
#define SMHC_RINT_DATA_CRC_ERROR    (1u << 7)
#define SMHC_RINT_RESP_TIMEOUT      (1u << 8)
#define SMHC_RINT_DATA_TIMEOUT      (1u << 9)
#define SMHC_RINT_VOLT_CHANGE_DONE  (1u << 10)
#define SMHC_RINT_FIFO_RUN_ERROR    (1u << 11)
#define SMHC_RINT_HARDWARE_LOCKED   (1u << 12)
#define SMHC_RINT_START_BIT_ERROR   (1u << 13)
#define SMHC_RINT_AUTO_CMD_DONE     (1u << 14)
#define SMHC_RINT_END_BIT_ERROR     (1u << 15)
#define SMHC_RINT_CARD_INSERT       (1u << 30)
#define SMHC_RINT_CARD_REMOVE       (1u << 31)
/* sunxi_mmc.h:103-113, "0xbfc2" in its own comment. */
#define SMHC_RINT_ERROR_BITS        0x0000BFC2u

#define SMHC_STATUS_RXWL_FLAG       (1u << 0)
#define SMHC_STATUS_TXWL_FLAG       (1u << 1)
#define SMHC_STATUS_FIFO_EMPTY      (1u << 2)
#define SMHC_STATUS_FIFO_FULL       (1u << 3)
#define SMHC_STATUS_CARD_PRESENT    (1u << 8)
#define SMHC_STATUS_CARD_BUSY       (1u << 9)
#define SMHC_STATUS_FSM_BUSY        (1u << 10)
#define SMHC_STATUS_FIFO_LEVEL(r)   (((r) >> 17) & 0x3FFFu)

#define SMHC_NTSR_MODE_SEL_NEW      (1u << 31)
#define SMHC_HWRST_ASSERT           0u
#define SMHC_HWRST_DEASSERT         1u
#define SMHC_THLDC_READ_EN          (1u << 0)
#define SMHC_THLDC_WRITE_EN         (1u << 2)
#define SMHC_THLDC_READ_THLD(x)     (((uint32_t)(x) & 0xFFFu) << 16)
#define SMHC_CAL_DL_SW_EN           (1u << 7)

/* ===================================================================== */
/* UART - DesignWare APB UART, 16550-compatible                          */
/* ===================================================================== */
/* [V-D1] [L] sunxi-d1s-t113.dtsi:323-334: compatible "snps,dw-apb-uart",
 * reg-io-width = <4>, reg-shift = <2>. So a 16550 register index i lives at
 * byte offset i*4 and is accessed 32 bits wide. The register names and the
 * DesignWare additions (USR at index 31, HALT at index 41) are from the
 * DesignWare DW_apb_uart databook, which is the standard this compatible
 * string names; they are not Allwinner-specific. */
#define UART_RBR        0x00u   /* read: receive buffer                     */
#define UART_THR        0x00u   /* write: transmit holding                  */
#define UART_DLL        0x00u   /* with LCR.DLAB: divisor low               */
#define UART_DLH        0x04u   /* with LCR.DLAB: divisor high              */
#define UART_IER        0x04u
#define UART_IIR        0x08u   /* read                                     */
#define UART_FCR        0x08u   /* write                                    */
#define UART_LCR        0x0cu
#define UART_MCR        0x10u
#define UART_LSR        0x14u
#define UART_MSR        0x18u
#define UART_USR        0x7cu   /* DesignWare: UART status                  */
#define UART_HALT       0xa4u   /* DesignWare: halt TX / change update      */

#define UART_IER_ERBFI  (1u << 0)   /* RX data available interrupt          */
#define UART_IER_ELSI   (1u << 2)   /* receiver line status interrupt       */
#define UART_IIR_ID_MASK 0x0Fu
#define UART_IIR_NO_INT 0x01u
#define UART_IIR_RLS    0x06u       /* receiver line status                 */
#define UART_IIR_RDA    0x04u       /* received data available              */
#define UART_IIR_CTI    0x0Cu       /* character timeout                    */
#define UART_FCR_FIFOE  (1u << 0)
#define UART_FCR_RFIFOR (1u << 1)
#define UART_FCR_XFIFOR (1u << 2)
#define UART_FCR_RT_1   (0u << 6)   /* RX trigger: 1 character              */
#define UART_LCR_WLEN8  0x03u
#define UART_LCR_DLAB   (1u << 7)
#define UART_LSR_DR     (1u << 0)   /* data ready                           */
#define UART_LSR_OE     (1u << 1)   /* overrun                              */
#define UART_LSR_PE     (1u << 2)   /* parity error                         */
#define UART_LSR_FE     (1u << 3)   /* framing error                        */
#define UART_LSR_BI     (1u << 4)   /* break                                */
#define UART_LSR_THRE   (1u << 5)   /* TX holding register empty            */
#define UART_LSR_TEMT   (1u << 6)   /* transmitter empty                    */
#define UART_USR_BUSY   (1u << 0)

#endif /* T113_SOC_H */
