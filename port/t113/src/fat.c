/* fat.c - read-only FAT16 and FAT32, short names only, one open file's worth
 * of state.
 *
 * SCOPE, AGAIN DELIBERATELY SMALL. Three files are read at boot and nothing
 * else ever touches the card (port/DESIGN.md 4.1). So: no writing, no long
 * file names, no directory enumeration beyond walking a path, no FAT12, no
 * timestamps. port/DESIGN.md 4.3 does ask for one thing this cannot yet do --
 * "list what *is* in /roms" when a ROM is missing -- and that is noted in
 * port/T113.md 8 as the one place the interface will need to grow.
 *
 * LAYOUT FACTS. The on-disk structures are the FAT specification's, and the
 * field offsets used below were checked against U-Boot's include/fat.h --
 * struct boot_sector (:86-110) and struct dir_entry (:133-144) -- rather than
 * written from memory. Offsets are open-coded as byte reads instead of
 * declared as a packed struct, because a packed struct over a buffer is a
 * source of alignment faults on ARM and this port runs with SCTLR.A off,
 * where the failure is silent rather than loud.
 *
 * MBR. A card straight from a camera or from the SD Association's formatter
 * has an MBR with one FAT partition; a card formatted by `mkfs.vfat /dev/sdX`
 * by hand often has no MBR at all. Both are common enough that refusing
 * either would be a support burden, so we look at sector 0, decide which it
 * is by the boot-sector signature and a sanity check on bytes-per-sector, and
 * say which we found.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "t113.h"
#include "mtp_log.h"

#define SECTOR 512u

static uint8_t  g_sec[SECTOR] __attribute__((aligned(8)));
static uint8_t  g_fat[SECTOR] __attribute__((aligned(8)));
static uint32_t g_fat_cached_sector = 0xFFFFFFFFu;

static struct {
    int      mounted;
    int      is_fat32;
    uint32_t part_lba;          /* first sector of the volume              */
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved;
    uint32_t num_fats;
    uint32_t fat_sectors;
    uint32_t root_entries;      /* FAT16 only                              */
    uint32_t root_cluster;      /* FAT32 only                              */
    uint32_t fat_lba;
    uint32_t root_lba;          /* FAT16 only                              */
    uint32_t data_lba;
    uint32_t total_clusters;
} v;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int read_sector(uint32_t lba, void *dst)
{
    return t113_smhc_read_blocks(lba, dst, 1u);
}

/* Parse a boot sector already in g_sec. Returns 0 if it looks like one. */
static int parse_bpb(uint32_t lba)
{
    uint32_t total_sectors, root_sectors, data_sectors;

    if (rd16(g_sec + 510) != 0xAA55u) return -1;
    v.bytes_per_sector    = rd16(g_sec + 11);   /* fat.h boot_sector
                                                 * .sector_size[2]         */
    v.sectors_per_cluster = g_sec[13];          /* .cluster_size           */
    v.reserved            = rd16(g_sec + 14);   /* .reserved               */
    v.num_fats            = g_sec[16];          /* .fats                   */
    v.root_entries        = rd16(g_sec + 17);   /* .dir_entries[2]         */
    total_sectors         = rd16(g_sec + 19);   /* .sectors[2]             */
    v.fat_sectors         = rd16(g_sec + 22);   /* .fat_length             */
    if (total_sectors == 0u) total_sectors = rd32(g_sec + 32); /* .total_sect */
    if (v.fat_sectors == 0u) v.fat_sectors = rd32(g_sec + 36); /* .fat32_length */
    v.root_cluster        = rd32(g_sec + 44);   /* .root_cluster           */

    if (v.bytes_per_sector != SECTOR) return -1;
    if (v.sectors_per_cluster == 0u || v.num_fats == 0u ||
        v.fat_sectors == 0u || v.reserved == 0u) return -1;

    v.part_lba = lba;
    v.fat_lba  = lba + v.reserved;
    root_sectors = ((v.root_entries * 32u) + SECTOR - 1u) / SECTOR;
    v.root_lba = v.fat_lba + v.num_fats * v.fat_sectors;
    v.data_lba = v.root_lba + root_sectors;
    data_sectors = total_sectors - (v.data_lba - lba);
    v.total_clusters = data_sectors / v.sectors_per_cluster;

    /* The cluster count is the *only* thing that decides FAT16 vs FAT32 --
     * not the "FAT32" string in the volume label area, which is advisory and
     * is wrong on plenty of real cards. Microsoft's own threshold is 4085 and
     * 65525 clusters. */
    v.is_fat32 = (v.total_clusters >= 65525u);
    return 0;
}

