/* t113_libc.h - prototypes for the libc surface this port supplies itself.
 *
 * src/printf.c and src/retarget.c *define* the C library functions that
 * -nostdlib leaves out and that mt32emu, libm and libgcc then ask for
 * (port/PORTING.md 4.1 has the list). With -Wmissing-prototypes -Werror every
 * one of those definitions needs a declaration in scope, and pulling in the
 * host's <stdio.h> and <string.h> to get them would be the wrong answer twice
 * over: those headers describe glibc's ABI, not ours, and -ffreestanding is
 * there precisely so that they are not consulted.
 *
 * So this file is the declaration half of the retarget. If a signature here
 * ever disagrees with what a library actually calls, the link says so -- which
 * is the behaviour we want, and is why the alternative (switching the warning
 * off for two files) was not taken.
 *
 * SPDX-License-Identifier: 0BSD
 */
#ifndef T113_LIBC_H
#define T113_LIBC_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ---- src/printf.c ------------------------------------------------------ */
int  vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap);
int  snprintf(char *dst, size_t cap, const char *fmt, ...);
int  vsprintf(char *dst, const char *fmt, va_list ap);
int  sprintf(char *dst, const char *fmt, ...);
int  vprintf(const char *fmt, va_list ap);
int  printf(const char *fmt, ...);
int  puts(const char *s);
int  putchar(int c);
int  fputc(int c, void *stream);
int  fputs(const char *s, void *stream);
int  fflush(void *stream);
int  vfprintf(void *stream, const char *fmt, va_list ap);
int  fprintf(void *stream, const char *fmt, ...);
int    fwrite_stub(const void *p, size_t sz, size_t n, void *stream);

/* ---- src/retarget.c: allocator ----------------------------------------- */
void *_sbrk(intptr_t incr);
void *malloc(size_t n);
void  free(void *p);
void *calloc(size_t n, size_t sz);
void *realloc(void *old, size_t n);

/* ---- src/retarget.c: string.h ------------------------------------------ */
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
void  *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t m);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strcat(char *d, const char *s);
char  *strstr(const char *h, const char *n);

/* ---- src/retarget.c: stdlib odds and ends ------------------------------ */
/* div() is port/PORTING.md 4.1's entry for Display.cpp:227; abs() is
 * TVP.cpp:88, which that table missed. */
typedef struct { int quot; int rem; } t113_div_t;
t113_div_t div(int num, int den);
int   abs(int v);
long  labs(long v);
int   rand(void);
void  srand(unsigned s);
int   raise(int sig);
void  abort(void) __attribute__((noreturn));
void  exit(int code) __attribute__((noreturn));
void  _exit(int code) __attribute__((noreturn));
void  __assert_fail(const char *expr, const char *file, unsigned line,
                    const char *fn) __attribute__((noreturn));
int  *__errno_location(void);
void  __stack_chk_fail(void);

/* ---- src/retarget.c: file descriptors ---------------------------------- */
int   _write(int fd, const char *buf, int len);
int   write(int fd, const void *buf, size_t n);
int   _read(int fd, char *buf, int len);
int   _close(int fd);
long  _lseek(int fd, long off, int w);
int   _fstat(int fd, void *st);
int   _isatty(int fd);
int   _getpid(void);
int   _kill(int pid, int sig);

/* ---- src/retarget.c: C++ runtime --------------------------------------- */
int   __cxa_atexit(void (*f)(void *), void *a, void *d);
void  __cxa_finalize(void *d);
void  __aeabi_atexit(void *obj, void (*f)(void *), void *d);
void  __cxa_pure_virtual(void);

#endif /* T113_LIBC_H */
