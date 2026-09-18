/* retarget.c - the libc surface, bare metal, plus the uncached allocator.
 *
 * Adapted from emu/src/retarget.c (0BSD, this repository). The list of what
 * must exist is port/PORTING.md 4.1's, unchanged; what differs on a board is
 * the bottom of the file -- there is no semihosting and nowhere to exit to --
 * and the addition of the .dma bump allocator that emu/ keeps in its own
 * src/dma.c.
 *
 * WHY A BUMP ALLOCATOR. port/PORTING.md 4.3: all 285 of mt32emu's allocations
 * happen inside Synth::open(), none during rendering, and the only thing that
 * frees them is Synth::close(), which on this device means "reboot". free()
 * is therefore a no-op except for the one case that is free and useful --
 * releasing the most recent allocation, which makes the ROM staging buffers
 * cheap to hand back.
 *
 * The consequence is stated rather than hidden: an open/close/open cycle leaks
 * the whole arena. main() prints the heap high-water mark, which is the number
 * port/PORTING.md 7 asks to be logged.
 *
 * SPDX-License-Identifier: 0BSD
 */

#include <stddef.h>
#include <stdint.h>
#include "t113.h"

int printf(const char *fmt, ...);

extern char __heap_start[], __heap_end[];

static char    *g_brk;
static uint32_t g_high_water;
static void    *g_last_block;
static size_t   g_last_size;

/* ------------------------------------------------------------ _sbrk ----- */
void *_sbrk(intptr_t incr)
{
    char *prev;
    if (g_brk == NULL) g_brk = __heap_start;
    prev = g_brk;
    if (incr > 0 && (size_t)(__heap_end - g_brk) < (size_t)incr)
        return (void *)-1;              /* caller turns this into MTP_ERR_NOMEM */
    g_brk += incr;
    if ((uint32_t)(g_brk - __heap_start) > g_high_water)
        g_high_water = (uint32_t)(g_brk - __heap_start);
    return prev;
}

uint32_t t113_heap_used(void)       { return (uint32_t)((g_brk ? g_brk : __heap_start) - __heap_start); }
uint32_t t113_heap_size(void)       { return (uint32_t)(__heap_end - __heap_start); }
uint32_t t113_heap_high_water(void) { return g_high_water; }

/* ------------------------------------------------------- the allocator -- */
void *malloc(size_t n)
{
    void *p;
    if (n == 0u) n = 1u;
    n = (n + 7u) & ~(size_t)7u;         /* 8-byte alignment: doubles, NEON  */
    p = _sbrk((intptr_t)n);
    if (p == (void *)-1) return NULL;
    g_last_block = p;
    g_last_size  = n;
    return p;
}

void free(void *p)
{
    /* The one case worth handling: the most recent block. It makes the 1 MB
     * PCM ROM staging buffer in the mt32emu binding actually cost nothing if
     * the caller frees it straight back. */
    if (p != NULL && p == g_last_block) {
        (void)_sbrk(-(intptr_t)g_last_size);
        g_last_block = NULL;
        g_last_size  = 0u;
    }
}

void *calloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    void *p;
    if (n != 0u && total / n != sz) return NULL;   /* overflow */
    p = malloc(total);
    if (p) {
        char *d = (char *)p;
        while (total--) *d++ = 0;
    }
    return p;
}

void *realloc(void *old, size_t n)
{
    void *p = malloc(n);
    if (p && old) {
        /* We do not know the old size; copying n bytes is safe only because
         * the heap is one contiguous arena and old+n stays inside it. */
        const char *s = (const char *)old;
        char *d = (char *)p;
        size_t i, avail = (size_t)(__heap_end - (char *)old);
        size_t k = n < avail ? n : avail;
        for (i = 0; i < k; i++) d[i] = s[i];
    }
    return p;
}

/* ------------------------------------------------ stdio and stdlib odds -- */
/* mt32emu's default ReportHandler writes to stdout (Synth.cpp:391-398). We
 * override it with our own ReportHandler, but the default implementations are
 * compiled in regardless -- port/PORTING.md 4.7 predicted exactly this -- so
 * the symbol has to exist. It is never dereferenced: our vfprintf ignores the
 * stream and writes to the console. */
struct _IO_FILE;
struct _IO_FILE *stdout;
struct _IO_FILE *stderr;
struct _IO_FILE *stdin;

/* port/PORTING.md 4.1 lists div (Display.cpp:227). abs comes from TVP.cpp:88,
 * which that table missed -- one symbol more than the twenty it counted. */
typedef struct { int quot; int rem; } t113_div_t;
t113_div_t div(int num, int den)
{
    t113_div_t r;
    r.quot = num / den;
    r.rem  = num % den;
    return r;
}
int  abs(int v)        { return v < 0 ? -v : v; }
long labs(long v)      { return v < 0 ? -v : v; }

