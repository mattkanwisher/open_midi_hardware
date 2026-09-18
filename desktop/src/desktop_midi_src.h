/* desktop_midi_src.h - internal interface between the MIDI hub and its
 * per-source back ends. Not used above the seam.
 * SPDX-License-Identifier: 0BSD */
#ifndef DESKTOP_MIDI_SRC_H
#define DESKTOP_MIDI_SRC_H

#include "mtp_platform.h"

/* desktop_midi_tty.c: a real serial line at a non-standard baud rate.
 * Returns an fd in raw, non-blocking mode, or -1. */
int dtty_open(const char *path, uint32_t baud);

/* desktop_midi_raw.c: a raw byte stream from a file, pulled on the render
 * thread. paced == 0 delivers the whole stream before the first block is
 * rendered, which is what makes conform.sh's byte-for-byte comparison against
 * port/host and emu/ reproducible. */
mtp_status draw_open(const char *path, uint32_t baud, int paced);
void       draw_start(void);    /* t0 for the paced mode; called at open */
void       draw_pump(void);
int        draw_eof(void);
void       draw_close(void);

/* desktop_midi_smf.c: a Standard MIDI File, played against mtp_time_us(). */
mtp_status dsmf_open(const char *path, int loop);
void       dsmf_pump(void);     /* pushes whatever is due; never blocks */
int        dsmf_eof(void);
void       dsmf_close(void);

#endif /* DESKTOP_MIDI_SRC_H */
