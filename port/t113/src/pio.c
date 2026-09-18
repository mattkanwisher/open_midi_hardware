/* pio.c - the pin controller: four-bit function fields, nothing else.
 *
 * Register layout is [V-D1] from U-Boot's drivers/gpio/sunxi_gpio.c:36-60 and
 * include/sunxi_gpio.h:173-175, in the CONFIG_SUNXI_NEW_PINCTRL form that
 * MACH_SUN8I_R528 selects (arch/arm/mach-sunxi/Kconfig:488).
 *
 * THE PART THAT IS NOT VERIFIED, and cannot be from software sources: which
 * of these balls exist on the T113-i's LFBGA. Every function number below is
 * read from mainline's pin table, which describes the *die*; the package is a
 * datasheet question. port/T113.md 7 lists it, hw/HARDWARE.md owns it, and
 * board bring-up step 3 is where a wrong assumption shows up as silence.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include "t113_soc.h"
#include "t113.h"

static volatile uint32_t *pio(unsigned bank, uint32_t off)
{
    return (volatile uint32_t *)(uintptr_t)
        (T113_PIO_BASE + bank * PIO_BANK_STRIDE + off);
}

/* CFGn holds 8 pins of 4 bits each: index = pin >> 3, shift = (pin & 7) * 4.
 * [V-D1] sunxi_gpio.c:36-37, GPIO_CFG_INDEX / GPIO_CFG_OFFSET. */
void t113_pio_set_function(unsigned bank, unsigned pin, unsigned fn)
{
    volatile uint32_t *r = pio(bank, PIO_CFG_OFF + (pin >> 3) * 4u);
    unsigned shift = (pin & 7u) * 4u;
    uint32_t v = *r;
    v &= ~(0xFu << shift);
    v |= (fn & 0xFu) << shift;
    *r = v;
}

/* PULLn holds 16 pins of 2 bits each, at +0x24 for the new pinctrl.
 * [V-D1] sunxi_gpio.c:49, 59-60. */
void t113_pio_set_pull(unsigned bank, unsigned pin, unsigned pull)
{
    volatile uint32_t *r = pio(bank, PIO_PULL_OFF + ((pin >> 4) & 1u) * 4u);
    unsigned shift = (pin & 15u) * 2u;
    uint32_t v = *r;
    v &= ~(0x3u << shift);
    v |= (pull & 0x3u) << shift;
    *r = v;
}

/* DRVn holds 8 pins of 4 bits each on the new pinctrl (2 bits on the old one
 * -- getting this wrong silently sets the *neighbouring* pin's strength).
 * [V-D1] sunxi_gpio.c:45-47. */
void t113_pio_set_drive(unsigned bank, unsigned pin, unsigned level)
{
    volatile uint32_t *r = pio(bank, PIO_DRV_OFF + (pin >> 3) * 4u);
    unsigned shift = (pin & 7u) * 4u;
    uint32_t v = *r;
    v &= ~(0xFu << shift);
    v |= (level & 0xFu) << shift;
    *r = v;
}
