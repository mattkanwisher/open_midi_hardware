/* desktop_config.c - see desktop_config.h for the format and why it is here.
 *
 * The scanner is deliberately dull. It is the code that will run on a
 * Cortex-A7 with no allocator available at the point it runs, reading a file
 * that a user edited on a Windows laptop with a text editor that may or may
 * not have added a BOM, and its failure mode has to be "say which line and
 * carry on" rather than "refuse to boot".
 *
 * SPDX-License-Identifier: 0BSD
 */

#include "desktop_config.h"
#include "mtp_storage.h"
#include "mtp_log.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The scanner. No allocation, no float, one pass.                     */

static int is_blank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

void cfg_scan(const char *text, size_t len, cfg_visit_fn visit, void *user,
              cfg_scan_stats *stats)
{
    size_t i = 0;
    unsigned line = 0;
    cfg_scan_stats st;

    memset(&st, 0, sizeof(st));
    if (!text) { if (stats) *stats = st; return; }

    /* A UTF-8 BOM is what a Windows editor leaves on a file it thinks is
     * Unicode. Skipping three bytes is cheaper than explaining it. */
    if (len >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        i = 3;

    while (i < len) {
        size_t bol = i, eol, ks, ke, vs, ve, eq;
        char key[CFG_MAX_KEY], value[CFG_MAX_VALUE];
        size_t n;

        while (i < len && text[i] != '\n') i++;
        eol = i;
        if (i < len) i++;                    /* step over the newline */
        line++;
        st.lines++;

        /* Trim, then drop a comment. A '#' or ';' inside a value is not a
         * comment -- paths contain them -- so only a comment that starts the
         * line counts. That is the rule busybox and U-Boot use and it is the
         * one that surprises people least. */
        while (bol < eol && is_blank(text[bol])) bol++;
        while (eol > bol && is_blank(text[eol - 1])) eol--;
        if (bol == eol) continue;                       /* blank */
        if (text[bol] == '#' || text[bol] == ';') continue;   /* comment */

        eq = bol;
        while (eq < eol && text[eq] != '=') eq++;
        if (eq == eol) { st.malformed++;
                         MTP_LOGW("config line %u: no '=' -- ignored", line);
                         continue; }

        ks = bol; ke = eq;
        while (ke > ks && is_blank(text[ke - 1])) ke--;
        vs = eq + 1; ve = eol;
        while (vs < ve && is_blank(text[vs])) vs++;

        if (ke == ks) { st.malformed++;
                        MTP_LOGW("config line %u: empty key -- ignored", line);
                        continue; }

        n = ke - ks;
        if (n >= CFG_MAX_KEY) n = CFG_MAX_KEY - 1;
        memcpy(key, text + ks, n);
        key[n] = '\0';

        n = ve - vs;
        if (n >= CFG_MAX_VALUE) {
            n = CFG_MAX_VALUE - 1;
            MTP_LOGW("config line %u: value truncated to %u characters",
                     line, (unsigned)n);
        }
        memcpy(value, text + vs, n);
        value[n] = '\0';

        st.pairs++;
        if (visit && !visit(user, key, value, line)) {
            st.unknown++;
            MTP_LOGW("config line %u: unknown key '%s' -- ignored "
                     "(not an error: a newer card may carry keys this build "
                     "does not have)", line, key);
        }
    }
    if (stats) *stats = st;
}

int cfg_as_uint(const char *value, uint32_t *out)
{
    uint32_t v = 0;
    const char *p = value;
    if (!p || !*p) return 0;
    while (*p) {
        if (*p < '0' || *p > '9') return 0;
        if (v > 429496729u) return 0;               /* would overflow */
        v = v * 10u + (uint32_t)(*p - '0');
        p++;
    }
    *out = v;
    return 1;
}

int cfg_as_bool(const char *value, int *out)
{
    static const char *const yes[] = { "on", "yes", "true", "1", 0 };
    static const char *const no[]  = { "off", "no", "false", "0", 0 };
    int i;
    for (i = 0; yes[i]; i++) if (!strcmp(value, yes[i])) { *out = 1; return 1; }
    for (i = 0; no[i];  i++) if (!strcmp(value, no[i]))  { *out = 0; return 1; }
    return 0;
}

/* ------------------------------------------------------------------ */
/* What mt32-desktop keeps in one.                                     */

void desktop_config_defaults(desktop_config *c)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->machine, sizeof(c->machine), "mt32");
    snprintf(c->rom_dir, sizeof(c->rom_dir), "roms");
    snprintf(c->engine,  sizeof(c->engine),  "auto");
    snprintf(c->log,     sizeof(c->log),     "info");
    snprintf(c->audio,   sizeof(c->audio),   "auto");
    c->rate      = 48000u;
    c->block     = 128u;
    c->ring      = 3u;
    c->lookahead = 0u;
    c->partials  = 32u;
    c->midi_baud = 31250u;
    c->reverb    = 1;
    c->periods   = 3u;
    c->status_ms = 500u;
}

