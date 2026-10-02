/* lib.h - the tiny freestanding C library SkarletOS uses.
 *
 * A kernel cannot use the normal C library (glibc etc.) because that library
 * itself needs an operating system underneath it.  So we write the handful of
 * string helpers we need ourselves.  Everything is prefixed with k_ so the
 * same code can also be compiled on Linux (for the tests) without clashing
 * with the real libc.
 */
#ifndef SKARLET_LIB_H
#define SKARLET_LIB_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int   k_strlen(const char *s);
int   k_strcmp(const char *a, const char *b);
int   k_strncmp(const char *a, const char *b, int n);
int   k_strcasestr(const char *hay, const char *needle); /* 1 if found, case-insensitive */
void  k_strlcpy(char *dst, const char *src, int size);    /* always NUL-terminates */
void  k_strlcat(char *dst, const char *src, int size);
char *k_strchr(const char *s, int c);
int   k_tolower(int c);
int   k_isdigit(int c);
int   k_isspace(int c);
int   k_atoi(const char *s);
void  k_memset(void *dst, int v, int n);
void  k_memcpy(void *dst, const void *src, int n);
void  k_memmove(void *dst, const void *src, int n);

/* printf-style formatting into a buffer. Supports %s %c %d %u %x %% and a
 * width with optional '-' (left align) or '0' (zero pad), e.g. "%-10s". */
int k_vsnprintf(char *buf, int size, const char *fmt, va_list ap);
int k_snprintf(char *buf, int size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Integer calculator used by Skarlet Runner and the shell's "calc" command.
 * Understands + - * / % and parentheses. Returns 0 on success. */
int k_eval(const char *expr, int32_t *out);

/* Small deterministic pseudo random number generator (xorshift). */
uint32_t k_rand(void);
void     k_srand(uint32_t seed);

#endif
