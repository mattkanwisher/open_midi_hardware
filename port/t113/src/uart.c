/* uart.c - two DesignWare APB UARTs: the console, polled, and MIDI in,
 * interrupt-driven.
 *
 * This is the file that replaces emu/src/console.c, which its own header
 * calls "the one part of emu/ that does not transfer to the T113". The seam
 * it has to fit is three functions -- init, putc, poll -- and it does.
 *
 * REGISTER MODEL. compatible "snps,dw-apb-uart" with reg-shift = <2> and
 * reg-io-width = <4> (linux sunxi-d1s-t113.dtsi:324-326), so a 16550 register
 * index i is at byte offset i*4 and is read and written 32 bits wide. The
 * register set is the 16550's plus DesignWare's USR (index 31) and HALT
 * (index 41); neither is Allwinner-specific.
 *
 * THE BAUD RATE. The reference is APB1, not a fixed 24 MHz. port/DESIGN.md
 * 3.3 says "31250 is exact from a 24 MHz reference (divisor 48 at 16x
 * oversampling)" -- which is true, and is true only if APB1 is running at
 * 24 MHz. U-Boot decides that, not us, so src/ccu.c reads APB1's configuration
 * and we divide from the answer. Two reasons that matters more than it looks:
 *
 *   - MIDI has no framing tolerance to spare. A UART tolerates roughly +/-2%
 *     of baud error before the stop bit lands in the wrong place; at 31250 a
 *     divisor rounded from a wrong reference is typically 4% or 100% out, and
 *     the symptom is "MIDI works for short messages and corrupts sysex",
 *     which is a miserable thing to debug.
 *   - the console at 115200 from 24 MHz is divisor 13.02: 13 rounds to
 *     115384 baud, 0.16% fast, which is fine. From a different APB1 it would
 *     not be, and a console that prints garbage before the first log line is
 *     how a bring-up session starts badly.
 *
 * Both divisors are computed, both are logged with their exact error, and an
 * error over 2% is a loud warning rather than a silent wrong answer.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include "t113_soc.h"
#include "t113_board.h"
#include "t113.h"
#include "mtp_log.h"
#include "mtp_time.h"

static volatile uint32_t *ur(unsigned n, uint32_t off)
{
    return (volatile uint32_t *)(uintptr_t)(T113_UART_BASE(n) + off);
}

/* Returns the divisor, and reports the resulting baud error in parts per
 * thousand through *err_ppt. Divisor 0 is illegal on a 16550 and is clamped
 * to 1, because a divisor of 0 stops the clock entirely. */
static uint32_t uart_divisor(uint32_t apb_hz, uint32_t baud, int32_t *err_ppt)
{
    uint32_t denom = baud * 16u;
    uint32_t div = (apb_hz + denom / 2u) / denom;
    uint32_t actual;
    if (div == 0u) div = 1u;
    actual = apb_hz / (div * 16u);
    *err_ppt = (int32_t)(((int64_t)actual - (int64_t)baud) * 1000 / (int64_t)baud);
    return div;
}

static void uart_program(unsigned n, uint32_t apb_hz, uint32_t baud,
                         const char *what)
{
    int32_t err;
    uint32_t div = uart_divisor(apb_hz, baud, &err);

    /* DesignWare: the divisor latch may only be written while the UART is not
     * busy. Wait for USR.BUSY to clear rather than assuming; a UART that
     * U-Boot left mid-transmit swallows the write silently. */
    {
        unsigned tries;
        for (tries = 0; tries < 10000u; tries++) {
            if (!(*ur(n, UART_USR) & UART_USR_BUSY)) break;
            mtp_time_delay_us(1u);
        }
    }

    *ur(n, UART_IER) = 0u;
    *ur(n, UART_FCR) = UART_FCR_FIFOE | UART_FCR_RFIFOR | UART_FCR_XFIFOR |
                       UART_FCR_RT_1;
    *ur(n, UART_LCR) = UART_LCR_WLEN8 | UART_LCR_DLAB;
    *ur(n, UART_DLL) = div & 0xFFu;
    *ur(n, UART_DLH) = (div >> 8) & 0xFFu;
    *ur(n, UART_LCR) = UART_LCR_WLEN8;      /* 8N1, DLAB cleared             */
    *ur(n, UART_MCR) = 0u;                  /* no flow control               */

    if (err > 20 || err < -20) {
        MTP_LOGE("uart%u: %s at %u baud from APB1=%u Hz needs divisor %u, "
                 "which is %d.%d%% off. A UART tolerates about 2%%.",
                 n, what, baud, apb_hz, div, err / 10,
                 (err < 0 ? -err : err) % 10);
    } else {
        MTP_LOGI("uart%u: %s %u baud, APB1 %u Hz, divisor %u, error %d/1000",
                 n, what, baud, apb_hz, div, err);
    }
}

