#ifndef CFL_CORE_H
#define CFL_CORE_H

#include <limits.h>
#include <stddef.h>
#include <stdio.h>

/*
 * Small, dependency-free helpers shared by CFL and its unit tests.
 * Return 0 on success and -1 on invalid input or insufficient capacity.
 */
static inline int CflBuildPath(char *dst, size_t dstSize,
                               const char *base, const char *entry) {
    if (dst == NULL || dstSize == 0 || base == NULL || entry == NULL)
        return -1;

    size_t baseLen = 0;
    while (base[baseLen] != '\0')
        ++baseLen;

    int written;
    if (baseLen > 0 && base[baseLen - 1] == '/')
        written = snprintf(dst, dstSize, "%s%s", base, entry);
    else
        written = snprintf(dst, dstSize, "%s/%s", base, entry);

    if (written < 0 || (size_t)written >= dstSize) {
        dst[0] = '\0';
        return -1;
    }

    return 0;
}

/* Encode arbitrary path bytes as a JSON string value's contents. */
static inline int CflJsonEscape(const char *src, char *dst, size_t dstSize) {
    static const char Hex[] = "0123456789abcdef";
    if (src == NULL || dst == NULL || dstSize == 0)
        return -1;

    size_t out = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p != 0; ++p) {
        char encoded[6];
        size_t count = 0;

        switch (*p) {
            case '"':  encoded[0] = '\\'; encoded[1] = '"'; count = 2; break;
            case '\\': encoded[0] = '\\'; encoded[1] = '\\'; count = 2; break;
            case '\b': encoded[0] = '\\'; encoded[1] = 'b'; count = 2; break;
            case '\f': encoded[0] = '\\'; encoded[1] = 'f'; count = 2; break;
            case '\n': encoded[0] = '\\'; encoded[1] = 'n'; count = 2; break;
            case '\r': encoded[0] = '\\'; encoded[1] = 'r'; count = 2; break;
            case '\t': encoded[0] = '\\'; encoded[1] = 't'; count = 2; break;
            default:
                if (*p < 0x20) {
                    encoded[0] = '\\';
                    encoded[1] = 'u';
                    encoded[2] = '0';
                    encoded[3] = '0';
                    encoded[4] = Hex[*p >> 4];
                    encoded[5] = Hex[*p & 0x0f];
                    count = 6;
                } else {
                    encoded[0] = (char)*p;
                    count = 1;
                }
                break;
        }

        if (count >= dstSize - out) {
            dst[0] = '\0';
            return -1;
        }
        for (size_t i = 0; i < count; ++i)
            dst[out++] = encoded[i];
    }

    dst[out] = '\0';
    return 0;
}

/* Read one complete newline-delimited record; safely handle EOF and blank lines. */
static inline int CflReadLine(FILE *stream, char *dst, size_t dstSize) {
    if (stream == NULL || dst == NULL || dstSize < 2)
        return -1;

    int readSize = dstSize > (size_t)INT_MAX ? INT_MAX : (int)dstSize;
    if (fgets(dst, readSize, stream) == NULL) {
        dst[0] = '\0';
        return 0;
    }

    size_t len = 0;
    while (dst[len] != '\0')
        ++len;

    if (len > 0 && dst[len - 1] == '\n') {
        dst[len - 1] = '\0';
        return 1;
    }

    if (feof(stream))
        return 1;

    /* A full buffer may still represent an exactly-sized line. */
    int ch = fgetc(stream);
    if (ch == '\n' || ch == EOF)
        return 1;

    while (ch != '\n' && ch != EOF)
        ch = fgetc(stream);
    dst[0] = '\0';
    return -1;
}

#endif
