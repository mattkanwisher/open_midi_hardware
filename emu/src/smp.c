/* smp.c - trying to make port/PORTING.md section 5's dual-core hazard fire.
 *
 * THE CLAIM UNDER TEST. port/PORTING.md found that mt32emu's MIDI event queue
 * synchronises its producer and its consumer with `volatile` and nothing else.
 * The code is Synth.cpp:2270-2283 (`MidiEventQueue::pushShortMessage`) and
 * Synth.cpp:2299-2310 (`peekMidiEvent`/`dropMidiEvent`), and the shape is the
 * textbook message-passing pattern:
 *
 *     producer                          consumer
 *     --------                          --------
 *     newEvent.shortMessageData = m;    if (startPosition == endPosition)
 *     newEvent.timestamp        = t;        nothing to do
 *     endPosition = newEndPosition;     else read ringBuffer[startPosition]
 *
 * `volatile` stops the *compiler* reordering those stores. It emits no barrier
 * instruction, so it does nothing about the *hardware*. On a Cortex-A7 -- a
 * weakly-ordered, multi-issue, write-buffered ARMv7-A core -- the store to
 * `endPosition` may become visible to the other core before the stores to the
 * event body, and the consumer may then read an event that has an index but
 * not yet a payload. The fix is one `dmb ish` before publishing the index and
 * one after observing it. DESIGN.md's answer is not to fix mt32emu but to
 * avoid needing to: MIDI and rendering live on one core.
 *
 * WHAT THIS FILE DOES. It boots the second core that `-M virt -smp 2` offers
 * (via PSCI CPU_ON, the only mechanism `-M virt` exposes), and runs two
 * classic litmus tests across the two cores, in Normal cacheable shareable
 * memory with the MMU on -- which matters, because with the MMU off ARMv7-A
 * treats all memory as Strongly Ordered and no reordering is architecturally
 * possible, so the experiment would be rigged to find nothing.
 *
 *   MP  (message passing, and it is mt32emu's queue restated in 40 lines,
 *        with the same field order and the same absence of barriers):
 *        core 1 writes a payload then an index; core 0 reads the index then
 *        the payload. A payload that does not match its index is the hazard.
 *
 *   SB  (store buffering):  x = 1; r1 = y   ||   y = 1; r2 = x
 *        Both reads returning 0 is impossible under sequential consistency
 *        and routine on real ARM, because each core's store sits in its own
 *        write buffer while its load is satisfied from memory. This is the
 *        single easiest weak-memory effect to observe on real silicon and the
 *        one a model either has or does not.
 *
 * READ THE RESULT WITH THE CAVEAT ATTACHED. A zero here is NOT evidence that
 * the code is safe. QEMU's TCG is not a memory-ordering model: each guest
 * instruction is translated to host code and, in the single-threaded round-
 * robin mode this image runs in, the two guest cores are not even executing at
 * the same time. Under MTTCG they would be, but TCG still emits host-native
 * accesses and relies on the *host's* ordering, and the x86-64 host this ran
 * on is TSO -- it has no store-store or load-load reordering to expose. So the
 * only correct reading of "0 violations" is "this experiment cannot see the
 * hazard", and the value of running it is to establish that, with a number,
 * so that nobody spends a day on it again.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include <stddef.h>

#include "emu.h"
#include "mtp_log.h"
#include "mtp_time.h"

int printf(const char *fmt, ...);

/* ---------------------------------------------------------------- PSCI -- */
/* PSCI 0.2 function IDs, SMC Calling Convention 32-bit. QEMU's `-M virt`
 * advertises `method = "hvc"` in its device tree (verified with
 * `qemu-system-arm -M virt -cpu cortex-a7 -smp 2 -machine dumpdtb=...`), so
 * HVC is tried first and SMC second. On the T113 neither is relevant: the BROM
 * releases CPU0 only and the second core is started through the CPUCFG block,
 * so this is QEMU plumbing, not portable code. */
#define PSCI_VERSION_FN   0x84000000u
#define PSCI_CPU_ON_FN    0x84000003u

typedef uint32_t (*psci_fn)(uint32_t, uint32_t, uint32_t, uint32_t);

static uint32_t psci_hvc(uint32_t fn, uint32_t a1, uint32_t a2, uint32_t a3)
{
    register uint32_t r0 __asm__("r0") = fn;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    register uint32_t r3 __asm__("r3") = a3;
    __asm__ volatile("hvc #0"
                     : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                     :: "memory");
    return r0;
}

static uint32_t psci_smc(uint32_t fn, uint32_t a1, uint32_t a2, uint32_t a3)
{
    register uint32_t r0 __asm__("r0") = fn;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    register uint32_t r3 __asm__("r3") = a3;
    __asm__ volatile("smc #0"
                     : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                     :: "memory");
    return r0;
}

/* ------------------------------------------------------------ the tests -- */

