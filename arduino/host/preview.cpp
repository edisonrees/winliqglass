// Host-side preview for the Arduino renderer.
//
// Compiles the exact same fg_*.cpp the sketch does — no shims, no float
// substitutions — against a "display" that writes a PNG. So what you see here
// is what the panel gets, and you can tune the material without reflashing
// anything.
//
//   make && ./preview
//   ./preview --frames 24 --out anim      # a sequence, for checking the drift
//   ./preview --size 480 320 --content    # the other of engine.py's two passes
//   ./preview --noopt                     # the unoptimised renderer, bit for bit
//
// The PNG is the RGB565 the renderer emitted, expanded back to 8 bits. It
// therefore shows the 4x4 Bayer dither too, which is honest: that dither is on
// the panel. If you are diffing against the shader rather than looking at it,
// diff before the quantisation — fg_render_set_f32() hands you the float
// triples and fg_render_set_dither(0) turns the dither off.
//
// Written as plain C++ with a hand-rolled PNG writer so it builds anywhere a
// compiler exists.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../LiquidGlass/fg_glass.h"
#include "../LiquidGlass/fg_tex.h"
#include "../LiquidGlass/fg_demo.h"
#include "png_write.h"

// ---------------------------------------------------------------------------
// the "display"
// ---------------------------------------------------------------------------
// fg_render_region hands over finished stripes exactly as it does on the panel;
// this one unpacks them into an RGB8 canvas instead of pushing them over SPI.

struct Canvas { int w, h; unsigned char *rgb; };

static void flush_stripe(int x, int y, int w, int h, const unsigned short *px, void *user) {
    Canvas *cv = (Canvas *)user;
    for (int r = 0; r < h; r++) {
        int dy = y + r;
        if (dy < 0 || dy >= cv->h) continue;
        for (int c = 0; c < w; c++) {
            int dx = x + c;
            if (dx < 0 || dx >= cv->w) continue;
            unsigned short v = px[r * w + c];
            unsigned cr = (v >> 11) & 0x1F, cg = (v >> 5) & 0x3F, cb = v & 0x1F;
            unsigned char *o = cv->rgb + ((size_t)dy * cv->w + dx) * 3;
            // Replicate the top bits into the low ones, so full scale stays full scale.
            o[0] = (unsigned char)((cr << 3) | (cr >> 2));
            o[1] = (unsigned char)((cg << 2) | (cg >> 4));
            o[2] = (unsigned char)((cb << 3) | (cb >> 2));
        }
    }
}

int main(int argc, char **argv) {
    int W = 320, H = 320, frames = 1, content = 0, noopt = 0, ss = -1;
    const char *out = "preview";
    double ov_bend = 0, ov_edge = 0, ov_opacity = 0, ov_disp = 0;
    int has_bend = 0, has_edge = 0, has_opacity = 0, has_disp = 0;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--frames")  && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out")     && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--size")    && i + 2 < argc) { W = atoi(argv[++i]); H = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--ss")      && i + 1 < argc) ss = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bend")    && i + 1 < argc) { ov_bend = atof(argv[++i]); has_bend = 1; }
        else if (!strcmp(argv[i], "--edge")    && i + 1 < argc) { ov_edge = atof(argv[++i]); has_edge = 1; }
        else if (!strcmp(argv[i], "--opacity") && i + 1 < argc) { ov_opacity = atof(argv[++i]); has_opacity = 1; }
        else if (!strcmp(argv[i], "--disp")    && i + 1 < argc) { ov_disp = atof(argv[++i]); has_disp = 1; }
        else if (!strcmp(argv[i], "--content")) content = 1;
        else if (!strcmp(argv[i], "--noopt"))   noopt = 1;
        else {
            printf("usage: preview [--size W H] [--frames N] [--out prefix] [--ss N]\n"
                   "               [--bend px] [--edge px] [--opacity a] [--disp d]\n"
                   "               [--content] [--noopt]\n"
                   "  --content  engine.py's content pass (bend -45.76, edge 12) instead of its UI pass\n"
                   "  --ss N     supersample N*N per pixel — the library's device-pixel-ratio, which is\n"
                   "             all a DPR is. It does not change how wide the lens band is.\n"
                   "  --noopt    render with the row-active-list and interior fast path OFF. Same pixels,\n"
                   "             slowly: that equality is the proof the optimisations are exact.\n");
            return 1;
        }
    }
    if (W < 1 || H < 1) { printf("bad size\n"); return 1; }
    if (W > 1280) { printf("width capped at 1280 (the stripe buffer)\n"); W = 1280; }

    // The wall, baked once and mipped once — the same two calls the sketch makes.
    unsigned short *wallPixels = (unsigned short *)malloc((size_t)W * H * 2);
    if (!wallPixels) { printf("out of memory for the wall\n"); return 1; }
    fg_demo_wall(wallPixels, W, H);

    fg_tex_t wall;
    memset(&wall, 0, sizeof wall);
    if (!fg_tex_bind(&wall, wallPixels, W, H)) { printf("mip pyramid failed\n"); return 1; }

    if (noopt) fg_opt_set(0, 0);

    Canvas cv;
    cv.w = W; cv.h = H;
    cv.rgb = (unsigned char *)calloc((size_t)W * H * 3, 1);

    FGScene sc;
    char path[512];
    double total_ms = 0.0;

    for (int f = 0; f < frames; f++) {
        float t = (float)f / (float)(frames > 1 ? frames : 1);
        fg_demo_scene(&sc, W, H, t);
        if (content) {
            // Re-derive the params for the other pass, then re-apply the scene's own pixel scaling.
            float k = (float)(W < H ? W : H) / 240.0f;
            fg_params_movie(&sc.p, 0);
            sc.p.bend *= k;
            sc.p.edge = sc.p.edge * k > 3.0f ? sc.p.edge * k : 3.0f;
            sc.p.mergeK *= k;
        }
        if (has_bend)    sc.p.bend = (float)ov_bend;
        if (has_edge)    sc.p.edge = (float)ov_edge;
        if (has_opacity) sc.p.opacity = (float)ov_opacity;
        if (has_disp)    sc.p.disp = (float)ov_disp;
        if (ss > 0)      sc.p.ss = ss;

        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        fg_render_region(&sc, &wall, 0, 0, W, H, flush_stripe, &cv);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double ms = (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
        total_ms += ms;

        if (frames == 1) snprintf(path, sizeof path, "%s.png", out);
        else             snprintf(path, sizeof path, "%s%03d.png", out, f);
        if (!lg_write_png(path, cv.rgb, W, H)) { printf("cannot write %s\n", path); return 1; }
        printf("%s  %dx%d  %s pass  %d taps  ss %d  %.1f ms  (%.3f us/px)\n",
               path, W, H, content ? "content" : "ui", FG_SPECTRAL_TAPS,
               sc.p.ss, ms, ms * 1000.0 / ((double)W * H));
    }
    printf("mean %.1f ms/frame over %d frame(s) on this host\n", total_ms / frames, frames);

    fg_tex_free(&wall);
    free(wallPixels);
    free(cv.rgb);
    return 0;
}
