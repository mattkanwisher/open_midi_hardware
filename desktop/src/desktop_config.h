/* desktop_config.h - mt32.cfg, the file that will live on the SD card.
 *
 * WHY IT IS HERE AND NOT IN port/. docs/PLAN.md section 1 says the microSD
 * carries "ROMs, SoundFonts, config", and nothing had yet said what "config"
 * looks like. A format nobody has parsed is a format nobody has found the
 * mistakes in, so this build reads it now, on a laptop, where a mistake costs
 * a rebuild rather than an SD card and a serial console. When the seam owner
 * wants it, cfg_scan() below moves into port/src unchanged -- it is written to
 * the target's constraints, not the desktop's:
 *
 *   - it parses a buffer the caller owns, in place, and allocates nothing;
 *   - it never uses float, long long, sscanf, strtol or errno;
 *   - it is one forward pass, so it works on a file read in one go by
 *     mtp_storage_load() into a 4 kB static buffer;
 *   - an unknown key is a warning and not a failure, so an old firmware reads
 *     a new card and a new firmware reads an old one.
 *
 * THE FORMAT, in full:
 *
 *     # comment to end of line, ';' works too
 *     key = value
 *     key=value                  whitespace around '=' is optional
 *
 *   key      [a-z0-9_]+, at most 31 characters, case sensitive (lowercase)
 *   value    everything after '=' with leading and trailing blanks removed,
 *            at most 127 characters, no quoting and no escapes: a value is
 *            literal, so a path with a space in it just works
 *   numbers  decimal, unsigned, no sign and no suffix
 *   booleans on off yes no true false 1 0
 *   blank lines and lines that are only a comment are skipped
 *   CR is treated as blank, so a card written on Windows reads correctly
 *
 * There are no sections and no nesting, on purpose: a flat key space is four
 * lines of parser and it is the difference between debugging a config file
 * over a 115200-baud console and not.
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef DESKTOP_CONFIG_H
#define DESKTOP_CONFIG_H

#include "mtp_platform.h"

#define CFG_MAX_KEY   32
#define CFG_MAX_VALUE 128
#define CFG_MAX_FILE  4096

/* Called once per `key = value`. line is 1-based, for messages. Return
 * non-zero to accept the key; zero means "I do not know this key" and the
 * scanner counts it. */
typedef int (*cfg_visit_fn)(void *user, const char *key, const char *value,
                            unsigned line);

typedef struct {
    unsigned lines;      /* lines seen                                    */
    unsigned pairs;      /* key = value pairs found                       */
    unsigned unknown;    /* pairs the visitor did not recognise           */
    unsigned malformed;  /* lines that were neither blank, comment nor pair */
} cfg_scan_stats;

/* Scans text[0..len) in place. text is not modified. */
void cfg_scan(const char *text, size_t len, cfg_visit_fn visit, void *user,
              cfg_scan_stats *stats);

/* Value helpers, both total: they never fail, they report. */
int cfg_as_uint(const char *value, uint32_t *out);   /* 1 on success */
int cfg_as_bool(const char *value, int *out);        /* 1 on success */

/* ------------------------------------------------------------------ */
/* What mt32-desktop actually keeps in one. Every field has a default that
 * matches the program's own, so a missing file changes nothing. */

typedef struct {
    char     machine[8];        /* mt32 | cm32l                          */
    char     rom_dir[CFG_MAX_VALUE];
    char     control_rom[CFG_MAX_VALUE];
    char     pcm_rom[CFG_MAX_VALUE];
    char     engine[16];        /* auto | fake | mt32emu | mt32emu-fakerom */
    uint32_t rate, block, ring, lookahead, partials, midi_baud;
    int      reverb;
    char     log[8];            /* error | warn | info | debug           */
    /* Below here is desktop-only: the target has one I2S port and one UART,
     * so it has nothing to choose. Keys are prefixed `desktop_` in the file
     * so that a card written for the board and a laptop can share one. */
    char     audio[16];         /* auto | alsa | pulse | jack | null ... */
    char     device[CFG_MAX_VALUE];
    uint32_t periods;
    uint32_t status_ms;
    /* Diagnostics from the load. */
    int            loaded;      /* a file was read                       */
    char           path[CFG_MAX_VALUE];
    cfg_scan_stats stats;
} desktop_config;

void       desktop_config_defaults(desktop_config *c);

/* Reads `path` through mtp_storage and applies it. MTP_ERR_NOENT if there is
 * no such file, which is not an error to the caller: the defaults stand. */
mtp_status desktop_config_load(desktop_config *c, const char *path);

/* One line per non-default setting, for the run header. */
void       desktop_config_report(const desktop_config *c);

#endif /* DESKTOP_CONFIG_H */