/* ------------------------------------------------- string.h essentials -- */
void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    /* word copy when both ends line up; this is the hot one, mt32emu copies
     * the expanded PCM ROM and every sysex through it */
    if (((uintptr_t)dd | (uintptr_t)ss) % 4u == 0u) {
        uint32_t *dw = (uint32_t *)dd;
        const uint32_t *sw = (const uint32_t *)ss;
        while (n >= 4u) { *dw++ = *sw++; n -= 4u; }
        dd = (unsigned char *)dw; ss = (const unsigned char *)sw;
    }
    while (n--) *dd++ = *ss++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    if (dd == ss || n == 0u) return d;
    if (dd < ss) return memcpy(d, s, n);
    dd += n; ss += n;
    while (n--) *--dd = *--ss;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *dd = (unsigned char *)d;
    unsigned char v = (unsigned char)c;
    if ((uintptr_t)dd % 4u == 0u) {
        uint32_t w = (uint32_t)v * 0x01010101u;
        uint32_t *dw = (uint32_t *)dd;
        while (n >= 4u) { *dw++ = w; n -= 4u; }
        dd = (unsigned char *)dw;
    }
    while (n--) *dd++ = v;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    while (n--) { if (*p == (unsigned char)c) return (void *)p; p++; }
    return NULL;
}

size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
size_t strnlen(const char *s, size_t m) { size_t n = 0; while (n < m && s[n]) n++; return n; }
int strcmp(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return (int)(unsigned char)*a - (int)(unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n)
{ while (n && *a && *a == *b) { a++; b++; n--; } return n ? (int)(unsigned char)*a - (int)(unsigned char)*b : 0; }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++) != '\0') {} return r; }
char *strncpy(char *d, const char *s, size_t n)
{ char *r = d; while (n && (*d = *s)) { d++; s++; n--; } while (n--) *d++ = '\0'; return r; }
char *strchr(const char *s, int c)
{ for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return NULL; } }
char *strrchr(const char *s, int c)
{ const char *last = NULL; for (;; s++) { if (*s == (char)c) last = s; if (!*s) break; } return (char *)last; }
char *strcat(char *d, const char *s) { char *r = d; while (*d) d++; while ((*d++ = *s++) != '\0') {} return r; }
char *strstr(const char *h, const char *n)
{
    size_t ln = strlen(n);
    if (!ln) return (char *)h;
    for (; *h; h++) if (!strncmp(h, n, ln)) return (char *)h;
    return NULL;
}

/* ------------------------------------------------------- rand (4.9) ----- */
/* port/PORTING.md section 4.9: TVP.cpp uses rand() on the render path and only
 * needs two low bits that vary. glibc's goes through _REENT and takes a lock;
 * this is a 3-line xorshift and the linker prefers it. */
static uint32_t g_rand_state = 0x1234567u;
int rand(void)
{
    uint32_t x = g_rand_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_rand_state = x;
    return (int)(x & 0x7FFFFFFFu);
}
void srand(unsigned s) { g_rand_state = s ? (uint32_t)s : 1u; }

/* ---------------------------------------------- errno and friends ------- */
/* glibc's libm.a sets errno through __errno_location in the reentrant build
 * and through the plain `errno` object in the static one; both appear in the
 * undefined-symbol dump of this link, so both exist. Nothing reads them. */
/* __thread, not a plain int: glibc's libm.a was compiled against a TLS errno
 * and the link fails outright against a non-TLS one. link.ld carries the
 * matching segment and start.S the thread pointer. */
__thread int errno;
int *__errno_location(void) { return &errno; }

/* libm's error paths call raise(SIGFPE) on some routes. There are no signals
 * here; swallow it, because an FP domain error in mt32emu's tables is a bug to
 * find with a debugger, not a reason to stop the audio. */
int raise(int sig) { (void)sig; return 0; }

/* glibc's libm.a is built with the stack protector on some configurations. */
uintptr_t __stack_chk_guard = 0xDEADBEEFu;
void __stack_chk_fail(void)
{
    printf("\n*** stack smashing detected ***\n");
    t113_exit(70);
}

/* ------------------------------------------------------------- exit ----- */
/* There is no semihosting here and there is nowhere to exit to. emu/ ends a
 * run with SYS_EXIT_EXTENDED so that `make test` can assert on the status; a
 * board has no host to report to, so exit() flushes the console, says why, and
 * parks the core in WFI with interrupts still enabled -- which means the DMA
 * keeps playing whatever the ring last held rather than the data line freezing
 * at a DC level. port/DESIGN.md 4.3: "a module that is quiet and talkative
 * over serial is debuggable; a module that is bricked is not."
 *
 * The watchdog at 0x020500a0 (linux sunxi-d1s-t113.dtsi:313-317) is the way to
 * turn this into a reboot instead, and is deliberately not used: an automatic
 * reboot loop on a device with no display is how a fault becomes invisible. */
