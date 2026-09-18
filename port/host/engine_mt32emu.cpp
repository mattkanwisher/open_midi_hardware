/* engine_mt32emu.cpp - the real engine behind mtp_engine.
 *
 * This file is the *entire* C++ surface of the port. Everything it does is
 * justified in PORTING.md; the short version:
 *
 *   - ROMs are loaded through mtp_storage into buffers we own and handed to
 *     mt32emu as ArrayFile (mt32emu/File.h:57). We never use FileStream,
 *     which is the only thing in the library that includes <fstream>
 *     (mt32emu/FileStream.h:94) and would drag the whole iostream/locale
 *     machinery into a bare-metal link.
 *   - AnalogOutputMode_ACCURATE, because its output rate is exactly 48000 Hz
 *     (Synth.cpp:294-298) so no resampler is needed at all.
 *   - RendererType_BIT16S, documented as "Maximum emulation accuracy and
 *     speed" (Enumerations.h:157), and matching render(Bit16s*) so that no
 *     format-conversion stack buffer is used (Synth.cpp:2364).
 *   - preallocateReverbMemory(true) and configureMIDIEventQueueSysexStorage(),
 *     which together make the render path allocation-free. Measured, not
 *     assumed: see PORTING.md, "Does it allocate while rendering?".
 *   - A ReportHandler that routes printDebug into mtp_log instead of stdout.
 *   - Half images: a 32 kB control ROM dump is one half of a FirstHalf/
 *     SecondHalf pair (ROMInfo.h:37-48) and is useless alone. If the config
 *     supplies a second path, both halves are loaded and merged with
 *     ROMImage::makeROMImage(File*, File*) (ROMInfo.h:108). DESIGN.md 4.3.
 *
 * SPDX-License-Identifier: 0BSD */

#include "mtp_engine.h"
#include "mtp_storage.h"
#include "mtp_log.h"

#include <mt32emu/mt32emu.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

class PortReportHandler : public MT32Emu::ReportHandler {
public:
    void printDebug(const char *fmt, va_list list) {
        char buf[256];
        vsnprintf(buf, sizeof(buf), fmt, list);
        mtp_log(MTP_LOG_DEBUG, "mt32emu: %s", buf);
    }
    void showLCDMessage(const char *msg) { mtp_log(MTP_LOG_INFO, "LCD: %s", msg); }
    void onErrorControlROM() { MTP_LOGE("mt32emu: bad control ROM"); }
    void onErrorPCMROM()     { MTP_LOGE("mt32emu: bad PCM ROM"); }
    bool onMIDIQueueOverflow() {
        /* false = do not retry; playMsg/playSysex return false and our render
         * loop applies back-pressure instead. Returning true here would spin
         * inside the library, on the render thread, for ever. */
        MTP_LOGW("mt32emu: MIDI queue overflow");
        return false;
    }
};

} // namespace

struct mtp_engine {
    MT32Emu::Synth      *synth;
    PortReportHandler   *handler;
    MT32Emu::ArrayFile  *ctrl_file;
    MT32Emu::ArrayFile  *pcm_file;
    MT32Emu::ArrayFile  *ctrl_file2;   /* second half of a split image, or 0 */
    MT32Emu::ArrayFile  *pcm_file2;
    const MT32Emu::ROMImage *ctrl_img;
    const MT32Emu::ROMImage *pcm_img;
    uint8_t             *ctrl_data;
    uint8_t             *pcm_data;
    uint8_t             *ctrl_data2;
    uint8_t             *pcm_data2;
};

/* Largest images mt32emu knows: 128 kB control (MT-32 v2.x), 1 MB PCM
 * (CM-32L). ROMInfo.cpp:103-117. */
static const long CTRL_CAP = 128L * 1024L;
static const long PCM_CAP  = 1024L * 1024L;

/* DESIGN.md 4.3: "no such file" without the path is a bug report waiting to
 * happen, and a bare path is not much better -- the user wants to know what IS
 * on the card. There is no directory enumeration behind the seam and there is
 * deliberately not going to be one (mtp_storage.h), so we probe the names the
 * design actually defines with mtp_storage_exists(), which is already there.
 * The answer a user needs is "MT32_CONTROL.ROM is missing but CM32L_*.ROM are
 * present", and this produces exactly that without a readdir. */
static const char *const KNOWN_ROMS[] = {
    "roms/MT32_CONTROL.ROM",  "roms/MT32_PCM.ROM",
    "roms/CM32L_CONTROL.ROM", "roms/CM32L_PCM.ROM",
    0
};

