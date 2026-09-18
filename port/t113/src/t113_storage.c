/* t113_storage.c - mtp_storage.h over SMHC0 + FAT.
 *
 * Thin by construction. The interface is five calls and one convenience
 * (mtp_storage.h), the medium is read-only, and the whole of it runs before
 * the audio loop starts. Everything below is the FAT layer's problem and
 * everything above it is the ROM loader's.
 *
 * ONE OPEN FILE AT A TIME plus a small table, because port/DESIGN.md 4.1's
 * boot order opens one file, reads it whole and closes it, three times. The
 * table exists only so that a caller holding two handles is a clean error
 * rather than silent corruption.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include <stddef.h>
#include "mtp_storage.h"
#include "mtp_log.h"
#include "t113.h"

struct mtp_file {
    int        used;
    fat_dirent ent;
    uint32_t   pos;
};

#define MAX_OPEN 2
static struct mtp_file g_files[MAX_OPEN];
static int g_mounted;

mtp_status mtp_storage_mount(void)
{
    if (g_mounted) return MTP_OK;          /* mtp_storage.h: idempotent */
    if (t113_smhc_init() != 0) return MTP_ERR_IO;
    if (fat_mount() != 0) return MTP_ERR_IO;
    g_mounted = 1;
    return MTP_OK;
}

void mtp_storage_unmount(void) { g_mounted = 0; }

mtp_status mtp_storage_open(const char *path, mtp_file **out)
{
    int i;
    if (!g_mounted) return MTP_ERR_STATE;
    if (!path || !out) return MTP_ERR_INVAL;

    for (i = 0; i < MAX_OPEN; i++) if (!g_files[i].used) break;
    if (i == MAX_OPEN) {
        MTP_LOGE("storage: %d files already open; this port expects one at a "
                 "time (port/DESIGN.md 4.1)", MAX_OPEN);
        return MTP_ERR_NOMEM;
    }
    if (fat_lookup(path, &g_files[i].ent) != 0) return MTP_ERR_NOENT;
    g_files[i].used = 1;
    g_files[i].pos = 0u;
    *out = &g_files[i];
    return MTP_OK;
}

void mtp_storage_close(mtp_file *f) { if (f) f->used = 0; }

long mtp_storage_size(mtp_file *f)
{ return f && f->used ? (long)f->ent.size : -1; }

long mtp_storage_read(mtp_file *f, void *dst, long n)
{
    long got;
    if (!f || !f->used || !dst || n < 0) return -1;
    if (f->pos >= f->ent.size) return 0;
    got = fat_read(&f->ent, f->pos, dst, (uint32_t)n);
    if (got > 0) f->pos += (uint32_t)got;
    return got;
}

int mtp_storage_exists(const char *path)
{
    fat_dirent e;
    if (!g_mounted || !path) return 0;
    return fat_lookup(path, &e) == 0;
}

mtp_status mtp_storage_load(const char *path, void *dst, long cap,
                            long *out_len)
{
    mtp_file *f;
    mtp_status st;
    long size, got;

    if (!dst || cap <= 0) return MTP_ERR_INVAL;
    st = mtp_storage_open(path, &f);
    if (st != MTP_OK) return st;

    size = mtp_storage_size(f);
    if (size < 0) { mtp_storage_close(f); return MTP_ERR_IO; }
    if (size > cap) {
        /* mtp_storage.h: "which for a ROM means the wrong file, and the
         * caller should say so rather than truncate". Name both numbers, so
         * the log says which file was wrong and by how much. */
        MTP_LOGE("storage: %s is %ld bytes, buffer is %ld", path, size, cap);
        mtp_storage_close(f);
        return MTP_ERR_NOMEM;
    }

    got = mtp_storage_read(f, dst, size);
    mtp_storage_close(f);
    if (got != size) return MTP_ERR_IO;
    if (out_len) *out_len = got;
    return MTP_OK;
}