void t113_exit(int code)
{
    printf("\n*** halted, code %d. Interrupts stay on; the audio ring keeps "
           "playing. ***\n", code);
    t113_uart_console_flush();
    for (;;) t113_wfi();
}

void t113_fatal_exception(uint32_t which, uint32_t pc)
{
    static const char *const names[] = {
        "?", "undefined instruction", "supervisor call", "prefetch abort",
        "data abort", "hyp trap", "FIQ"
    };
    uint32_t dfsr = 0, dfar = 0, ifsr = 0, ifar = 0;
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(ifsr));
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));

    /* A bare-metal image that spins on a data abort tells you nothing; one
     * that names the fault, the PC and the faulting address tells you almost
     * everything. DFSR's status field is the ARM ARM B3.13.3 encoding --
     * 0b00101 translation fault is the one that means "you dereferenced
     * something outside the map src/mmu.c built". */
    printf("\n*** %s at pc 0x%08x ***\n",
           which < sizeof names / sizeof names[0] ? names[which] : "?",
           (unsigned)pc);
    printf("    DFSR 0x%08x DFAR 0x%08x  IFSR 0x%08x IFAR 0x%08x\n",
           (unsigned)dfsr, (unsigned)dfar, (unsigned)ifsr, (unsigned)ifar);
    t113_uart_console_flush();
    for (;;) t113_wfi();
}

void abort(void)
{
    printf("\n*** abort() ***\n");
    t113_exit(134);
}

void exit(int code) { t113_exit(code); }
void _exit(int code) { t113_exit(code); }

void __assert_fail(const char *expr, const char *file, unsigned line,
                   const char *fn)
{
    printf("\n*** assert: %s at %s:%u (%s)\n", expr, file, line,
           fn ? fn : "?");
    t113_exit(134);
}

/* ------------------------------------------------- file descriptor stubs  */
int _write(int fd, const char *buf, int len)
{
    (void)fd;
    t113_uart_console_write(buf, (unsigned)len);
    return len;
}
int write(int fd, const void *buf, size_t n) { return _write(fd, (const char *)buf, (int)n); }
int _read(int fd, char *buf, int len)   { (void)fd; (void)buf; (void)len; return -1; }
int _close(int fd)                      { (void)fd; return -1; }
long _lseek(int fd, long off, int w)    { (void)fd; (void)off; (void)w; return -1; }
int _fstat(int fd, void *st)            { (void)fd; (void)st; return -1; }
int _isatty(int fd)                     { (void)fd; return 1; }
int _getpid(void)                       { return 1; }
int _kill(int pid, int sig)             { (void)pid; (void)sig; return -1; }

/* ---------------------------------------------------- C++ runtime ------- */
/* port/PORTING.md section 4.1: one-line stubs, because the device never exits.
 * -fno-threadsafe-statics removes __cxa_guard_*, so they are absent here on
 * purpose: if a link ever asks for them, the library was built without that
 * flag and the build is wrong. */
void *__dso_handle = (void *)&__dso_handle;
int __cxa_atexit(void (*f)(void *), void *a, void *d) { (void)f; (void)a; (void)d; return 0; }
void __cxa_finalize(void *d) { (void)d; }
void __aeabi_atexit(void *obj, void (*f)(void *), void *d) { (void)obj; (void)f; (void)d; }
void __cxa_pure_virtual(void)
{
    printf("\n*** pure virtual call ***\n");
    t113_exit(134);
}

/* ------------------------------------------- the uncached allocator ----- */
/* Everything a device reads or writes behind the CPU's back comes from here:
 * the audio ring and the DMA descriptor list. The region is link.ld's .dma
 * section, which src/mmu.c retypes Normal Non-cacheable (port/DESIGN.md 2.3).
 *
 * Bump only, never freed, because it is allocated once at mtp_audio_open()
 * and lives until reset -- the same argument as the arena above. This is the
 * allocator that has to be right on real silicon: a descriptor list left in
 * write-back memory is the classic silent failure, and QEMU could never have
 * caught it because TCG models no caches at all. */
extern char __dma_start[], __dma_end[];
static char *g_dma_next;

void *t113_dma_alloc(size_t n, size_t align)
{
    uintptr_t p;
    if (g_dma_next == NULL) g_dma_next = __dma_start;
    if (align < 8u) align = 8u;
    p = ((uintptr_t)g_dma_next + (align - 1u)) & ~(uintptr_t)(align - 1u);
    if (p + n > (uintptr_t)__dma_end) return NULL;
    g_dma_next = (char *)(p + n);
    return (void *)p;
}

uint32_t t113_dma_used(void)
{
    return (uint32_t)((g_dma_next ? g_dma_next : __dma_start) - __dma_start);
}
