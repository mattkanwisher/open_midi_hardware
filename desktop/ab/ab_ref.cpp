/* ab_ref.cpp - the reference leg of the A/B rig: the engine, and nothing else.
 *
 * WHY THIS EXISTS. `ab.sh` wants to answer "does OUR pipeline render the same
 * audio as somebody else's front end driving the same synthesiser?". The
 * strongest answer available is Munt's own `mt32emu-smf2wav`, which is built
 * and used when there are ROMs. But smf2wav loads ROMs from files and hashes
 * them, so on a machine with no Roland data -- which is every machine in this
 * project -- it cannot run at all, and the rig would degrade to nothing.
 *
 * This is the leg that still works there. It opens the engine through
 * port/include/mtp_engine.h and drives it directly:
 *
 *      read events -> render to the next event -> play it -> repeat
 *
 * What it does NOT use is everything this project wrote above the engine:
 * port/src/mtp_midi_parser.c, port/src/mtp_render.c, the audio ring, the
 * block scheduler, the timestamp arithmetic, desktop/src. Those are exactly
 * the parts an A/B is asking about, so they are the parts left out. The
 * events come already framed, from ab/midiprep.py.
 *
 * It is therefore WEAKER EVIDENCE THAN smf2wav, and the two are not
 * interchangeable. smf2wav shares only the library with us. This shares the
 * library, the engine wrapper (port/host/engine_mt32emu.cpp or emu/'s
 * fake-ROM fixture) and the process. When ROMs exist, believe smf2wav.
 *
 * Scheduling is sample-exact by construction: rendering stops at the frame an
 * event is due on, so no event can land in the wrong block. A real-time
 * device cannot do that and is not meant to.
 *
 *   ab_ref --events F.evt --wav OUT.wav [--frames N] [--rate HZ]
 *          [--engine fake|mt32emu|mt32emu-fakerom] [--roms DIR]
 *          [--control-rom P] [--pcm-rom P] [--machine mt32|cm32l]
 *          [--partials N] [--no-reverb]
 *
 * SPDX-License-Identifier: 0BSD
 */

#include "mtp_engine.h"
#include "mtp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#ifdef MTP_WITH_FAKEROM
extern const mtp_engine_vtable mtp_engine_mt32emu_fakerom;
#endif
}

namespace {

struct Event {
    uint64_t  t_us;
    uint32_t  len;
    uint8_t  *bytes;
};

Event   *g_ev;
uint32_t g_ev_len, g_ev_cap;

int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "<microseconds> <hex>", '#' starts a comment line. ab/midiprep.py writes it. */
int load_events(const char *path)
{
    char line[262144];
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "ab_ref: cannot open %s\n", path); return -1; }

    while (fgets(line, (int)sizeof(line), f)) {
        char *p = line;
        uint64_t t;
        uint32_t n = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        t = strtoull(p, &p, 10);
        while (*p == ' ' || *p == '\t') p++;

        {
            char *h = p;
            uint32_t chars = 0;
            uint8_t *buf;
            while (hexval((unsigned char)*h) >= 0) { h++; chars++; }
            if (chars < 2 || (chars & 1u)) {
                fprintf(stderr, "ab_ref: bad hex in %s\n", path);
                fclose(f);
                return -1;
            }
            n = chars / 2u;
            buf = (uint8_t *)malloc(n);
            if (!buf) { fclose(f); return -1; }
            for (uint32_t i = 0; i < n; i++)
                buf[i] = (uint8_t)((hexval((unsigned char)p[2 * i]) << 4)
                                   | hexval((unsigned char)p[2 * i + 1]));
            if (g_ev_len == g_ev_cap) {
                uint32_t cap = g_ev_cap ? g_ev_cap * 2u : 4096u;
                Event *e = (Event *)realloc(g_ev, (size_t)cap * sizeof(Event));
                if (!e) { free(buf); fclose(f); return -1; }
                g_ev = e; g_ev_cap = cap;
            }
            g_ev[g_ev_len].t_us  = t;
            g_ev[g_ev_len].len   = n;
            g_ev[g_ev_len].bytes = buf;
            g_ev_len++;
        }
    }
    fclose(f);
    return 0;
}

