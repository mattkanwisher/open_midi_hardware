/* t113_log.c - mtp_log.h on the console UART, and mtp_strerror.
 *
 * Identical in shape to emu/src/emu_log.c and port/host/host_log_stderr.c,
 * and carrying the same warning -- except that here the arithmetic in
 * port/include/mtp_log.h is about this UART rather than about someone else's:
 * "a printf into a 115200-baud UART is 87 us per character and will blow the
 * block budget". A 128-frame block at 48 kHz is 2667 us, so thirty-one
 * characters is the entire budget. The render loop must not log, and the
 * counters exist so that it does not have to.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdarg.h>
#include "mtp_log.h"
#include "t113.h"

int vprintf(const char *fmt, va_list ap);
int printf(const char *fmt, ...);

static mtp_log_level g_level = MTP_LOG_INFO;
static const char *const TAG[] = { "", "E", "W", "I", "D" };

void mtp_log_init(mtp_log_level l)      { g_level = l; }
void mtp_log_set_level(mtp_log_level l) { g_level = l; }
mtp_log_level mtp_log_get_level(void)   { return g_level; }

void mtp_log(mtp_log_level level, const char *fmt, ...)
{
    va_list ap;
    if (level > g_level || level == MTP_LOG_NONE) return;
    printf("[%s] ", TAG[level]);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    t113_uart_console_putc('\n');
}

const char *mtp_strerror(mtp_status s)
{
    switch (s) {
    case MTP_OK:        return "ok";
    case MTP_ERR_IO:    return "I/O error";
    case MTP_ERR_NOENT: return "no such file";
    case MTP_ERR_NOMEM: return "out of memory";
    case MTP_ERR_INVAL: return "invalid argument";
    case MTP_ERR_AGAIN: return "would block";
    case MTP_ERR_STATE: return "wrong state";
    default:            return "unknown";
    }
}
