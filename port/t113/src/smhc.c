/* smhc.c - SMHC0 as a read-only SD card reader, polled, no DMA.
 *
 * SCOPE, AND WHY IT IS THIS SMALL. Everything this device reads off the card
 * is read once, at boot, before the audio loop starts: two ROM images and one
 * text file (port/include/mtp_storage.h, "No writing, no directory
 * enumeration, no seeking"). So there is no write path, no interrupt, no DMA
 * and no card-detect handling here. port/DESIGN.md 6.3 lists "SD access
 * *while* rendering" as one of the three things that would justify an RTOS;
 * none of them is in this build, and this driver is what that decision looks
 * like in code.
 *
 * REGISTER MAP [V-D1]: U-Boot drivers/mmc/sunxi_mmc.h, the whole file, with
 * the CONFIG_SUNXI_GEN_NCAT2 branch selected -- which is the branch
 * MACH_SUN8I_R528 takes (arch/arm/mach-sunxi/Kconfig:486 selects
 * SUNXI_GEN_NCAT2). That branch is what puts THLDC at 0x100 and the FIFO at
 * 0x200 rather than the FIFO at 0x100; getting it wrong reads a threshold
 * register 512 times and returns a block of garbage with no error flagged.
 *
 * RESET AND CLOCK SEQUENCE [V-D1]: U-Boot drivers/mmc/sunxi_mmc.c,
 * sunxi_mmc_reset() (the SUN50I_GEN_H6/SUNXI_GEN_NCAT2 arm: card hardware
 * reset plus the FIFO threshold "needed on H616"), mmc_config_clock() and
 * mmc_update_clk() -- the UPCLK_ONLY command that has to bracket every clock
 * change, which is the single most commonly forgotten step in a sunxi MMC
 * bring-up and produces a controller that answers CMD0 and nothing else.
 *
 * CARD PROTOCOL: the ordinary SD initialisation, cross-checked against
 * U-Boot's drivers/mmc/mmc.c -- CMD0, CMD8 (mmc_send_if_cond:2807-2825, arg
 * 0x1aa, echo check on 0xaa), ACMD41 with HCS looping on OCR_BUSY
 * (sd_send_op_cond:665-712), CMD2, CMD3, CMD9, CMD7, ACMD6 for four-bit,
 * CMD16, CMD17/CMD18. Command numbers from U-Boot include/mmc.h:89-130.
 *
 * SPEED. src/ccu.c parents the module clock on the 24 MHz oscillator and
 * rounds the divider so the card clock never exceeds what was asked for. That
 * keeps us inside SD default speed (25 MHz) with no delay-line calibration at
 * all, which is the one part of a sunxi MMC driver that genuinely needs the
 * user manual. The cost is boot time and it is logged: a 1 MB PCM ROM at
 * 16 MHz on four lines is about 130 ms of bus time.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include <stddef.h>
#include "t113_soc.h"
#include "t113_board.h"
#include "t113.h"
#include "mtp_log.h"
#include "mtp_time.h"

static volatile uint32_t *sd(uint32_t off)
{ return (volatile uint32_t *)(uintptr_t)(T113_SD_BASE + off); }

/* Command numbers, U-Boot include/mmc.h:89-130. */
#define CMD_GO_IDLE             0
#define CMD_ALL_SEND_CID        2
#define CMD_SEND_RELATIVE_ADDR  3
#define CMD_SET_BUS_WIDTH       6       /* ACMD */
#define CMD_SELECT_CARD         7
#define CMD_SEND_IF_COND        8
#define CMD_SEND_CSD            9
#define CMD_STOP_TRANSMISSION   12
#define CMD_SET_BLOCKLEN        16
#define CMD_READ_SINGLE_BLOCK   17
#define CMD_READ_MULTI_BLOCK    18
#define CMD_SD_SEND_OP_COND     41      /* ACMD */
#define CMD_APP_CMD             55

#define OCR_BUSY                0x80000000u  /* include/mmc.h:160 */
#define OCR_HCS                 0x40000000u  /* include/mmc.h:161 */
#define OCR_VOLTAGE_3V          0x00FF8000u  /* 2.7-3.6 V, mmc.c:2814 */

