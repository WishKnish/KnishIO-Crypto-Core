/*
 * Minimal reader for <build>/vectors/differential.json as written by tests/gen_vectors.py: one case
 * per line, flat fields whose values are strings, integers or integer arrays. Not a JSON parser; it
 * relies on that fixed format. Used by kcore_selftest.c.
 */
#ifndef KCORE_VECREAD_H
#define KCORE_VECREAD_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Returns a pointer to the value of "key" in line (inside the quotes for strings) and its length. */
static const char *vr_field(const char *line, const char *key, size_t *len) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(line, pat);
    if (p == NULL) return NULL;
    p += strlen(pat);
    if (*p == '"') {
        p++;
        const char *e = strchr(p, '"');
        if (e == NULL) return NULL;
        *len = (size_t)(e - p);
        return p;
    }
    *len = (*p == '[') ? strcspn(p, "]") + 1 : strcspn(p, ",}");
    return p;
}

static int vr_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int vr_unhex(const char *s, size_t len, uint8_t *out) {
    if (len % 2) return -1;
    for (size_t i = 0; i < len / 2; i++) {
        int hi = vr_hexval(s[2 * i]), lo = vr_hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    return 0;
}

static void vr_tohex(const uint8_t *d, size_t n, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = digits[d[i] >> 4];
        out[2 * i + 1] = digits[d[i] & 15];
    }
}

static int vr_counts(const char *p, size_t len, int *counts, size_t max) {
    size_t n = 0;
    const char *end = p + len;
    if (len == 0 || *p != '[') return -1;
    p++;
    while (p < end && *p != ']') {
        char *next;
        if (n == max) return -1;
        counts[n++] = (int)strtol(p, &next, 10);
        if (next == p) return -1;
        p = next;
        if (*p == ',') p++;
    }
    return (int)n;
}

/* Reads a whole file into a NUL-terminated buffer; NULL on failure. */
static char *vr_slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)size + 1);
    if (buf == NULL || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[size] = 0;
    fclose(f);
    return buf;
}

#endif /* KCORE_VECREAD_H */