int fat_mount(void)
{
    unsigned p;

    memset(&v, 0, sizeof v);
    g_fat_cached_sector = 0xFFFFFFFFu;

    if (read_sector(0u, g_sec)) {
        MTP_LOGE("fat: cannot read sector 0");
        return -1;
    }

    /* Superfloppy first: a boot sector has a sane bytes-per-sector where an
     * MBR has partition table bytes there. */
    if (rd16(g_sec + 11) == SECTOR && parse_bpb(0u) == 0) {
        MTP_LOGI("fat: no partition table, volume starts at sector 0");
    } else {
        uint32_t start = 0u;
        if (rd16(g_sec + 510) != 0xAA55u) {
            MTP_LOGE("fat: sector 0 has no 0xAA55 signature; not a "
                     "formatted card");
            return -1;
        }
        for (p = 0; p < 4u; p++) {
            const uint8_t *e = g_sec + 446u + p * 16u;
            uint8_t type = e[4];
            if (type == 0x0Bu || type == 0x0Cu ||    /* FAT32, FAT32 LBA   */
                type == 0x06u || type == 0x0Eu ||    /* FAT16, FAT16 LBA   */
                type == 0x04u) {                     /* small FAT16        */
                start = rd32(e + 8);
                break;
            }
        }
        if (start == 0u) {
            MTP_LOGE("fat: no FAT partition in the MBR");
            return -1;
        }
        if (read_sector(start, g_sec) || parse_bpb(start)) {
            MTP_LOGE("fat: partition at LBA %u is not a FAT volume",
                     (unsigned)start);
            return -1;
        }
        MTP_LOGI("fat: partition %u starts at sector %u", p, (unsigned)start);
    }

    v.mounted = 1;
    MTP_LOGI("fat: FAT%d, %u clusters of %u B, FAT at %u, data at %u",
             v.is_fat32 ? 32 : 16, (unsigned)v.total_clusters,
             (unsigned)(v.sectors_per_cluster * SECTOR),
             (unsigned)v.fat_lba, (unsigned)v.data_lba);
    return 0;
}

static uint32_t cluster_lba(uint32_t cluster)
{
    return v.data_lba + (cluster - 2u) * v.sectors_per_cluster;
}

/* One FAT sector is cached, which turns a linear walk of a 1 MB file into
 * one FAT read per 128 clusters instead of one per cluster. */
static uint32_t fat_next(uint32_t cluster)
{
    uint32_t off = v.is_fat32 ? cluster * 4u : cluster * 2u;
    uint32_t sec = v.fat_lba + off / SECTOR;
    uint32_t idx = off % SECTOR;

    if (sec != g_fat_cached_sector) {
        if (read_sector(sec, g_fat)) return 0x0FFFFFFFu;
        g_fat_cached_sector = sec;
    }
    if (v.is_fat32) return rd32(g_fat + idx) & 0x0FFFFFFFu;
    return rd16(g_fat + idx);
}

static int cluster_is_end(uint32_t c)
{
    if (c < 2u) return 1;
    return v.is_fat32 ? (c >= 0x0FFFFFF8u) : (c >= 0xFFF8u);
}

/* "readme.txt" -> "README  TXT". Returns 0 on success. */
static int to_83(const char *name, char out[11])
{
    unsigned i = 0, j = 0;
    memset(out, ' ', 11);
    while (name[i] && name[i] != '.' && j < 8u) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[j++] = c;
    }
    while (name[i] && name[i] != '.') i++;
    if (name[i] == '.') {
        i++;
        j = 8u;
        while (name[i] && j < 11u) {
            char c = name[i++];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            out[j++] = c;
        }
    }
    return 0;
}

/* Scan one directory (given as a cluster, or as the FAT16 fixed root when
 * `cluster` is 0 and the volume is FAT16) for an 8.3 name. */