#define RESP_NONE   0
#define RESP_R1     1       /* 48-bit, CRC checked                       */
#define RESP_R1B    2
#define RESP_R2     3       /* 136-bit                                   */
#define RESP_R3     4       /* 48-bit, no CRC (OCR)                      */
#define RESP_R6     RESP_R1
#define RESP_R7     RESP_R1

static uint32_t g_rca;
static int      g_hcs;          /* high capacity: addresses are blocks    */
static uint64_t g_capacity;
static int      g_ready;
static uint32_t g_resp[4];

uint64_t t113_smhc_capacity_bytes(void) { return g_capacity; }

static int wait_rint(uint32_t done_bit, uint32_t timeout_ms, const char *what)
{
    uint32_t t0 = mtp_time_us();
    for (;;) {
        uint32_t st = *sd(SMHC_RINT);
        if (st & SMHC_RINT_ERROR_BITS) {
            MTP_LOGE("smhc: %s error, RINT=0x%08x", what, (unsigned)st);
            return -1;
        }
        if (st & done_bit) return 0;
        if (mtp_time_us() - t0 > timeout_ms * 1000u) {
            MTP_LOGE("smhc: %s timeout, RINT=0x%08x STATUS=0x%08x", what,
                     (unsigned)st, (unsigned)*sd(SMHC_STATUS));
            return -1;
        }
    }
}

/* Every clock change has to be bracketed by an "update clock only" command,
 * which is a command with no index that the controller uses to re-time its
 * internal clock divider. U-Boot mmc_update_clk(). */
static int update_clk(void)
{
    uint32_t t0;
    *sd(SMHC_CMD) = SMHC_CMD_START | SMHC_CMD_UPCLK_ONLY |
                    SMHC_CMD_WAIT_PRE_OVER;
    t0 = mtp_time_us();
    while (*sd(SMHC_CMD) & SMHC_CMD_START) {
        if (mtp_time_us() - t0 > 2000000u) {
            MTP_LOGE("smhc: clock update did not complete");
            return -1;
        }
    }
    *sd(SMHC_RINT) = *sd(SMHC_RINT);    /* the update sets spurious bits */
    return 0;
}

static int set_clock(uint32_t card_hz)
{
    uint32_t c = *sd(SMHC_CLKCR);

    c &= ~SMHC_CLK_ENABLE;
    *sd(SMHC_CLKCR) = c;
    if (update_clk()) return -1;

    t113_ccu_mmc0_clk_init(card_hz);

    /* Internal divider cleared: the module clock is the card clock (modulo
     * the factor-of-two disagreement src/ccu.c documents). U-Boot does the
     * same and leaves all the division to the CCU. */
    c &= ~SMHC_CLK_DIVIDER_MASK;
    *sd(SMHC_CLKCR) = c;

    /* "have to set delay of zero before starting calibration" -- U-Boot
     * mmc_config_clock(), for every controller that can calibrate, and
     * sunxi_mmc_can_calibrate() returns true for SUNXI_GEN_NCAT2. */
    *sd(SMHC_SAMP_DL) = SMHC_CAL_DL_SW_EN;

    c |= SMHC_CLK_ENABLE;
    *sd(SMHC_CLKCR) = c;
    return update_clk();
}

