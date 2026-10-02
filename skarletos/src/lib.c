/* lib.c - freestanding string helpers, a mini printf and a calculator. */
#include "lib.h"

int k_strlen(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

int k_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int k_strncmp(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

int k_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int k_isdigit(int c) { return c >= '0' && c <= '9'; }
int k_isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

int k_strcasestr(const char *hay, const char *needle)
{
    int n = k_strlen(needle);
    if (n == 0)
        return 1;
    for (; *hay; hay++) {
        int i = 0;
        while (i < n && hay[i] && k_tolower(hay[i]) == k_tolower(needle[i]))
            i++;
        if (i == n)
            return 1;
    }
    return 0;
}

void k_strlcpy(char *dst, const char *src, int size)
{
    if (size <= 0)
        return;
    int i = 0;
    for (; i < size - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

void k_strlcat(char *dst, const char *src, int size)
{
    int len = k_strlen(dst);
    if (len < size)
        k_strlcpy(dst + len, src, size - len);
}

char *k_strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c == 0 ? (char *)s : 0;
}

int k_atoi(const char *s)
{
    int sign = 1, v = 0;
    while (k_isspace(*s))
        s++;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    while (k_isdigit(*s))
        v = v * 10 + (*s++ - '0');
    return v * sign;
}

void k_memset(void *dst, int v, int n)
{
    unsigned char *d = dst;
    while (n-- > 0)
        *d++ = (unsigned char)v;
}

void k_memcpy(void *dst, const void *src, int n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n-- > 0)
        *d++ = *s++;
}

void k_memmove(void *dst, const void *src, int n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n-- > 0)
            *d++ = *s++;
    } else {
        while (n-- > 0)
            d[n] = s[n];
    }
}

/* ---- printf ------------------------------------------------------------ */

struct out {
    char *buf;
    int size;
    int len;
};

static void out_c(struct out *o, char c)
{
    if (o->len < o->size - 1)
        o->buf[o->len] = c;
    o->len++;
}

static void out_padded(struct out *o, const char *s, int width, int left, char pad)
{
    int n = k_strlen(s);
    if (!left)
        for (int i = n; i < width; i++)
            out_c(o, pad);
    while (*s)
        out_c(o, *s++);
    if (left)
        for (int i = n; i < width; i++)
            out_c(o, ' ');
}

static void utoa(uint64_t v, unsigned base, char *tmp)
{
    char rev[24];
    int n = 0;
    do {
        rev[n++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v);
    for (int i = 0; i < n; i++)
        tmp[i] = rev[n - 1 - i];
    tmp[n] = 0;
}

int k_vsnprintf(char *buf, int size, const char *fmt, va_list ap)
{
    struct out o = { buf, size, 0 };
    char tmp[24];

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out_c(&o, *fmt);
            continue;
        }
        fmt++;
        int left = 0, width = 0;
        char pad = ' ';
        if (*fmt == '-') {
            left = 1;
            fmt++;
        }
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (k_isdigit(*fmt))
            width = width * 10 + (*fmt++ - '0');

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            out_padded(&o, s ? s : "(null)", width, left, ' ');
            break;
        }
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            tmp[1] = 0;
            out_padded(&o, tmp, width, left, ' ');
            break;
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) {
                tmp[0] = '-';
                utoa((uint64_t)(-(int64_t)v), 10, tmp + 1);
            } else {
                utoa((uint64_t)v, 10, tmp);
            }
            out_padded(&o, tmp, width, left, pad);
            break;
        }
        case 'u':
            utoa(va_arg(ap, unsigned), 10, tmp);
            out_padded(&o, tmp, width, left, pad);
            break;
        case 'x':
            utoa(va_arg(ap, unsigned), 16, tmp);
            out_padded(&o, tmp, width, left, pad);
            break;
        case '%':
            out_c(&o, '%');
            break;
        case 0:
            fmt--; /* format ended with a lone '%' */
            break;
        default:
            out_c(&o, '%');
            out_c(&o, *fmt);
        }
    }
    if (size > 0)
        buf[o.len < size ? o.len : size - 1] = 0;
    return o.len;
}

int k_snprintf(char *buf, int size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = k_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* ---- calculator (recursive descent parser) ------------------------------
 *   expr   := term   (('+' | '-') term)*
 *   term   := factor (('*' | '/' | '%') factor)*
 *   factor := number | '-' factor | '(' expr ')'
 * Intermediate values are 64-bit so we can detect 32-bit overflow. */

struct parser {
    const char *p;
    int err;
};

static int64_t p_expr(struct parser *ps);

static void p_skip(struct parser *ps)
{
    while (k_isspace(*ps->p))
        ps->p++;
}

static int64_t p_check(struct parser *ps, int64_t v)
{
    if (v > INT32_MAX || v < INT32_MIN)
        ps->err = 1;
    return ps->err ? 0 : v;
}

static int64_t p_factor(struct parser *ps)
{
    p_skip(ps);
    if (*ps->p == '-') {
        ps->p++;
        return p_check(ps, -p_factor(ps));
    }
    if (*ps->p == '(') {
        ps->p++;
        int64_t v = p_expr(ps);
        p_skip(ps);
        if (*ps->p != ')')
            ps->err = 1;
        else
            ps->p++;
        return v;
    }
    if (!k_isdigit(*ps->p)) {
        ps->err = 1;
        return 0;
    }
    int64_t v = 0;
    while (k_isdigit(*ps->p)) {
        v = v * 10 + (*ps->p++ - '0');
        if (v > INT32_MAX) {
            ps->err = 1;
            return 0;
        }
    }
    return v;
}

static int64_t p_term(struct parser *ps)
{
    int64_t v = p_factor(ps);
    for (;;) {
        p_skip(ps);
        char op = *ps->p;
        if (op != '*' && op != '/' && op != '%')
            return v;
        ps->p++;
        int64_t r = p_factor(ps);
        if (ps->err)
            return 0;
        if (op == '*') {
            v = p_check(ps, v * r);
        } else {
            if (r == 0) {
                ps->err = 1;
                return 0;
            }
            v = p_check(ps, op == '/' ? v / r : v % r);
        }
    }
}

static int64_t p_expr(struct parser *ps)
{
    int64_t v = p_term(ps);
    for (;;) {
        p_skip(ps);
        char op = *ps->p;
        if (op != '+' && op != '-')
            return v;
        ps->p++;
        int64_t r = p_term(ps);
        v = p_check(ps, op == '+' ? v + r : v - r);
    }
}

int k_eval(const char *expr, int32_t *out)
{
    struct parser ps = { expr, 0 };
    int64_t v = p_expr(&ps);
    p_skip(&ps);
    if (ps.err || *ps.p)
        return -1;
    *out = (int32_t)v;
    return 0;
}

static uint32_t rng_state = 2463534242u;

void k_srand(uint32_t seed) { rng_state = seed ? seed : 2463534242u; }

uint32_t k_rand(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