static void report_what_is_there(void)
{
    int any = 0;
    for (int i = 0; KNOWN_ROMS[i]; i++) {
        if (mtp_storage_exists(KNOWN_ROMS[i])) {
            MTP_LOGI("  present: %s", KNOWN_ROMS[i]);
            any = 1;
        }
    }
    if (!any)
        MTP_LOGI("  none of the ROM paths in DESIGN.md 4.1 exist on this "
                 "volume");
}

static mtp_status load_rom(const char *path, uint8_t **out, long *out_len,
                           long cap, const char *what)
{
    long len = 0;
    uint8_t *buf;
    mtp_status s;
    if (!path) { MTP_LOGE("no %s ROM path given", what); return MTP_ERR_INVAL; }
    buf = (uint8_t *)malloc((size_t)cap);
    if (!buf) return MTP_ERR_NOMEM;
    s = mtp_storage_load(path, buf, cap, &len);
    if (s != MTP_OK) {
        free(buf);
        MTP_LOGE("%s ROM '%s': %s", what, path, mtp_strerror(s));
        if (s == MTP_ERR_NOENT) report_what_is_there();
        return s;
    }
    MTP_LOGI("%s ROM '%s': %ld bytes", what, path, len);
    *out = buf; *out_len = len;
    return MTP_OK;
}

/* One ROM, whole or split. Returns the ROMImage, or NULL with the reason
 * logged. The two-argument makeROMImage is what merges FirstHalf/SecondHalf
 * and Mux0/Mux1 pairs (ROMInfo.h:108). */
static const MT32Emu::ROMImage *make_image(const char *path, const char *path2,
                                           uint8_t **data, uint8_t **data2,
                                           MT32Emu::ArrayFile **file,
                                           MT32Emu::ArrayFile **file2,
                                           long cap, const char *what)
{
    long len = 0, len2 = 0;
    if (load_rom(path, data, &len, cap, what) != MTP_OK) return 0;
    *file = new MT32Emu::ArrayFile(*data, (size_t)len);
    if (!path2) return MT32Emu::ROMImage::makeROMImage(*file);

    if (load_rom(path2, data2, &len2, cap, what) != MTP_OK) return 0;
    *file2 = new MT32Emu::ArrayFile(*data2, (size_t)len2);
    MTP_LOGI("%s ROM: merging two half images (%ld + %ld bytes)",
             what, len, len2);
    return MT32Emu::ROMImage::makeROMImage(*file, *file2);
}

static mtp_status me_open(const mtp_engine_config *cfg, mtp_engine **out)
{
    mtp_engine *e = (mtp_engine *)calloc(1, sizeof(mtp_engine));
    long ctrl_len = 0, pcm_len = 0;
    if (!e) return MTP_ERR_NOMEM;

    /* makeROMImage identifies the image by size + SHA1 (ROMInfo.h:52-54). A
     * NULL result means "this is not a ROM mt32emu recognises", which is a far
     * more useful error than a synth that opens and plays nothing. */
    e->ctrl_img = make_image(cfg->control_rom_path, cfg->control_rom_path2,
                             &e->ctrl_data, &e->ctrl_data2,
                             &e->ctrl_file, &e->ctrl_file2,
                             CTRL_CAP, "control");
    e->pcm_img  = make_image(cfg->pcm_rom_path, cfg->pcm_rom_path2,
                             &e->pcm_data, &e->pcm_data2,
                             &e->pcm_file, &e->pcm_file2,
                             PCM_CAP, "PCM");
    if (!e->ctrl_img || !e->ctrl_img->getROMInfo()) {
        MTP_LOGE("control ROM not recognised%s",
                 cfg->control_rom_path2
                     ? " (the two halves do not make a known image)"
                     : " -- wrong file, or a half image whose partner was not "
                       "given (see DESIGN.md 4.3)");
        goto fail;
    }
    if (!e->pcm_img || !e->pcm_img->getROMInfo()) {
        MTP_LOGE("PCM ROM not recognised%s",
                 cfg->pcm_rom_path2
                     ? " (the two halves do not make a known image)"
                     : " -- wrong file, or a half image whose partner was not "
                       "given (see DESIGN.md 4.3)");
        goto fail;
    }
    (void)ctrl_len; (void)pcm_len;
    MTP_LOGI("ROMs: %s + %s",
             e->ctrl_img->getROMInfo()->description,
             e->pcm_img->getROMInfo()->description);

    e->handler = new PortReportHandler;
    e->synth   = new MT32Emu::Synth(e->handler);

    e->synth->selectRendererType(MT32Emu::RendererType_BIT16S);
    e->synth->preallocateReverbMemory(true);
    e->synth->configureMIDIEventQueueSysexStorage(64u * 1024u);
    e->synth->setMIDIDelayMode(MT32Emu::MIDIDelayMode_IMMEDIATE);

    if (!e->synth->open(*e->ctrl_img, *e->pcm_img,
                        cfg->max_partials ? cfg->max_partials : 32u,
                        MT32Emu::AnalogOutputMode_ACCURATE)) {
        MTP_LOGE("Synth::open failed");
        goto fail;
    }
    e->synth->setReverbEnabled(cfg->reverb_enabled != 0);

    if (e->synth->getStereoOutputSampleRate() != cfg->output_rate) {
        MTP_LOGW("engine outputs %u Hz, caller asked for %u Hz",
                 e->synth->getStereoOutputSampleRate(), cfg->output_rate);
    }
    MTP_LOGI("engine: mt32emu %s, %u partials, %u Hz out",
             MT32Emu::Synth::getLibraryVersionString(),
             e->synth->getPartialCount(),
             e->synth->getStereoOutputSampleRate());

    *out = e;
    return MTP_OK;

fail:
    if (e->synth) delete e->synth;
    if (e->handler) delete e->handler;
    if (e->ctrl_img) MT32Emu::ROMImage::freeROMImage(e->ctrl_img);
    if (e->pcm_img)  MT32Emu::ROMImage::freeROMImage(e->pcm_img);
    delete e->ctrl_file; delete e->pcm_file;
    delete e->ctrl_file2; delete e->pcm_file2;
    free(e->ctrl_data); free(e->pcm_data);
    free(e->ctrl_data2); free(e->pcm_data2);
    free(e);
    return MTP_ERR_IO;
}

