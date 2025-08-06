// stb_base64.h - public domain base64 encode/decode by Jeff Bezanson, 2010
// https://github.com/nothings/stb/blob/master/stb_base64.h
// Only the encode/decode functions, minimal version for easy integration
#ifndef STB_BASE64_H
#define STB_BASE64_H

#include <stddef.h>
#include <stdint.h>

static const char stb_b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int stb_base64_encode(const unsigned char *data, size_t len, char *out)
{
    size_t i, j;
    for (i = 0, j = 0; i + 2 < len; i += 3) {
        out[j++] = stb_b64_table[(data[i] >> 2) & 0x3F];
        out[j++] = stb_b64_table[((data[i] & 0x3) << 4) | ((data[i+1] >> 4) & 0xF)];
        out[j++] = stb_b64_table[((data[i+1] & 0xF) << 2) | ((data[i+2] >> 6) & 0x3)];
        out[j++] = stb_b64_table[data[i+2] & 0x3F];
    }
    if (i < len) {
        out[j++] = stb_b64_table[(data[i] >> 2) & 0x3F];
        if (i + 1 < len) {
            out[j++] = stb_b64_table[((data[i] & 0x3) << 4) | ((data[i+1] >> 4) & 0xF)];
            out[j++] = stb_b64_table[(data[i+1] & 0xF) << 2];
            out[j++] = '=';
        } else {
            out[j++] = stb_b64_table[(data[i] & 0x3) << 4];
            out[j++] = '=';
            out[j++] = '=';
        }
    }
    out[j] = '\0';
    return (int)j;
}

static int stb_base64_decode(const char *src, unsigned char *out, size_t *outlen)
{
    int dtable[256], i, j, k;
    size_t len = 0;
    for (i = 0; i < 256; ++i) dtable[i] = -1;
    for (i = 0; i < 64; ++i) dtable[(unsigned char)stb_b64_table[i]] = i;
    for (i = 0; src[i]; ++i) {
        if (src[i] == '=') break;
        if (dtable[(unsigned char)src[i]] == -1) continue;
        ++len;
    }
    k = 0;
    for (i = 0; src[i]; ) {
        int a = src[i] == '=' ? 0 : dtable[(unsigned char)src[i++]];
        int b = src[i] == '=' ? 0 : dtable[(unsigned char)src[i++]];
        int c = src[i] == '=' ? 0 : dtable[(unsigned char)src[i++]];
        int d = src[i] == '=' ? 0 : dtable[(unsigned char)src[i++]];
        out[k++] = (a << 2) | (b >> 4);
        if (src[i-2] != '=') out[k++] = (b << 4) | (c >> 2);
        if (src[i-1] != '=') out[k++] = (c << 6) | d;
    }
    if (outlen) *outlen = k;
    return 0;
}

#endif // STB_BASE64_H