static int send_cmd(uint32_t idx, uint32_t arg, int resp,
                    void *data, uint32_t blocks, uint32_t blocksize)
{
    uint32_t cmdval = SMHC_CMD_START | idx;
    uint32_t *buf = (uint32_t *)data;

    *sd(SMHC_RINT) = 0xFFFFFFFFu;       /* write-one-to-clear */

    if (idx == CMD_GO_IDLE) cmdval |= SMHC_CMD_SEND_INIT_SEQ;
    if (resp != RESP_NONE)  cmdval |= SMHC_CMD_RESP_EXPIRE;
    if (resp == RESP_R2)    cmdval |= SMHC_CMD_LONG_RESPONSE;
    /* R3 carries the OCR and has no CRC; asking the controller to check one
     * makes every ACMD41 fail with a CRC error. */
    if (resp != RESP_R3 && resp != RESP_NONE)
        cmdval |= SMHC_CMD_CHK_RESPONSE_CRC;

    if (data) {
        cmdval |= SMHC_CMD_DATA_EXPIRE | SMHC_CMD_WAIT_PRE_OVER;
        if (blocks > 1u) cmdval |= SMHC_CMD_AUTO_STOP;
        *sd(SMHC_BLKSZ)   = blocksize;
        *sd(SMHC_BYTECNT) = blocks * blocksize;
    }

    *sd(SMHC_ARG) = arg;

    if (!data) {
        *sd(SMHC_CMD) = cmdval;
    } else {
        uint32_t words = (blocks * blocksize) >> 2;
        uint32_t i = 0;
        uint32_t t0;

        /* Read the FIFO through the AHB port rather than through the internal
         * DMA: one bit, and it removes a whole descriptor format from a path
         * that runs three times per boot. U-Boot's mmc_trans_data_by_cpu()
         * sets the same bit for the same reason. */
        *sd(SMHC_GCTRL) = *sd(SMHC_GCTRL) | SMHC_GCTRL_ACCESS_BY_AHB;
        *sd(SMHC_CMD) = cmdval;

        t0 = mtp_time_us();
        while (i < words) {
            uint32_t status = *sd(SMHC_STATUS);
            uint32_t level;
            if (status & SMHC_STATUS_FIFO_EMPTY) {
                if (*sd(SMHC_RINT) & SMHC_RINT_ERROR_BITS) {
                    MTP_LOGE("smhc: read error, RINT=0x%08x",
                             (unsigned)*sd(SMHC_RINT));
                    return -1;
                }
                if (mtp_time_us() - t0 > 2000000u) {
                    MTP_LOGE("smhc: read stalled at word %u of %u",
                             (unsigned)i, (unsigned)words);
                    return -1;
                }
                continue;
            }
            /* STATUS[30:17] is the current FIFO level, so a whole burst can
             * be taken without re-reading STATUS per word -- U-Boot's comment
             * calls this "effectively doubling the read performance". A level
             * of 0 with FIFO_FULL set is a known lie on some parts; take a
             * conservative 32 words in that case. */
            level = SMHC_STATUS_FIFO_LEVEL(status);
            if (level == 0u && (status & SMHC_STATUS_FIFO_FULL)) level = 32u;
            if (level == 0u) continue;
            if (level > words - i) level = words - i;
            while (level--) buf[i++] = *sd(SMHC_FIFO);
            __asm__ volatile("dmb" ::: "memory");
            t0 = mtp_time_us();
        }
    }

    if (wait_rint(SMHC_RINT_COMMAND_DONE, 1000u, "command")) return -1;

    if (data) {
        if (wait_rint(SMHC_RINT_DATA_OVER, 2000u, "data")) return -1;
        if (blocks > 1u &&
            wait_rint(SMHC_RINT_AUTO_CMD_DONE, 1000u, "auto-stop"))
            return -1;
    }

    if (resp == RESP_R2) {
        /* The controller returns the 136-bit response with the low word
         * first, so the CSD's bit 127..96 are in RESP3. */
        g_resp[0] = *sd(SMHC_RESP3);
        g_resp[1] = *sd(SMHC_RESP2);
        g_resp[2] = *sd(SMHC_RESP1);
        g_resp[3] = *sd(SMHC_RESP0);
    } else {
        g_resp[0] = *sd(SMHC_RESP0);
        g_resp[1] = g_resp[2] = g_resp[3] = 0u;
    }
    return 0;
}

static int app_cmd(uint32_t idx, uint32_t arg, int resp)
{
    if (send_cmd(CMD_APP_CMD, g_rca << 16, RESP_R1, NULL, 0, 0)) return -1;
    return send_cmd(idx, arg, resp, NULL, 0, 0);
}

