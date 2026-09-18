/* main.c - the T113 payload: a bring-up harness that turns into the module.
 *
 * This is not a test program that will later be replaced. It is the boot
 * sequence port/DESIGN.md 4.1 specifies -- "mount, read config (defaults if
 * absent), load ROMs, open the synth, start I2S, start the UART" -- with the
 * fake engine standing in for mt32emu until workstream A's gate is answered
 * and ROMs exist. Swapping in mt32emu is a Makefile flag and one call, exactly
 * as it is in emu/.
 *
 * WHAT IT PRINTS, AND WHY EACH LINE EARNS ITS PLACE ON A BOARD THAT HAS NEVER
 * RUN. Every one of these answers a specific bring-up question in
 * port/T113.md 6, in order:
 *
 *   entry state     which core, what mode, secure or not, was the MMU on --
 *                   boot/BRINGUP.md 4.4 asks for exactly this dump and calls
 *                   it "a 10-line test that removes a guess"
 *   assumptions     every MTP_T113_UNVERIFIED constant compiled into this
 *                   image, with its value and why it is unverified
 *   clock tree      what U-Boot left behind, and what we programmed
 *   peripherals     one line each as they come up, and a named failure if not
 *   counters        every few seconds, for ever: the underrun count, the
 *                   minimum ring occupancy, the MIDI overrun count
 *
 * A module that is silent tells you nothing. A module that says
 * "audio: DMA started ... irqs 0" tells you the DMA never fired, which is one
 * register away from the answer.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include <stddef.h>

#include "mtp_platform.h"
#include "mtp_audio.h"
#include "mtp_midi.h"
#include "mtp_storage.h"
#include "mtp_time.h"
#include "mtp_log.h"
#include "mtp_engine.h"
#include "mtp_render.h"

#include "t113_soc.h"
#include "t113_board.h"
#include "t113.h"
#include "t113_unverified.h"

#include "t113_libc.h"

/* port/DESIGN.md 2.2: 128 frames, ring 3, target occupancy 2. 48 kHz from
 * 2.1. These are the design's numbers and they are not guesses; the two that
 * "should move in response to a measurement" are block and ring, and they are
 * here rather than buried so that a bring-up session can change them in one
 * place. */
#define AUDIO_RATE      48000u
#define AUDIO_BLOCK     128u
#define AUDIO_RING      3u
#define AUDIO_TARGET    2u

static mtp_render_ctx g_ctx;

void t113_print_entry_state(void)
{
    const uint32_t *s = t113_entry_state;
    uint32_t cpsr = s[0], sctlr = s[1], idpfr1 = s[5];

    printf("entry: CPSR   0x%08x  mode %s%s\n", (unsigned)cpsr,
           (cpsr & 0x1Fu) == 0x13u ? "SVC" :
           (cpsr & 0x1Fu) == 0x1Au ? "HYP" :
           (cpsr & 0x1Fu) == 0x1Fu ? "SYS" : "other",
           (cpsr & 0x80u) ? ", IRQ masked" : ", IRQ UNMASKED");
    printf("entry: SCTLR  0x%08x  MMU %s, D-cache %s, I-cache %s\n",
           (unsigned)sctlr,
           (sctlr & 1u) ? "ON" : "off",
           (sctlr & (1u << 2)) ? "ON" : "off",
           (sctlr & (1u << 12)) ? "ON" : "off");
    /* boot/BRINGUP.md 4.1: `bootm` must hand over with the MMU and D-cache
     * off, because it is the only U-Boot command that calls
     * cleanup_before_linux(). If they are on, the loader used `go` or
     * `bootelf` and start.S has just enabled a second set of page tables on
     * top of U-Boot's. Say so in the one place anyone will look. */
    if (sctlr & 1u)
        printf("entry: *** the MMU was ALREADY ON. Use `bootm`, not `go` --"
               " boot/BRINGUP.md 4.1 ***\n");
    printf("entry: ACTLR  0x%08x  SMP %s\n", (unsigned)s[2],
           (s[2] & (1u << 6)) ? "set by the loader" : "was clear");
    printf("entry: MIDR   0x%08x  %s\n", (unsigned)s[4],
           ((s[4] >> 4) & 0xFFFu) == 0xC07u ? "Cortex-A7 (expected)"
                                            : "NOT a Cortex-A7");
    printf("entry: ID_PFR1 0x%08x  security ext %s, virt ext %s\n",
           (unsigned)idpfr1,
           ((idpfr1 >> 4) & 0xFu) ? "present" : "absent",
           ((idpfr1 >> 12) & 0xFu) ? "present" : "absent");
    printf("entry: CNTFRQ 0x%08x (%u Hz)\n", (unsigned)s[6], (unsigned)s[6]);
    printf("entry: r0=0x%08x r1=0x%08x r2=0x%08x  running at 0x%08x\n",
           (unsigned)s[7], (unsigned)s[8], (unsigned)s[9], (unsigned)s[10]);
    printf("entry: MPIDR  0x%08x  core %u\n", (unsigned)s[11],
           (unsigned)(s[11] & 0xFFu));
}

