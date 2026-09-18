/* t113.h - the interfaces *below* port/include, internal to this board port.
 *
 * Nothing above the platform seam ever sees this header. docs/PLAN.md 0.5
 * rule 1: no #ifdef T113 above the line, and the way that rule is kept is
 * that everything T113-shaped is declared here and nowhere else.
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef T113_H
#define T113_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- CPU, from src/start.S and src/mmu.c ------------------------------ */
void t113_wfi(void);
void t113_irq_enable(void);
void t113_irq_disable(void);
void t113_mmu_enable(void);
void t113_dcache_clean_range(const void *addr, uint32_t len);

/* Captured by start.S before anything is reconfigured; main() prints it.
 * boot/BRINGUP.md 4.4 asks for exactly this dump on first silicon, because it
 * is what answers the secure-vs-non-secure question. */
extern uint32_t t113_entry_state[12];
void t113_print_entry_state(void);

/* ---- GIC-400 (src/gic.c) ---------------------------------------------- */
typedef void (*t113_irq_handler)(void);
void t113_gic_init(void);
void t113_gic_set_handler(unsigned intid, t113_irq_handler h);
void t113_gic_enable(unsigned intid, unsigned priority);
void t113_gic_disable(unsigned intid);
void t113_irq_dispatch(void);
uint32_t t113_irq_count(void);
uint32_t t113_spurious_count(void);

/* ---- CCU (src/ccu.c) --------------------------------------------------- */
void     t113_ccu_gate_and_reset(uint32_t bgr_reg, unsigned gate_bit,
                                 unsigned reset_bit);
uint32_t t113_ccu_apb1_hz(void);
uint32_t t113_ccu_pll_periph0_hz(void);
/* Programs PLL_AUDIO0 for the requested family and returns the achieved
 * pll-audio0 (1x) rate in Hz, or 0 if the rate is not one this driver can
 * reach. See src/ccu.c for what "can reach" costs. */
uint32_t t113_ccu_audio_pll_init(uint32_t sample_rate);
/* Sets the i2s1 module clock from pll-audio0 and returns its rate. */
uint32_t t113_ccu_i2s1_clk_init(uint32_t want_hz);
void     t113_ccu_mmc0_clk_init(uint32_t card_hz);
void     t113_ccu_dump(void);

/* ---- PIO (src/pio.c) --------------------------------------------------- */
void t113_pio_set_function(unsigned bank, unsigned pin, unsigned fn);
void t113_pio_set_pull(unsigned bank, unsigned pin, unsigned pull);
void t113_pio_set_drive(unsigned bank, unsigned pin, unsigned level);

/* ---- UART (src/uart.c) ------------------------------------------------- */
void t113_uart_console_init(void);      /* the log UART, polled            */
void t113_uart_console_putc(char c);
void t113_uart_console_write(const char *s, unsigned len);
int  t113_uart_console_getc(void);
void t113_uart_console_flush(void);

/* The MIDI UART: interrupt-driven RX only. The ISR is in src/t113_midi.c. */
void t113_uart_midi_init(uint32_t baud, t113_irq_handler rx_isr);
void t113_uart_midi_stop(void);

/* ---- DMAC (src/dmac.c) ------------------------------------------------- */
/* One linked-list item, exactly the hardware's layout.
 * [V-D1] linux drivers/dma/sun6i-dma.c, struct sun6i_dma_lli. */
typedef struct t113_dma_lli {
    uint32_t cfg;
    uint32_t src;
    uint32_t dst;
    uint32_t len;
    uint32_t para;
    uint32_t next;      /* physical address of the next item, or LLI_LAST   */
    uint32_t pad[2];    /* to 32 bytes: one cache line, and the engine reads
                         * the first six words only                         */
} t113_dma_lli;

void t113_dmac_init(void);
void t113_dmac_set_handler(unsigned ch, t113_irq_handler h);
void t113_dmac_start(unsigned ch, const t113_dma_lli *first_lli_phys,
                     uint32_t irq_mask);
void t113_dmac_stop(unsigned ch);
uint32_t t113_dmac_cur_src(unsigned ch);

/* ---- I2S (src/i2s.c) --------------------------------------------------- */
/* Returns MTP_OK-ish (0) or negative. Configures I2S1 as clock master,
 * philips I2S, `channels` slots of `slot_bits`, `sample_bits` significant. */
int      t113_i2s_open(uint32_t sample_rate, unsigned channels,
                       unsigned sample_bits, unsigned slot_bits);
void     t113_i2s_start_tx(void);
void     t113_i2s_stop_tx(void);
uint32_t t113_i2s_fifo_addr(void);
uint32_t t113_i2s_bclk_hz(void);
void     t113_i2s_dump(void);

/* ---- SMHC (src/smhc.c) ------------------------------------------------- */
int  t113_smhc_init(void);                       /* bring up card, 0 = ok   */
int  t113_smhc_read_blocks(uint32_t lba, void *dst, uint32_t nblocks);
uint64_t t113_smhc_capacity_bytes(void);

/* ---- FAT (src/fat.c) --------------------------------------------------- */
typedef struct {
    uint32_t first_cluster;
    uint32_t size;
} fat_dirent;

int  fat_mount(void);
int  fat_lookup(const char *path, fat_dirent *out);
/* Reads up to len bytes from `offset` within the file. Returns bytes read. */
long fat_read(const fat_dirent *f, uint32_t offset, void *dst, uint32_t len);

/* ---- board glue (src/main.c, src/retarget.c) --------------------------- */
void t113_exit(int code) __attribute__((noreturn));
void t113_fatal_exception(uint32_t which, uint32_t pc) __attribute__((noreturn));
uint32_t t113_heap_used(void);
uint32_t t113_heap_size(void);
uint32_t t113_heap_high_water(void);
void    *t113_dma_alloc(size_t n, size_t align);
uint32_t t113_dma_used(void);

#ifdef __cplusplus
}
#endif

#endif /* T113_H */