static int scan_dir(uint32_t cluster, const char name83[11], int want_dir,
                    fat_dirent *out)
{
    uint32_t sec, sectors_left, c = cluster;

    for (;;) {
        unsigned s;
        if (!v.is_fat32 && cluster == 0u) {
            sec = v.root_lba;
            sectors_left = ((v.root_entries * 32u) + SECTOR - 1u) / SECTOR;
        } else {
            sec = cluster_lba(c);
            sectors_left = v.sectors_per_cluster;
        }

        for (s = 0; s < sectors_left; s++) {
            unsigned e;
            if (read_sector(sec + s, g_sec)) return -1;
            for (e = 0; e < SECTOR / 32u; e++) {
                const uint8_t *d = g_sec + e * 32u;
                if (d[0] == 0x00u) return -1;      /* end of directory     */
                if (d[0] == 0xE5u) continue;       /* deleted              */
                if ((d[11] & 0x0Fu) == 0x0Fu) continue;  /* long-name slot */
                if ((d[11] & 0x08u) != 0u) continue;     /* volume label   */
                if (memcmp(d, name83, 11) != 0) continue;
                if (want_dir && !(d[11] & 0x10u)) return -1;
                if (!want_dir && (d[11] & 0x10u)) return -1;
                /* .starthi at offset 20, .start at offset 26, .size at 28 --
                 * U-Boot include/fat.h:141-143. */
                out->first_cluster = ((uint32_t)rd16(d + 20) << 16) |
                                     rd16(d + 26);
                out->size = rd32(d + 28);
                return 0;
            }
        }

        if (!v.is_fat32 && cluster == 0u) return -1;   /* fixed root ended */
        c = fat_next(c);
        if (cluster_is_end(c)) return -1;
    }
}

int fat_lookup(const char *path, fat_dirent *out)
{
    char name83[11];
    char comp[64];
    uint32_t dir_cluster;
    const char *p = path;

    if (!v.mounted || !path || !out) return -1;
    while (*p == '/') p++;

    dir_cluster = v.is_fat32 ? v.root_cluster : 0u;

    for (;;) {
        const char *slash = p;
        size_t n;
        while (*slash && *slash != '/') slash++;
        n = (size_t)(slash - p);
        if (n == 0u || n >= sizeof comp) return -1;
        memcpy(comp, p, n);
        comp[n] = '\0';
        to_83(comp, name83);

        if (*slash == '\0') return scan_dir(dir_cluster, name83, 0, out);

        {
            fat_dirent d;
            if (scan_dir(dir_cluster, name83, 1, &d)) return -1;
            dir_cluster = d.first_cluster;
            if (dir_cluster < 2u) return -1;
        }
        p = slash + 1;
        while (*p == '/') p++;
    }
}

long fat_read(const fat_dirent *f, uint32_t offset, void *dst, uint32_t len)
{
    uint8_t *out = (uint8_t *)dst;
    uint32_t cluster_bytes, got = 0u, c;
    uint32_t skip_clusters, i;

    if (!v.mounted || !f || !dst) return -1;
    if (offset >= f->size) return 0;
    if (len > f->size - offset) len = f->size - offset;

    cluster_bytes = v.sectors_per_cluster * SECTOR;
    c = f->first_cluster;
    skip_clusters = offset / cluster_bytes;
    for (i = 0; i < skip_clusters; i++) {
        c = fat_next(c);
        if (cluster_is_end(c)) return -1;
    }
    offset %= cluster_bytes;

    while (got < len) {
        uint32_t in_cluster = cluster_bytes - offset;
        uint32_t want = len - got;
        uint32_t lba, first_sec, sec_off;

        if (cluster_is_end(c)) break;
        if (want > in_cluster) want = in_cluster;

        first_sec = offset / SECTOR;
        sec_off   = offset % SECTOR;
        lba = cluster_lba(c) + first_sec;

        if (sec_off == 0u && want >= SECTOR && ((uintptr_t)(out + got) & 3u) == 0u) {
            /* Whole sectors straight into the caller's buffer, as many as
             * this cluster holds: the only path that matters for a 512 KB
             * PCM ROM. One CMD18 per cluster. */
            uint32_t nsec = want / SECTOR;
            if (nsec > v.sectors_per_cluster - first_sec)
                nsec = v.sectors_per_cluster - first_sec;
            if (t113_smhc_read_blocks(lba, out + got, nsec)) return -1;
            got += nsec * SECTOR;
            offset += nsec * SECTOR;
        } else {
            uint32_t n = SECTOR - sec_off;
            if (n > want) n = want;
            if (read_sector(lba, g_sec)) return -1;
            memcpy(out + got, g_sec + sec_off, n);
            got += n;
            offset += n;
        }

        if (offset >= cluster_bytes) {
            offset = 0u;
            c = fat_next(c);
        }
    }
    return (long)got;
}