void t113_print_assumptions(void)
{
    const mtp_t113_assumption *a;
    unsigned n = 0;

    for (a = __mtp_assumptions_start; a < __mtp_assumptions_end; a++) n++;
    printf("\n--- %u unverified constants in this image ---\n", n);
    for (a = __mtp_assumptions_start; a < __mtp_assumptions_end; a++)
        printf("  %-24s 0x%08x  %s\n", a->tag, (unsigned)a->value, a->why);
    if (n == 0u)
        printf("  (none -- which would mean every number came from a cited "
               "source)\n");
    printf("---\n\n");
}

static void print_counters(const mtp_render_ctx *ctx)
{
    printf("[c] blocks %u  underruns %u  minq %u/%u  dma-irq %u  played %u  "
           "midi %u/%u/%u  ovr %u  fe %u  heap %u/%u\n",
           (unsigned)ctx->stats.blocks,
           (unsigned)mtp_audio_underruns(),
           (unsigned)(ctx->stats.min_queued == 0xFFFFFFFFu
                      ? 0u : ctx->stats.min_queued),
           (unsigned)AUDIO_RING,
           (unsigned)t113_audio_dma_irqs(),
           (unsigned)t113_audio_played(),
           (unsigned)ctx->stats.midi_bytes,
           (unsigned)ctx->stats.short_msgs,
           (unsigned)ctx->stats.sysex_msgs,
           (unsigned)mtp_midi_overruns(),
           (unsigned)mtp_midi_frame_errors(),
           (unsigned)t113_heap_used(), (unsigned)t113_heap_size());
}