static void me_close(mtp_engine *e)
{
    if (!e) return;
    delete e->synth;
    if (e->ctrl_img) MT32Emu::ROMImage::freeROMImage(e->ctrl_img);
    if (e->pcm_img)  MT32Emu::ROMImage::freeROMImage(e->pcm_img);
    delete e->ctrl_file; delete e->pcm_file;
    delete e->ctrl_file2; delete e->pcm_file2;
    delete e->handler;
    free(e->ctrl_data); free(e->pcm_data);
    free(e->ctrl_data2); free(e->pcm_data2);
    free(e);
}

static uint32_t me_timebase(mtp_engine *)  { return MT32Emu::SAMPLE_RATE; }

static uint32_t me_rendered(mtp_engine *e)
{
    return e->synth->getInternalRenderedSampleCount();
}

static mtp_status me_short(mtp_engine *e, uint32_t msg, uint32_t ts)
{
    return e->synth->playMsg(msg, ts) ? MTP_OK : MTP_ERR_AGAIN;
}

static mtp_status me_sysex(mtp_engine *e, const uint8_t *d, uint32_t len,
                           uint32_t ts)
{
    return e->synth->playSysex(d, len, ts) ? MTP_OK : MTP_ERR_AGAIN;
}

static void me_render(mtp_engine *e, int16_t *stereo, uint32_t frames)
{
    e->synth->render(stereo, frames);
}

static void me_display(mtp_engine *e, char *dst21)
{
    e->synth->getDisplayState(dst21, false);
}

static void me_set_gain(mtp_engine *e, float gain)
{
    /* Synth::setOutputGain (Synth.h:452) applies inside the analogue-circuit
     * emulation, which is where a volume control on an MT-32 actually is. */
    e->synth->setOutputGain(gain);
}

/* Panic. flushMIDIQueue() (Synth.h:398) throws away every event that has been
 * scheduled but not yet applied -- without it, a Note On sitting behind our
 * All Sound Off would restart a note a millisecond after we silenced it. Then
 * All Sound Off (CC 120) and All Notes Off (CC 123) on all sixteen channels,
 * at the synth's current position, which is immediate. */
static void me_panic(mtp_engine *e)
{
    e->synth->flushMIDIQueue();
    for (unsigned ch = 0; ch < 16u; ch++) {
        e->synth->playMsgNow(0xB0u | ch | (120u << 8) | (0u << 16));
        e->synth->playMsgNow(0xB0u | ch | (123u << 8) | (0u << 16));
    }
}

extern "C" const mtp_engine_vtable mtp_engine_mt32emu = {
    "mt32emu",
    me_open,
    me_close,
    me_timebase,
    me_rendered,
    me_short,
    me_sysex,
    me_render,
    me_display,
    me_set_gain,
    me_panic
};