int t113_smhc_init(void)
{
    unsigned i;
    uint32_t t0;

    g_ready = 0;
    g_rca = 0;
    g_hcs = 0;
    g_capacity = 0;

    /* 1. Clocks and pins. Six pins, all function 2: PF0..PF5 is
     * D1 D0 CLK CMD D3 D2 (pinctrl-sun20i-d1.c:580-...; the dtsi groups
     * exactly PF0..PF5 as mmc0_pins at :121-124). Pull-ups on everything but
     * CLK: an SD bus idles high and the card's own pull-ups are weak. */
    t113_ccu_gate_and_reset(CCU_MMC_BGR, T113_SD_BGR_GATE_BIT,
                            T113_SD_BGR_RESET_BIT);
    for (i = 0; i <= 5u; i++) {
        t113_pio_set_function(T113_SD_PIN_BANK, i, T113_SD_PIN_FN);
        if (i != 2u) t113_pio_set_pull(T113_SD_PIN_BANK, i, PIO_PULL_UP);
        /* Drive strength 3 (of 0..3 in the 4-bit field) on CLK and CMD. The
         * field is 4 bits wide on the new pinctrl, and the encoding beyond
         * "bigger is stronger" is a datasheet question -- so we set a value
         * inside the range every sunxi driver uses and note it. */
        t113_pio_set_drive(T113_SD_PIN_BANK, i, 3u);
    }

    /* 2. Controller reset, then the card's own reset line, then the FIFO
     * threshold. U-Boot sunxi_mmc_reset(), NCAT2 arm, including the 1 ms /
     * 10 us / 300 us delays. */
    *sd(SMHC_GCTRL) = SMHC_GCTRL_RESET;
    mtp_time_delay_us(1000u);
    t0 = mtp_time_us();
    while (*sd(SMHC_GCTRL) & SMHC_GCTRL_RESET) {
        if (mtp_time_us() - t0 > 100000u) {
            MTP_LOGE("smhc: controller reset never cleared (GCTRL=0x%08x). "
                     "Bus gate or reset not released?",
                     (unsigned)*sd(SMHC_GCTRL));
            return -1;
        }
    }
    *sd(SMHC_HWRST) = SMHC_HWRST_ASSERT;
    mtp_time_delay_us(10u);
    *sd(SMHC_HWRST) = SMHC_HWRST_DEASSERT;
    mtp_time_delay_us(300u);
    *sd(SMHC_THLDC) = SMHC_THLDC_READ_THLD(512) | SMHC_THLDC_WRITE_EN |
                      SMHC_THLDC_READ_EN;

    *sd(SMHC_TIMEOUT) = 0xFFFFFFFFu;    /* the longest the controller can
                                         * wait; we time out ourselves      */
    *sd(SMHC_IMASK)   = 0u;             /* polled: no interrupt to the GIC  */
    *sd(SMHC_RINT)    = 0xFFFFFFFFu;
    *sd(SMHC_WIDTH)   = 0u;             /* 1-bit until ACMD6                */

    /* 3. Identification at 400 kHz -- the SD spec's initialisation clock. */
    if (set_clock(400000u)) return -1;
    mtp_time_delay_us(2000u);           /* 74 clocks at 400 kHz = 185 us;
                                         * 2 ms is free and unambiguous     */

    if (send_cmd(CMD_GO_IDLE, 0u, RESP_NONE, NULL, 0, 0)) return -1;
    mtp_time_delay_us(2000u);

    /* CMD8: arg 0x1AA = "2.7-3.6 V" plus the 0xAA check pattern, which the
     * card echoes. A card that does not answer is pre-2.0 and is not
     * high-capacity; we do not support those, and say so rather than
     * silently addressing it wrongly. */
    if (send_cmd(CMD_SEND_IF_COND, 0x1AAu, RESP_R7, NULL, 0, 0) ||
        (g_resp[0] & 0xFFu) != 0xAAu) {
        MTP_LOGE("smhc: card did not answer CMD8 (SD 1.x or no card). "
                 "Only SDHC/SDXC are supported.");
        return -1;
    }

    /* ACMD41 until the card leaves busy. U-Boot allows 1000 iterations of
     * 1 ms; the SD spec's own limit is 1 second. */
    for (i = 0; i < 1000u; i++) {
        if (app_cmd(CMD_SD_SEND_OP_COND, OCR_VOLTAGE_3V | OCR_HCS, RESP_R3))
            return -1;
        if (g_resp[0] & OCR_BUSY) break;
        mtp_time_delay_us(1000u);
    }
    if (!(g_resp[0] & OCR_BUSY)) {
        MTP_LOGE("smhc: card never left busy (OCR=0x%08x)",
                 (unsigned)g_resp[0]);
        return -1;
    }
    g_hcs = (g_resp[0] & OCR_HCS) != 0u;

    if (send_cmd(CMD_ALL_SEND_CID, 0u, RESP_R2, NULL, 0, 0)) return -1;
    if (send_cmd(CMD_SEND_RELATIVE_ADDR, 0u, RESP_R6, NULL, 0, 0)) return -1;
    g_rca = g_resp[0] >> 16;

    if (send_cmd(CMD_SEND_CSD, g_rca << 16, RESP_R2, NULL, 0, 0)) return -1;
    {
        /* CSD version 2 (SDHC/SDXC): C_SIZE is bits [69:48], capacity is
         * (C_SIZE + 1) * 512 KB. g_resp[] holds bits 127..96, 95..64, 63..32,
         * 31..0, so [69:48] spans g_resp[1] and g_resp[2]. */
        uint32_t csd_structure = g_resp[0] >> 30;
        if (csd_structure == 1u) {
            uint32_t c_size = ((g_resp[1] & 0x3Fu) << 16) | (g_resp[2] >> 16);
            g_capacity = ((uint64_t)c_size + 1u) * 512u * 1024u;
        } else {
            /* CSD v1 is a different geometry and only appears on SDSC, which
             * CMD8 already ruled out. Report zero rather than a wrong size. */
            g_capacity = 0u;
            MTP_LOGW("smhc: CSD structure %u, capacity not decoded",
                     (unsigned)csd_structure);
        }
    }

    if (send_cmd(CMD_SELECT_CARD, g_rca << 16, RESP_R1B, NULL, 0, 0)) return -1;

    /* 4. Four-bit bus, then the data clock. ACMD6 argument 2 = 4 bits.
     * The controller's own width register has to follow, and the order
     * matters: tell the card first, then the host, or the next command goes
     * out on four lines to a card still listening on one. */
    if (app_cmd(CMD_SET_BUS_WIDTH, 2u, RESP_R1)) return -1;
    *sd(SMHC_WIDTH) = 1u;               /* 0 = 1-bit, 1 = 4-bit             */

    if (send_cmd(CMD_SET_BLOCKLEN, 512u, RESP_R1, NULL, 0, 0)) return -1;
    if (set_clock(T113_SD_CLOCK_HZ)) return -1;

    g_ready = 1;
    MTP_LOGI("smhc: SD%s card, RCA 0x%04x, %u MiB, 4-bit, clock <= %u Hz",
             g_hcs ? "HC/XC" : "SC", (unsigned)g_rca,
             (unsigned)(g_capacity >> 20), (unsigned)T113_SD_CLOCK_HZ);
    return 0;
}

int t113_smhc_read_blocks(uint32_t lba, void *dst, uint32_t nblocks)
{
    uint32_t arg;

    if (!g_ready || !dst || nblocks == 0u) return -1;
    if (((uintptr_t)dst & 3u) != 0u) {
        /* The FIFO is read 32 bits at a time straight into the caller's
         * buffer. U-Boot refuses an unaligned destination for the same
         * reason; so do we, loudly, rather than trapping on an unaligned
         * word store with SCTLR.A off and corrupting a neighbour. */
        MTP_LOGE("smhc: destination %p is not 4-byte aligned", dst);
        return -1;
    }

    /* A high-capacity card is addressed in blocks; a standard-capacity one in
     * bytes. We only get here with SDHC/SDXC, but the shift is what makes the
     * distinction, and getting it backwards reads 512 times the wrong place. */
    arg = g_hcs ? lba : lba * 512u;

    if (nblocks == 1u)
        return send_cmd(CMD_READ_SINGLE_BLOCK, arg, RESP_R1, dst, 1u, 512u);
    return send_cmd(CMD_READ_MULTI_BLOCK, arg, RESP_R1, dst, nblocks, 512u);
}