int main(void)
{
    mtp_audio_config  acfg;
    mtp_engine_config ecfg;
    mtp_engine       *inst = NULL;
    const mtp_engine_vtable *vt = &mtp_engine_fake;
    mtp_status s;
    uint64_t next_report;

    /* 1. Console first, before anything can fail silently. It needs the CCU
     *    gate, which needs t113_ccu_gate_and_reset()'s delays, which is why
     *    mtp_time_delay_us() has a pre-timebase path (src/t113_time.c). */
    t113_uart_console_init();
    mtp_log_init(MTP_LOG_INFO);

    printf("\n\n=== mt32-t113 platform layer, T113 build ===\n");
    printf("built " __DATE__ " " __TIME__ ", image at 0x40200000\n\n");

    t113_print_entry_state();
    t113_print_assumptions();

    /* 2. Timebase. Everything after this can measure itself. */
    mtp_time_init();
    t113_time_report();

    /* 3. Interrupts. Nothing has enabled one yet, so this cannot arrive
     *    early. */
    t113_gic_init();
    MTP_LOGI("gic: distributor 0x%08x, cpu interface 0x%08x, %u lines",
             (unsigned)T113_GICD_BASE, (unsigned)T113_GICC_BASE,
             (unsigned)t113_gic_lines());

    t113_ccu_dump();

    /* 4. Storage. port/DESIGN.md 4.3 is explicit that a missing card is the
     *    normal case and must not be fatal: "Log it. Play nothing. ... Do not
     *    hang -- a UART console still comes up, so mt32.cfg can be diagnosed
     *    over serial." */
    s = mtp_storage_mount();
    if (s != MTP_OK) {
        MTP_LOGW("storage: no usable card (%s). Continuing with defaults and "
                 "no ROMs; the module will boot, accept MIDI and output "
                 "silence.", mtp_strerror(s));
    } else {
        MTP_LOGI("storage: mounted, %u MiB card",
                 (unsigned)(t113_smhc_capacity_bytes() >> 20));
        MTP_LOGI("storage: /mt32.cfg %s, /roms/MT32_CONTROL.ROM %s",
                 mtp_storage_exists("mt32.cfg") ? "present" : "absent",
                 mtp_storage_exists("roms/MT32_CONTROL.ROM") ? "present"
                                                             : "absent");
    }

    /* 5. DMA controller, before the audio sink that uses it. */
    t113_dmac_init();
    MTP_LOGI("dmac: base 0x%08x, %u channels, audio on channel %u, IRQ %u",
             (unsigned)T113_DMAC_BASE, (unsigned)T113_DMAC_CHANNELS,
             (unsigned)T113_AUDIO_DMA_CHAN, (unsigned)T113_IRQ_DMAC);

    /* 6. The engine, before the DAC: port/DESIGN.md 4.1 wants the synth open
     *    before I2S starts "so that the first block of audio is real rather
     *    than a ramp from silence". */
    ecfg.control_rom_path = "roms/MT32_CONTROL.ROM";
    ecfg.pcm_rom_path     = "roms/MT32_PCM.ROM";
    ecfg.output_rate      = AUDIO_RATE;
    ecfg.max_partials     = 32u;
    ecfg.reverb_enabled   = 1;
    s = vt->open(&ecfg, &inst);
    if (s != MTP_OK) {
        MTP_LOGE("engine %s would not open: %s", vt->name, mtp_strerror(s));
        t113_exit(1);
    }
    MTP_LOGI("engine: %s, %u Hz", vt->name, AUDIO_RATE);

    /* 7. Audio. This is the call that programs PLL_AUDIO0, and it is the one
     *    most likely to be the first thing that fails on new silicon. */
    acfg.sample_rate      = AUDIO_RATE;
    acfg.channels         = 2u;
    acfg.frames_per_block = (uint16_t)AUDIO_BLOCK;
    acfg.block_count      = (uint8_t)AUDIO_RING;
    s = mtp_audio_open(&acfg);
    if (s != MTP_OK) {
        MTP_LOGE("audio open failed: %s. The module will still accept MIDI "
                 "and still talk over serial; see port/T113.md 6 step 5.",
                 mtp_strerror(s));
    } else {
        t113_i2s_dump();
    }

    /* 8. MIDI in. */
    s = mtp_midi_open(31250u);
    if (s != MTP_OK) MTP_LOGE("midi open failed: %s", mtp_strerror(s));

    /* 9. The render loop, which is port/src/mtp_render.c -- the same object
     *    file the host harness and the QEMU image link. Nothing below this
     *    line is T113-specific, and that is docs/PLAN.md 0.5 rule 1 holding
     *    in practice rather than in principle. */
    s = mtp_render_init(&g_ctx, vt, inst, AUDIO_BLOCK, AUDIO_RATE,
                        AUDIO_TARGET, 0u);
    if (s != MTP_OK) {
        MTP_LOGE("render init: %s", mtp_strerror(s));
        t113_exit(1);
    }

    MTP_LOGI("running. Counters every 5 s; minq is the safety margin "
             "(port/DESIGN.md 2.4).");
    next_report = mtp_time_us64() + 5000000ull;

    for (;;) {
        /* The superloop from port/DESIGN.md 2.4, unchanged: pump, then block
         * in mtp_audio_wait() until the DMA frees a block. The 100 ms timeout
         * exists so that a stopped DMA still prints counters instead of
         * hanging in WFI for ever -- which on a board with no display is the
         * difference between "it is broken" and "it is broken here". */
        mtp_render_pump(&g_ctx, AUDIO_RING);
        mtp_audio_wait(100000u);

        if (mtp_time_us64() >= next_report) {
            print_counters(&g_ctx);
            next_report += 5000000ull;
        }
    }
}