/* mt32emu's MidiEvent, field for field (Synth.h, MidiEventQueue::MidiEvent).
 * The payload fields are what the consumer must not see stale. */
typedef struct {
    volatile uint32_t shortMessageData;
    volatile uint32_t timestamp;
    volatile uint32_t sysexLength;
    volatile uint32_t sysexData;        /* a pointer there; a word here     */
} midi_event;

#define RING 64u

/* Deliberately NOT in the uncached window: the hazard is a property of Normal
 * cacheable shareable memory, which is where a real queue lives. */
static volatile midi_event g_ring[RING];
static volatile uint32_t   g_start_pos;     /* consumer writes, producer reads */
static volatile uint32_t   g_end_pos;       /* producer writes, consumer reads */

/* SB litmus */
static volatile uint32_t g_x, g_y;
static volatile uint32_t g_r1, g_r2;

/* handshake between the cores; itself barriered, because the harness must not
 * be the thing that is broken */
static volatile uint32_t g_go;
static volatile uint32_t g_sec_alive;
static volatile uint32_t g_sec_done;
static volatile uint32_t g_sec_iters;
static volatile uint32_t g_iters_want;
static volatile uint32_t g_mode;            /* 1 = MP, 2 = SB               */

/* results */
static volatile uint32_t g_mp_violations;
static volatile uint32_t g_mp_observed;
static volatile uint32_t g_sb_both_zero;
static volatile uint32_t g_sb_rounds;

static inline void dmb_ish(void) { __asm__ volatile("dmb ish" ::: "memory"); }

/* ---- core 1 ------------------------------------------------------------ */

/* Byte for byte the shape of MidiEventQueue::pushShortMessage: payload stores,
 * then the index store, with nothing between them. No dmb, on purpose. */
static void mp_producer(uint32_t n)
{
    uint32_t i;
    for (i = 1u; i <= n; i++) {
        uint32_t end = g_end_pos;
        uint32_t next = (end + 1u) & (RING - 1u);
        /* Strict ping-pong: wait until the queue is EMPTY before publishing.
         * With a deep queue the consumer usually runs behind, so by the time
         * it reads an event the payload has been in memory for thousands of
         * cycles and the race window was never open. Keeping exactly one
         * event in flight means the consumer is spinning on `endPosition` at
         * the instant the producer writes it, which is the only arrangement
         * in which the hazard could be seen at all. (First version used the
         * full 64-entry ring; it observed 2 000 000 events and could not have
         * caught anything.) */
        while (g_start_pos != end) { }
        (void)next;
        g_ring[end].shortMessageData = i;
        g_ring[end].timestamp        = i ^ 0xA5A5A5A5u;
        g_ring[end].sysexLength      = 0u;
        g_ring[end].sysexData        = 0u;
        g_end_pos = next;
        g_sec_iters = i;
    }
}

static void sb_producer(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        while (g_go != i + 1u) { }
        g_y = 1u;
        g_r2 = g_x;
        dmb_ish();
        g_sec_iters = i + 1u;
    }
}

/* One pass per mode core 0 selects, because the two litmus tests are run in
 * sequence on the same core and a producer that exits after the first one
 * leaves the second measuring nothing. (It did, the first time: "sb rounds 0
 * of 200000".) Mode 3 releases the core. */
void emu_secondary_main(void)
{
    g_sec_alive = 1u;
    dmb_ish();
    for (;;) {
        uint32_t m;
        while ((m = g_mode) == 0u) { }
        if      (m == 1u) mp_producer(g_iters_want);
        else if (m == 2u) sb_producer(g_iters_want);
        else              break;
        dmb_ish();
        g_sec_done = 1u;
        while (g_mode == m) { }        /* wait for core 0 to move us on */
    }
}

/* ---- core 0 ------------------------------------------------------------ */

/* MidiEventQueue::peekMidiEvent + dropMidiEvent, same absence of barriers. */
static void mp_consumer(uint32_t n)
{
    uint32_t got = 0u;
    uint64_t guard = mtp_time_us64() + 5000000ull;
    while (got < n) {
        uint32_t s = g_start_pos;
        if (s == g_end_pos) {
            if (mtp_time_us64() > guard) break;
            continue;
        }
        {
            uint32_t d = g_ring[s].shortMessageData;
            uint32_t t = g_ring[s].timestamp;
            g_mp_observed++;
            if (t != (d ^ 0xA5A5A5A5u)) g_mp_violations++;
            else if (d != got + 1u)     g_mp_violations++;
        }
        got++;
        g_start_pos = (s + 1u) & (RING - 1u);
    }
}

static void sb_consumer(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        g_x = 0u; g_y = 0u; g_r1 = 0u; g_r2 = 0u;
        dmb_ish();
        g_go = i + 1u;
        g_x = 1u;
        g_r1 = g_y;
        /* Wait for the other side of the round. */
        {
            uint64_t guard = mtp_time_us64() + 1000000ull;
            while (g_sec_iters != i + 1u) {
                if (mtp_time_us64() > guard) return;
            }
        }
        g_sb_rounds++;
        if (g_r1 == 0u && g_r2 == 0u) g_sb_both_zero++;
    }
}

