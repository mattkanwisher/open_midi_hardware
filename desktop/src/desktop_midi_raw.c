/* desktop_midi_raw.c - MIDI in from a raw byte file, deterministically.
 *
 * WHY THIS EXISTS, when --midi-fifo already reads a file. A FIFO or a
 * redirected stdin is read by the poll() thread, so *when* a byte is handed to
 * the render loop depends on how the kernel schedules that thread against this
 * one. The bytes always arrive, and the parser counters always come out the
 * same, but the audio does not: an event that lands one pump call later lands
 * 128 frames later in the PCM. That makes --midi-fifo unusable for comparing
 * rendered audio between two runs, let alone between two machines.
 *
 * This source is pulled from mtp_midi_read() on the render thread, exactly as
 * port/host/host_midi_file.c and emu/src/emu_midi.c are, and with the same two
 * modes and the same arithmetic:
 *
 *   unpaced (default)  every byte is due at once, stamped as if it had been
 *                      clocked out at 31250 baud from the moment the source
 *                      opened. Deterministic: the whole stream is drained
 *                      before the first block is rendered, which is what makes
 *                      a byte-for-byte comparison against the other two
 *                      implementations mean something.
 *   paced              a byte becomes readable only once mtp_time_us() says
 *                      its 320 us of wire time has elapsed. Real arrival
 *                      pattern, not reproducible.
 *
 * conform.sh uses the unpaced mode. Nothing else in this directory needs it,
 * which is exactly why it is here and not in port/.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include "desktop_midi_src.h"
#include "desktop_midi.h"
#include "mtp_time.h"
#include "mtp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t  *g_data;
static long      g_len;
static long      g_pos;
static uint32_t  g_us_per_byte = 320u;
static uint64_t  g_t0;
static int       g_paced;
static int       g_open;

mtp_status draw_open(const char *path, uint32_t baud, int paced)
{
    FILE *fp;
    if (!path) return MTP_ERR_INVAL;
    if (g_open) { MTP_LOGE("only one --midi-raw source"); return MTP_ERR_STATE; }
    if (!baud) baud = 31250u;

    fp = fopen(path, "rb");
    if (!fp) { MTP_LOGE("cannot read '%s'", path); return MTP_ERR_NOENT; }
    fseek(fp, 0, SEEK_END);
    g_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (g_len < 0) { fclose(fp); return MTP_ERR_IO; }
    g_data = (uint8_t *)malloc((size_t)(g_len > 0 ? g_len : 1));
    if (!g_data) { fclose(fp); return MTP_ERR_NOMEM; }
    if (fread(g_data, 1, (size_t)g_len, fp) != (size_t)g_len) {
        fclose(fp); free(g_data); g_data = NULL; return MTP_ERR_IO;
    }
    fclose(fp);

    g_pos = 0;
    g_paced = paced;
    g_us_per_byte = 10000000u / baud;      /* 10 bits per byte -> 320 us */
    g_open = 1;
    MTP_LOGI("midi in: raw %s, %ld bytes, %u baud (%u us/byte)%s",
             path, g_len, baud, g_us_per_byte, paced ? "" : ", unpaced");
    return MTP_OK;
}

void draw_start(void) { g_t0 = mtp_time_us64(); }

void draw_pump(void)
{
    /* Push everything that is due. The hub's FIFO is drained by the caller
     * immediately after this returns, so "push then read" is the same
     * ordering host_midi_file.c gets by returning bytes directly. */
    while (g_pos < g_len) {
        uint64_t due_us = (uint64_t)g_pos * g_us_per_byte;
        uint8_t b;
        if (g_paced && (mtp_time_us64() - g_t0) < due_us) break;
        b = g_data[g_pos];
        desktop_midi_push(&b, 1u, (uint32_t)(g_t0 + due_us));
        g_pos++;
    }
}

int draw_eof(void) { return g_data == NULL || g_pos >= g_len; }

void draw_close(void)
{
    free(g_data);
    g_data = NULL;
    g_len = g_pos = 0;
    g_open = 0;
}