void put32(FILE *f, uint32_t v)
{
    fputc((int)(v & 0xffu), f);        fputc((int)((v >> 8) & 0xffu), f);
    fputc((int)((v >> 16) & 0xffu), f); fputc((int)((v >> 24) & 0xffu), f);
}
void put16(FILE *f, uint16_t v)
{
    fputc((int)(v & 0xffu), f); fputc((int)((v >> 8) & 0xffu), f);
}

/* 16-bit stereo PCM, the same shape port/host/host_audio_wav.c writes. */
int write_wav(const char *path, const int16_t *pcm, uint32_t frames, uint32_t rate)
{
    uint32_t bytes = frames * 4u;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite("RIFF", 1, 4, f);  put32(f, 36u + bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);  put32(f, 16u);
    put16(f, 1u); put16(f, 2u);
    put32(f, rate); put32(f, rate * 4u);
    put16(f, 4u); put16(f, 16u);
    fwrite("data", 1, 4, f);  put32(f, bytes);
    fwrite(pcm, 1, bytes, f);
    fclose(f);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    const char *events = NULL, *wav = NULL, *roms = NULL;
    const char *ctrl = NULL, *pcm_rom = NULL, *machine = "mt32";
    const char *engname = "auto";
    uint32_t rate = 48000u, frames = 0u, partials = 32u;
    int reverb = 1;
    char ctrl_buf[1024], pcm_buf[1024];

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--events") && i + 1 < argc)      events = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc)    wav = argv[++i];
        else if (!strcmp(argv[i], "--roms") && i + 1 < argc)   roms = argv[++i];
        else if (!strcmp(argv[i], "--control-rom") && i+1<argc) ctrl = argv[++i];
        else if (!strcmp(argv[i], "--pcm-rom") && i + 1 < argc) pcm_rom = argv[++i];
        else if (!strcmp(argv[i], "--machine") && i + 1 < argc) machine = argv[++i];
        else if (!strcmp(argv[i], "--engine") && i + 1 < argc)  engname = argv[++i];
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc)    rate = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)  frames = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--partials") && i + 1 < argc) partials = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-reverb"))               reverb = 0;
        else { fprintf(stderr, "ab_ref: unknown option %s\n", argv[i]); return 2; }
    }
    if (!events || !wav) {
        fprintf(stderr, "usage: ab_ref --events F.evt --wav OUT.wav "
                        "[--frames N] [--engine NAME] [--roms DIR]\n");
        return 2;
    }

    if (roms && !ctrl) {
        const char *c = strcmp(machine, "cm32l") ? "MT32_CONTROL.ROM"
                                                 : "CM32L_CONTROL.ROM";
        const char *p = strcmp(machine, "cm32l") ? "MT32_PCM.ROM"
                                                 : "CM32L_PCM.ROM";
        snprintf(ctrl_buf, sizeof(ctrl_buf), "%s/%s", roms, c);
        snprintf(pcm_buf,  sizeof(pcm_buf),  "%s/%s", roms, p);
        ctrl = ctrl_buf; pcm_rom = pcm_buf;
    }

    /* Engine choice, in the same words desktop/src/main.c uses. */
    const mtp_engine_vtable *eng = NULL;
    if (!strcmp(engname, "fake")) eng = &mtp_engine_fake;
#ifdef MTP_WITH_MT32EMU
    else if (!strcmp(engname, "mt32emu")) eng = &mtp_engine_mt32emu;
#endif
#ifdef MTP_WITH_FAKEROM
    else if (!strcmp(engname, "mt32emu-fakerom")) eng = &mtp_engine_mt32emu_fakerom;
