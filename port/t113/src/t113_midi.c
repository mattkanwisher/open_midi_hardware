/* t113_midi.c - mtp_midi.h from UART2 RX, one interrupt per byte.
 *
 * port/include/mtp_midi.h defines this as a *byte* source with per-byte
 * timestamps, and port/DESIGN.md 3.3 specifies the ISR exactly: "read RBR,
 * read the timestamp, push {byte, t_us} into the ring, update the write index
 * with a release barrier. Nothing else. No parsing, no logging, no
 * allocation." That is what this is.
 *
 * WHY NOT DMA. port/DESIGN.md 3.3: at 3125 bytes/s the interrupt rate is
 * trivial, and DMA would cost the per-byte timestamp that the look-ahead knob
 * in 2.6 needs. The T113's UARTs do have DMA ports (sunxi-d1s-t113.dtsi:331,
 * "dmas = <&dma 14>, <&dma 14>" for uart0, 16 for uart2) and we deliberately
 * do not use them.
 *
 * THE RING. 256 entries, which port/DESIGN.md 3.3 sizes as "82 ms of wire at
 * full rate, which is thirty block periods -- if the render loop ever falls
 * that far behind, the audio has already broken". Single producer (the ISR),
 * single consumer (the render loop), monotonic indices, no disabled
 * interrupts.
 *
 * WHERE THIS RUNS. Both ends are on core 0: the render loop is the consumer
 * and the ISR preempts it. So strictly the barriers are belt and braces --
 * and they are here anyway, because port/DESIGN.md 3.3 says to write it as if
 * the ISR could be moved to core 1, and because the cost is a DMB per byte at
 * 3125 bytes per second.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include <stddef.h>
#include "mtp_midi.h"
#include "mtp_time.h"
#include "mtp_log.h"
#include "t113_soc.h"
#include "t113_board.h"
#include "t113.h"

#define MIDI_FIFO_ENTRIES   256u    /* port/DESIGN.md 3.3                   */

static mtp_midi_byte    g_fifo[MIDI_FIFO_ENTRIES];
static volatile uint32_t g_wr;      /* ISR only            */
static volatile uint32_t g_rd;      /* render loop only    */
static volatile uint32_t g_overruns;
static volatile uint32_t g_frame_errors;
static int g_open;

static volatile uint32_t *mu(uint32_t off)
{
    return (volatile uint32_t *)(uintptr_t)
        (T113_UART_BASE(T113_MIDI_UART) + off);
}

static void midi_rx_isr(void)
{
    /* Drain everything the FIFO holds. A 16550 asserts its interrupt at the
     * trigger level and keeps it asserted while data remains; returning after
     * one byte would work but would re-enter the vector per byte, and at a
     * trigger level of 1 that is the same thing done more expensively. */
    for (;;) {
        uint32_t lsr = *mu(UART_LSR);

        if (lsr & (UART_LSR_OE | UART_LSR_PE | UART_LSR_FE | UART_LSR_BI)) {
            /* Overrun is our fault (interrupt too slow); framing and parity
             * are the wire's. mtp_midi.h keeps them in separate counters
             * because on an opto-isolated DIN input "a non-zero value usually
             * means the optocoupler, not the software". Reading LSR clears
             * these bits on a 16550. */
            if (lsr & UART_LSR_OE) g_overruns++;
            if (lsr & (UART_LSR_PE | UART_LSR_FE | UART_LSR_BI))
                g_frame_errors++;
        }

        if (!(lsr & UART_LSR_DR)) return;

        {
            uint8_t  b  = (uint8_t)(*mu(UART_RBR) & 0xFFu);
            uint32_t wr = g_wr;
            uint32_t rd = g_rd;

            if (wr - rd >= MIDI_FIFO_ENTRIES) {
                g_overruns++;       /* application too slow */
                continue;           /* the byte is already consumed */
            }
            g_fifo[wr % MIDI_FIFO_ENTRIES].t_us = mtp_time_us();
            g_fifo[wr % MIDI_FIFO_ENTRIES].byte = b;
            g_fifo[wr % MIDI_FIFO_ENTRIES].pad[0] = 0u;
            g_fifo[wr % MIDI_FIFO_ENTRIES].pad[1] = 0u;
            g_fifo[wr % MIDI_FIFO_ENTRIES].pad[2] = 0u;
            /* Payload before index. */
            __asm__ volatile("dmb ish" ::: "memory");
            g_wr = wr + 1u;
        }
    }
}

mtp_status mtp_midi_open(uint32_t baud)
{
    if (g_open) return MTP_ERR_STATE;
    if (baud == 0u) return MTP_ERR_INVAL;

    g_wr = 0u;
    g_rd = 0u;
    g_overruns = 0u;
    g_frame_errors = 0u;

    t113_uart_midi_init(baud, midi_rx_isr);
    g_open = 1;
    MTP_LOGI("midi: uart%u RX at %u baud, %u-entry stamped FIFO (%u ms of "
             "wire)", (unsigned)T113_MIDI_UART, baud, MIDI_FIFO_ENTRIES,
             (unsigned)(MIDI_FIFO_ENTRIES * 10u * 1000u / baud));
    return MTP_OK;
}

void mtp_midi_close(void)
{
    if (!g_open) return;
    t113_uart_midi_stop();
    g_open = 0;
}

size_t mtp_midi_read(mtp_midi_byte *dst, size_t max)
{
    size_t n = 0;
    uint32_t wr;

    if (!g_open || !dst) return 0;
    wr = g_wr;
    /* Index before payload. */
    __asm__ volatile("dmb ish" ::: "memory");
    while (n < max && g_rd != wr) {
        dst[n++] = g_fifo[g_rd % MIDI_FIFO_ENTRIES];
        g_rd++;
    }
    return n;
}

uint32_t mtp_midi_overruns(void)     { return g_overruns; }
uint32_t mtp_midi_frame_errors(void) { return g_frame_errors; }

/* mtp_midi.h: "Always false on the target (a UART is never finished)". */
int mtp_midi_eof(void) { return 0; }
