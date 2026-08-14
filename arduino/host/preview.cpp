// Host-side preview for the Arduino renderer.
//
// Compiles the exact same lg_*.cpp the sketch does — no shims, no float
// substitutions — against a "display" that writes a PNG. So what you see here
// is what the panel gets, to the bit, and you can tune the material without
// reflashing anything.
//
//   make && ./preview
//   ./preview --frames 24 --out anim   # a sequence, for checking the drift
//
// Written as plain C++ with a hand-rolled PNG writer so it builds anywhere a
// compiler exists.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../LiquidGlass/lg_scene.h"
#include "../LiquidGlass/lg_render.h"
#include "../LiquidGlass/lg_rays.h"
#include "../LiquidGlass/lg_bg.h"
#include "../LiquidGlass/lg_demo.h"

// ---------------------------------------------------------------------------
// minimal PNG writer (stored deflate blocks + crc32/adler32)
// ---------------------------------------------------------------------------

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

static int write_png(const char *path, const unsigned char *rgb, int w, int h) {
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

// ---------------------------------------------------------------------------
// display stand-in
// ---------------------------------------------------------------------------

struct Canvas {
    int w, h;
    unsigned char *rgb;
};

static void flush_stripe(int x, int y, int w, int h, const uint16_t *px, void *user) {
    Canvas *cv = (Canvas *)user;
    for (int r = 0; r < h; r++) {
        int dy = y + r;
        if (dy < 0 || dy >= cv->h) continue;
        for (int c = 0; c < w; c++) {
            int dx = x + c;
            if (dx < 0 || dx >= cv->w) continue;
            uint16_t v = px[r * w + c];
            unsigned int cr = (v >> 11) & 0x1F, cg = (v >> 5) & 0x3F, cb = v & 0x1F;
            unsigned char *o = cv->rgb + ((size_t)dy * cv->w + dx) * 3;
            o[0] = (unsigned char)((cr << 3) | (cr >> 2));
            o[1] = (unsigned char)((cg << 2) | (cg >> 4));
            o[2] = (unsigned char)((cb << 3) | (cb >> 2));
        }
    }
}

int main(int argc, char **argv) {
    int W = 240, H = 240, frames = 1;
    const char *out = "preview";
    // Material overrides, applied after lg_demo_scene(). Tuning the edge rays
    // means looking at them, and looking at them should not mean a reflash.
    double ov_gain = -1, ov_disp = -1, ov_decay = -1, ov_bounce = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 2 < argc) { W = atoi(argv[++i]); H = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--gain") && i + 1 < argc) ov_gain = atof(argv[++i]);
        else if (!strcmp(argv[i], "--disp") && i + 1 < argc) ov_disp = atof(argv[++i]);
        else if (!strcmp(argv[i], "--decay") && i + 1 < argc) ov_decay = atof(argv[++i]);
        else if (!strcmp(argv[i], "--bounce") && i + 1 < argc) ov_bounce = atof(argv[++i]);
        else {
            printf("usage: preview [--size W H] [--frames N] [--out prefix]\n"
                   "               [--gain g] [--disp px] [--decay d] [--bounce b]\n");
            return 1;
        }
    }
    if (W > LG_MAX_WIDTH) { printf("width capped at LG_MAX_WIDTH=%d\n", LG_MAX_WIDTH); W = LG_MAX_WIDTH; }

    Canvas cv;
    cv.w = W; cv.h = H;
    cv.rgb = (unsigned char *)calloc((size_t)W * H * 3, 1);

    LGScene sc;
    char path[512];
    double total_ms = 0.0;

    for (int f = 0; f < frames; f++) {
        fx t = (fx)((int64_t)f * FX_ONE / (frames > 1 ? frames : 1));
        lg_demo_scene(&sc, W, H, t);
        if (ov_gain   >= 0) sc.p.rayGain   = (fx)(ov_gain * 65536.0);
        if (ov_disp   >= 0) sc.p.rayDisp   = (fx)(ov_disp * 65536.0);
        if (ov_decay  >= 0) sc.p.rayDecay  = (fx)(ov_decay * 65536.0);
        if (ov_bounce >= 0) sc.p.rayBounce = (fx)(ov_bounce * 65536.0);

        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        lg_render(&sc, W, H, flush_stripe, &cv);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double ms = (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
        total_ms += ms;

        if (frames == 1) snprintf(path, sizeof path, "%s.png", out);
        else             snprintf(path, sizeof path, "%s%03d.png", out, f);
        if (!write_png(path, cv.rgb, W, H)) { printf("cannot write %s\n", path); return 1; }
        printf("%s  %dx%d  %d rays  %.1f ms\n", path, W, H, lg_rays_count(), ms);
    }
    printf("mean %.1f ms/frame over %d frame(s) on this host\n", total_ms / frames, frames);
    free(cv.rgb);
    return 0;
}