#endif
    else if (!strcmp(engname, "auto")) {
#ifdef MTP_WITH_MT32EMU
        if (ctrl) eng = &mtp_engine_mt32emu;
#endif
#ifdef MTP_WITH_FAKEROM
        if (!eng) eng = &mtp_engine_mt32emu_fakerom;
#endif
        if (!eng) eng = &mtp_engine_fake;
    }
    if (!eng) {
        fprintf(stderr, "ab_ref: no engine called '%s' in this build\n", engname);
        return 2;
    }

    if (load_events(events) != 0) return 1;

    mtp_engine_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.control_rom_path = ctrl;
    cfg.pcm_rom_path     = pcm_rom;
    cfg.output_rate      = rate;
    cfg.max_partials     = partials;
    cfg.reverb_enabled   = reverb;

    mtp_engine *inst = NULL;
    if (eng->open(&cfg, &inst) != MTP_OK) {
        fprintf(stderr, "ab_ref: engine '%s' would not open\n", eng->name);
        return 1;
    }

    if (frames == 0u) {
        uint64_t span = g_ev_len ? g_ev[g_ev_len - 1].t_us : 0;
        frames = (uint32_t)((span * rate) / 1000000u) + 3u * rate;   /* + 3 s */
    }

    int16_t *pcm = (int16_t *)calloc((size_t)frames * 2u, sizeof(int16_t));
    if (!pcm) { fprintf(stderr, "ab_ref: out of memory\n"); return 1; }

    uint32_t at = 0u, next = 0u, backpressure = 0u, shorts = 0u, sysexes = 0u;
    while (at < frames) {
        /* How far may we render before the next event is due? */
        uint32_t upto = frames;
        if (next < g_ev_len) {
            uint64_t f = (g_ev[next].t_us * (uint64_t)rate) / 1000000u;
            upto = f > (uint64_t)frames ? frames : (uint32_t)f;
        }
        if (upto > at) {
            uint32_t n = upto - at;
            /* One call per 4096 frames: mt32emu renders in chunks internally
             * anyway, and this keeps the stack and the cache behaviour close
             * to the real loop's without letting block size change the
             * result -- it cannot, since no event lands inside a chunk. */
            while (n) {
                uint32_t chunk = n > 4096u ? 4096u : n;
                eng->render(inst, pcm + (size_t)at * 2u, chunk);
                at += chunk;
                n -= chunk;
            }
            continue;
        }
        if (next >= g_ev_len) continue;

        {
            const Event *e = &g_ev[next];
            uint32_t ts = eng->rendered_samples(inst);
            mtp_status st;
            if (e->bytes[0] == 0xF0u) {
                st = eng->sysex(inst, e->bytes, e->len, ts);
                sysexes++;
            } else {
                uint32_t msg = e->bytes[0];
                if (e->len > 1u) msg |= (uint32_t)e->bytes[1] << 8;
                if (e->len > 2u) msg |= (uint32_t)e->bytes[2] << 16;
                st = eng->short_msg(inst, msg, ts);
                shorts++;
            }
            if (st != MTP_OK) {
                /* The engine's queue is full. Render one millisecond and
                 * offer it again -- the same hold-and-retry the render loop
                 * does, and the reason the counter is printed. */
                backpressure++;
                uint32_t chunk = rate / 1000u;
                if (at + chunk > frames) break;
                eng->render(inst, pcm + (size_t)at * 2u, chunk);
                at += chunk;
                continue;
            }
            next++;
        }
    }

    if (write_wav(wav, pcm, frames, rate) != 0) {
        fprintf(stderr, "ab_ref: cannot write %s\n", wav);
        return 1;
    }

    /* The same words compare.py greps for, so an ab_ref run can sit in the
     * same output directory as a conform.sh run and be read by the same
     * tooling. The counters that are not ours to have are not printed. */
    printf("engine                  %s\n", eng->name);
    printf("short messages          %u\n", (unsigned)shorts);
    printf("sysex messages          %u\n", (unsigned)sysexes);
    printf("engine back-pressure    %u\n", (unsigned)backpressure);
    printf("frames rendered         %u (%.3f s at %u Hz)\n",
           (unsigned)frames, (double)frames / (double)rate, (unsigned)rate);
    printf("events played           %u of %u\n",
           (unsigned)next, (unsigned)g_ev_len);
    if (next < g_ev_len)
        printf("NOTE: %u event(s) fell past the end of the render window\n",
               (unsigned)(g_ev_len - next));

    eng->close(inst);
    free(pcm);
    return 0;
}