/* ------------------------------------------------------------- console -- */

static int g_console_up;

void t113_uart_console_init(void)
{
    uint32_t apb;

    t113_ccu_gate_and_reset(CCU_UART_BGR, T113_CONSOLE_UART,
                            16u + T113_CONSOLE_UART);
    t113_pio_set_function(T113_CONSOLE_TX_BANK, T113_CONSOLE_TX_PIN,
                          T113_CONSOLE_PIN_FN);
    t113_pio_set_function(T113_CONSOLE_RX_BANK, T113_CONSOLE_RX_PIN,
                          T113_CONSOLE_PIN_FN);
    t113_pio_set_pull(T113_CONSOLE_RX_BANK, T113_CONSOLE_RX_PIN, PIO_PULL_UP);

    apb = t113_ccu_apb1_hz();
    uart_program(T113_CONSOLE_UART, apb, T113_CONSOLE_BAUD, "console");
    g_console_up = 1;
}

void t113_uart_console_putc(char c)
{
    if (!g_console_up) return;
    if (c == '\n') t113_uart_console_putc('\r');
    while (!(*ur(T113_CONSOLE_UART, UART_LSR) & UART_LSR_THRE)) { }
    *ur(T113_CONSOLE_UART, UART_THR) = (uint32_t)(unsigned char)c;
}

void t113_uart_console_write(const char *s, unsigned len)
{
    unsigned i;
    for (i = 0; i < len; i++) t113_uart_console_putc(s[i]);
}

int t113_uart_console_getc(void)
{
    if (!g_console_up) return -1;
    if (!(*ur(T113_CONSOLE_UART, UART_LSR) & UART_LSR_DR)) return -1;
    return (int)(*ur(T113_CONSOLE_UART, UART_RBR) & 0xFFu);
}

void t113_uart_console_flush(void)
{
    if (!g_console_up) return;
    while (!(*ur(T113_CONSOLE_UART, UART_LSR) & UART_LSR_TEMT)) { }
}

/* ---------------------------------------------------------- MIDI input -- */

void t113_uart_midi_init(uint32_t baud, t113_irq_handler rx_isr)
{
    uint32_t apb;

    t113_ccu_gate_and_reset(CCU_UART_BGR, T113_MIDI_UART, 16u + T113_MIDI_UART);
    t113_pio_set_function(T113_MIDI_RX_BANK, T113_MIDI_RX_PIN,
                          T113_MIDI_PIN_FN);
    /* Pull-up on the RX line. An opto-isolated MIDI input idles high and the
     * optocoupler's output is open-collector, so the pull-up is what makes
     * "no cable" read as idle rather than as a stream of framing errors.
     * port/DESIGN.md 3 note on the level-shifting. */
    t113_pio_set_pull(T113_MIDI_RX_BANK, T113_MIDI_RX_PIN, PIO_PULL_UP);

    apb = t113_ccu_apb1_hz();
    uart_program(T113_MIDI_UART, apb, baud, "MIDI in");

    /* RX trigger level 1 character: at 3125 bytes/s the interrupt rate is
     * trivial and port/include/mtp_midi.h wants a per-byte arrival timestamp,
     * which a deeper trigger level would smear. ELSI as well as ERBFI,
     * because framing and overrun errors are counters this port exports
     * (mtp_midi_frame_errors) and a line-status interrupt is how they arrive
     * without polling LSR. */
    t113_gic_set_handler(T113_IRQ_UART(T113_MIDI_UART), rx_isr);
    t113_gic_enable(T113_IRQ_UART(T113_MIDI_UART), 0x60u);
    *ur(T113_MIDI_UART, UART_IER) = UART_IER_ERBFI | UART_IER_ELSI;
}

void t113_uart_midi_stop(void)
{
    *ur(T113_MIDI_UART, UART_IER) = 0u;
    t113_gic_disable(T113_IRQ_UART(T113_MIDI_UART));
}