#define STR_KEY(name, field)                                              \
    if (!strcmp(key, name)) {                                             \
        snprintf(c->field, sizeof(c->field), "%s", value); return 1; }
#define UINT_KEY(name, field)                                             \
    if (!strcmp(key, name)) {                                             \
        uint32_t v;                                                       \
        if (!cfg_as_uint(value, &v)) {                                    \
            MTP_LOGW("config line %u: %s wants a number, got '%s'",       \
                     line, key, value); return 1; }                       \
        c->field = v; return 1; }
#define BOOL_KEY(name, field)                                             \
    if (!strcmp(key, name)) {                                             \
        int v;                                                            \
        if (!cfg_as_bool(value, &v)) {                                    \
            MTP_LOGW("config line %u: %s wants on/off, got '%s'",         \
                     line, key, value); return 1; }                       \
        c->field = v; return 1; }

static int visit(void *user, const char *key, const char *value, unsigned line)
{
    desktop_config *c = (desktop_config *)user;

    /* On the card and on the T113 alike. */
    STR_KEY ("machine",       machine)
    STR_KEY ("rom_dir",       rom_dir)
    STR_KEY ("control_rom",   control_rom)
    STR_KEY ("pcm_rom",       pcm_rom)
    STR_KEY ("engine",        engine)
    STR_KEY ("log",           log)
    UINT_KEY("rate",          rate)
    UINT_KEY("block",         block)
    UINT_KEY("ring",          ring)
    UINT_KEY("lookahead",     lookahead)
    UINT_KEY("partials",      partials)
    UINT_KEY("midi_baud",     midi_baud)
    BOOL_KEY("reverb",        reverb)

    /* Desktop-only. The target has one I2S port and one UART and nothing to
     * choose, so it will ignore these with the same warning any other unknown
     * key gets -- which is the point of the prefix. */
    STR_KEY ("desktop_audio",     audio)
    STR_KEY ("desktop_device",    device)
    UINT_KEY("desktop_periods",   periods)
    UINT_KEY("desktop_status_ms", status_ms)

    (void)line;
    return 0;
}

mtp_status desktop_config_load(desktop_config *c, const char *path)
{
    static char buf[CFG_MAX_FILE + 1];   /* not on the stack: see mtp_render.h */
    long len = 0;
    mtp_status s;

    if (!c || !path) return MTP_ERR_INVAL;
    snprintf(c->path, sizeof(c->path), "%s", path);

    if (!mtp_storage_exists(path)) return MTP_ERR_NOENT;

    s = mtp_storage_load(path, buf, (long)CFG_MAX_FILE, &len);
    if (s == MTP_ERR_NOMEM) {
        MTP_LOGE("config '%s' is larger than %u bytes -- that is not a config "
                 "file", path, (unsigned)CFG_MAX_FILE);
        return s;
    }
    if (s != MTP_OK) { MTP_LOGE("cannot read config '%s'", path); return s; }

    buf[len] = '\0';
    cfg_scan(buf, (size_t)len, visit, c, &c->stats);
    c->loaded = 1;
    MTP_LOGI("config %s: %u lines, %u settings, %u unknown, %u malformed",
             path, c->stats.lines, c->stats.pairs, c->stats.unknown,
             c->stats.malformed);
    return MTP_OK;
}

void desktop_config_report(const desktop_config *c)
{
    if (!c->loaded) return;
    MTP_LOGI("config: machine %s, roms '%s', engine %s, %u Hz, block %u, "
             "ring %u, partials %u, reverb %s",
             c->machine, c->rom_dir, c->engine, c->rate, c->block, c->ring,
             c->partials, c->reverb ? "on" : "off");
}