/* ---- driver ------------------------------------------------------------ */

uint32_t emu_smp_mp_violations(void) { return g_mp_violations; }
uint32_t emu_smp_sb_both_zero(void)  { return g_sb_both_zero; }

int emu_smp_run(const char *conduit, uint32_t mp_iters, uint32_t sb_iters)
{
    extern char _secondary_start[];
    psci_fn call;
    uint32_t ver, rc;
    uint64_t t0;
    int strcmp(const char *a, const char *b);

    call = (conduit && strcmp(conduit, "smc") == 0) ? psci_smc : psci_hvc;

    printf("\n--- smp ---\n");
    printf("conduit             %s\n", call == psci_smc ? "smc" : "hvc");

    /* PSCI_VERSION first: it is the cheapest call and, unlike CPU_ON, a
     * failure to implement it costs nothing. If the conduit instruction is
     * UNDEFINED at PL1 in this configuration, the image's undefined-
     * instruction handler prints and exits -- which is itself the answer. */
    ver = call(PSCI_VERSION_FN, 0u, 0u, 0u);
    printf("psci version        0x%08x  (major %u minor %u)\n",
           ver, (ver >> 16) & 0x7FFFu, ver & 0xFFFFu);
    if (ver == 0xFFFFFFFFu || ver == 0u) {
        printf("psci                NOT IMPLEMENTED on this conduit\n");
        printf("second core         not started\n");
        printf("--- end smp ---\n");
        return -1;
    }

    g_mode = 0u; g_sec_alive = 0u; g_sec_done = 0u;
    dmb_ish();

    rc = call(PSCI_CPU_ON_FN, 1u, (uint32_t)(uintptr_t)_secondary_start, 0u);
    printf("cpu_on(mpidr=1)     %d\n", (int)rc);
    if ((int)rc != 0) {
        printf("second core         refused (%d)\n", (int)rc);
        printf("--- end smp ---\n");
        return -1;
    }

    t0 = mtp_time_us64();
    while (g_sec_alive == 0u) {
        if (mtp_time_us64() - t0 > 2000000ull) {
            printf("second core         started but never reached C\n");
            printf("--- end smp ---\n");
            return -1;
        }
    }
    printf("second core         alive after %u us\n",
           (uint32_t)(mtp_time_us64() - t0));

    /* --- MP --- */
    g_start_pos = g_end_pos = 0u;
    g_sec_iters = 0u;
    g_iters_want = mp_iters;
    dmb_ish();
    g_mode = 1u;
    t0 = mtp_time_us64();
    mp_consumer(mp_iters);
    printf("mp iterations       %u  (producer reached %u)\n",
           g_mp_observed, g_sec_iters);
    printf("mp elapsed          %u us\n", (uint32_t)(mtp_time_us64() - t0));
    printf("mp violations       %u\n", g_mp_violations);

    /* Wait for the producer to finish before switching modes. */
    t0 = mtp_time_us64();
    while (g_sec_done == 0u && mtp_time_us64() - t0 < 5000000ull) { }

    /* --- SB --- */
    g_sec_done = 0u;
    g_sec_iters = 0u;
    g_go = 0u;
    g_iters_want = sb_iters;
    dmb_ish();
    g_mode = 2u;
    t0 = mtp_time_us64();
    sb_consumer(sb_iters);
    printf("sb rounds           %u of %u\n", g_sb_rounds, sb_iters);
    printf("sb elapsed          %u us\n", (uint32_t)(mtp_time_us64() - t0));
    printf("sb both-zero        %u\n", g_sb_both_zero);

    /* Release the second core. */
    g_sec_done = 0u;
    dmb_ish();
    g_mode = 3u;

    /* Two separate verdicts, because conflating them is the whole trap. SB is
     * the control: it says whether this environment can show non-sequentially-
     * consistent execution at all. MP is the hazard: it is the reordering
     * mt32emu's queue is actually exposed to. On an x86-64 host the first
     * fires and the second cannot, because TSO permits store->load and forbids
     * store->store. See the comment at the top of this file. */
    printf("control (sb)        %s\n",
           g_sb_both_zero ? "FIRED -- this harness can see reordering"
                          : "SILENT -- this harness is sequentially consistent,"
                            " so the mp result below means NOTHING");
    printf("hazard (mp)         %s\n",
           g_mp_violations
           ? "OBSERVED -- port/PORTING.md 5 settled in the affirmative"
           : "NOT OBSERVED -- and on a TSO host it CANNOT be: mt32emu's queue"
             " is exposed to store-store and load-load reordering, which the"
             " host does not do and TCG does not add. Not evidence of safety.");
    printf("--- end smp ---\n");
    return 0;
}
