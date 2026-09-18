/* t113_board.h - every choice that belongs to the *board* rather than to the
 * SoC, in one file, so that porting to a different T113 board is editing this
 * and nothing else.
 *
 * Each choice is a decision this workstream made, with the reason and the
 * citation for the pin function number. None of them is verified against a
 * T113-i package ballout, because no T113-i datasheet was reachable from this
 * session (docs/HISTORY.md 8, boot/BRINGUP.md "Sites blocked"). They are
 * verified against mainline's *die* pin table, which is a different claim.
 * hw/HARDWARE.md owns the package question; port/T113.md 7 lists it.
 *
 * Pin function numbers are [V-D1] from
 * linux drivers/pinctrl/sunxi/pinctrl-sun20i-d1.c, line numbers given.
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef T113_BOARD_H
#define T113_BOARD_H

#include "t113_soc.h"

/* ---- console ----------------------------------------------------------- */
/* UART0 on PB8/PB9, function 6. pinctrl-sun20i-d1.c:113 (PB8 "uart0" TX) and
 * :123 (PB9 "uart0" RX). PB8/PB9 is the pair the sunxi world calls the debug
 * console on this die and the pair every D1/T113 devkit brings to a header.
 *
 * Board note: mangopi_mq_r_defconfig in U-Boot sets CONFIG_CONS_INDEX=4, i.e.
 * UART3, so on *that* board U-Boot's console is elsewhere. We do not have to
 * match U-Boot -- two UARTs can be up at once -- but if you want one cable,
 * change these four lines. */
#ifndef T113_CONSOLE_UART
#define T113_CONSOLE_UART       0u
#define T113_CONSOLE_TX_BANK    PIO_BANK_B
#define T113_CONSOLE_TX_PIN     8u
#define T113_CONSOLE_RX_BANK    PIO_BANK_B
#define T113_CONSOLE_RX_PIN     9u
#define T113_CONSOLE_PIN_FN     6u
#endif
#ifndef T113_CONSOLE_BAUD
#define T113_CONSOLE_BAUD       115200u
#endif

/* ---- MIDI in ----------------------------------------------------------- */
/* UART2 on PB0/PB1, function 7. pinctrl-sun20i-d1.c:25 (PB0 "uart2" TX) and
 * :37 (PB1 "uart2" RX). port/DESIGN.md 3.3 names UART2 "or whichever pin
 * group the board settles on"; this is the settling.
 *
 * Only RX is wired. There is no MIDI OUT in this design (port/DESIGN.md 5,
 * "deliberately absent: no MIDI output"), so PB0 is left alone and can be
 * anything the board wants. */
#ifndef T113_MIDI_UART
#define T113_MIDI_UART          2u
#define T113_MIDI_RX_BANK       PIO_BANK_B
#define T113_MIDI_RX_PIN        1u
#define T113_MIDI_PIN_FN        7u
#endif

/* ---- I2S out ----------------------------------------------------------- */
/* I2S1 on PG12 (LRCK), PG13 (BCLK), PG15 (DOUT0), all function 2.
 * pinctrl-sun20i-d1.c:742, :752, :772.
 *
 * I2S1 rather than I2S0 because I2S1 is the block mainline documents: it has
 * a DT node (sunxi-d1s-t113.dtsi:265), a driver binding, a DMA port number
 * and a pin table. I2S0's base address exists only in Allwinner's own HAL,
 * which carries no licence grant (boot/BRINGUP.md 5.1).
 *
 * No MCLK. PG11 function 2 is i2s1 MCLK (pinctrl-sun20i-d1.c:733) if a DAC
 * ever needs one, but the PCM5102A runs from its internal PLL on BCLK alone,
 * which is why port/DESIGN.md 2.1 chose it. Defining T113_I2S_MCLK_OUT
 * without also defining T113_I2S_MCLK_HZ is a deliberate link error. */
#ifndef T113_I2S_BASE
#define T113_I2S_BASE           T113_I2S1_BASE
#define T113_I2S_DRQ            DMA_DRQ_I2S1
#define T113_I2S_BGR_GATE_BIT   1u      /* ccu-sun20i-d1.c:577, bus-i2s1     */
#define T113_I2S_BGR_RESET_BIT  17u     /* ccu-sun20i-d1.c:1279, RST_BUS_I2S1 */
#define T113_I2S_LRCK_BANK      PIO_BANK_G
#define T113_I2S_LRCK_PIN       12u
#define T113_I2S_BCLK_BANK      PIO_BANK_G
#define T113_I2S_BCLK_PIN       13u
#define T113_I2S_DOUT_BANK      PIO_BANK_G
#define T113_I2S_DOUT_PIN       15u
#define T113_I2S_PIN_FN         2u
#endif

/* Slot width. port/DESIGN.md 2.1 computes the clock tree from "48 kHz x 32
 * bits x 2 channels = 3.072 MHz BCLK", so 32-bit slots carrying 16-bit
 * samples. Linux's default for S16_LE would be 16-bit slots and 1.536 MHz;
 * the PCM5102A accepts either (it recovers the frame from LRCK). 32 is kept
 * because it is what the design document's numbers describe, and because a
 * wider slot gives the DAC's own PLL more edges to lock to. */
#ifndef T113_I2S_SLOT_BITS
#define T113_I2S_SLOT_BITS      32u
#endif
#ifndef T113_I2S_SAMPLE_BITS
#define T113_I2S_SAMPLE_BITS    16u
#endif

/* ---- DMA --------------------------------------------------------------- */
/* Channel 0 of 16 (sunxi-d1s-t113.dtsi:507 "dma-channels = <16>"). Nothing
 * else in this firmware uses the DMAC -- the SD path is polled PIO and the
 * MIDI UART is per-byte interrupt -- so there is no allocator, and channel 0
 * is simply reserved for audio for the life of the image. */
#ifndef T113_AUDIO_DMA_CHAN
#define T113_AUDIO_DMA_CHAN     0u
#endif

/* ---- SD ---------------------------------------------------------------- */
/* SMHC0 on PF0..PF5, function 2: D1 D0 CLK CMD D3 D2 in that pin order.
 * pinctrl-sun20i-d1.c:580-588 for PF0 ("mmc0" D1) and the five that follow;
 * sunxi-d1s-t113.dtsi:121-124 groups exactly these six as mmc0_pins. */
#ifndef T113_SD_BASE
#define T113_SD_BASE            T113_MMC0_BASE
#define T113_SD_PIN_BANK        PIO_BANK_F
#define T113_SD_PIN_FN          2u
#define T113_SD_BGR_GATE_BIT    0u      /* ccu-sun20i-d1.c:449, bus-mmc0     */
#define T113_SD_BGR_RESET_BIT   16u     /* ccu-sun20i-d1.c:1257, RST_BUS_MMC0 */
#endif

/* Card clock for the data phase. Under SD default speed (25 MHz max) with no
 * delay-line tuning. src/ccu.c rounds down, never up. */
#ifndef T113_SD_CLOCK_HZ
#define T113_SD_CLOCK_HZ        16000000u
#endif

#endif /* T113_BOARD_H */
