// Minimal PNG writer (stored deflate blocks + crc32/adler32), lifted VERBATIM from the PREVIOUS version of
// arduino/host/preview.cpp, where it was written inline. It is split into a header here only so preview.cpp can
// stay about the renderer; not one byte of the encoder changed. The function name is lg_write_png rather than
// write_png so that a harness linking this against another encoder does not collide.
//
// Stored (uncompressed) deflate blocks mean no zlib dependency and no build system: it compiles anywhere a
// compiler exists, which is the whole point of the host build.
#ifndef LG_PNG_WRITE_H
#define LG_PNG_WRITE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long crc_table[256];
static int crc_ready = 0;

static void crc_init(void) {
    for (unsigned long n = 0; n < 256; n++) {
        unsigned long c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static unsigned long crc32buf(const unsigned char *buf, size_t len, unsigned long crc) {
    if (!crc_ready) crc_init();
    crc ^= 0xffffffffUL;
    for (size_t n = 0; n < len; n++) crc = crc_table[(crc ^ buf[n]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffUL;
}

static void put32(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static void png_chunk(FILE *f, const char *tag, const unsigned char *data, size_t len) {
    unsigned char hdr[8];
    put32(hdr, (unsigned long)len);
    memcpy(hdr + 4, tag, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    unsigned long crc = crc32buf((const unsigned char *)tag, 4, 0);
    if (len) crc = crc32buf(data, len, crc);
    unsigned char c[4];
    put32(c, crc);
    fwrite(c, 1, 4, f);
}

static int lg_write_png(const char *path, const unsigned char *rgb, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    static const unsigned char sig[8] = { 137, 'P', 'N', 'G', '\r', '\n', 26, '\n' };
    fwrite(sig, 1, 8, f);

    unsigned char ihdr[13];
    put32(ihdr, (unsigned long)w);
    put32(ihdr + 4, (unsigned long)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);

    size_t raw_len = (size_t)h * ((size_t)w * 3 + 1);
    unsigned char *raw = (unsigned char *)malloc(raw_len);
    for (int y = 0; y < h; y++) {
        unsigned char *dst = raw + (size_t)y * ((size_t)w * 3 + 1);
        *dst++ = 0;
        memcpy(dst, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }

    size_t nblk = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + nblk * 5 + 4;
    unsigned char *z = (unsigned char *)malloc(z_len);
    size_t zi = 0;
    z[zi++] = 0x78; z[zi++] = 0x01;
    size_t off = 0;
    while (off < raw_len) {
        size_t n = raw_len - off;
        if (n > 65535) n = 65535;
        int last = (off + n >= raw_len);
        z[zi++] = (unsigned char)last;
        z[zi++] = (unsigned char)(n & 0xff);
        z[zi++] = (unsigned char)(n >> 8);
        z[zi++] = (unsigned char)(~n & 0xff);
        z[zi++] = (unsigned char)((~n >> 8) & 0xff);
        memcpy(z + zi, raw + off, n);
        zi += n;
        off += n;
    }
    unsigned long a = 1, bsum = 0;
    for (size_t i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521;
        bsum = (bsum + a) % 65521;
    }
    put32(z + zi, (bsum << 16) | a);
    zi += 4;

    png_chunk(f, "IDAT", z, zi);
    png_chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw);
    free(z);
    return 1;
}


#endif // LG_PNG_WRITE_H
