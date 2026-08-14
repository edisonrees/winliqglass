#include "lg_bg.h"

static int   s_w = 240, s_h = 240;
static fx    s_invW = FX(1.0 / 240.0), s_invH = FX(1.0 / 240.0);

static const uint16_t *s_img = 0;
static int s_imgW = 0, s_imgH = 0;
static fx  s_imgSx = FX_ONE, s_imgSy = FX_ONE;   // cover-fit scale, uv space
static fx  s_imgOx = 0, s_imgOy = 0;
static uint8_t s_imgAmb[16];                      // 4x4 luma grid, 0..255

// Row cache for the pure-background fast path.
static fx s_rowY = FX_MAXV;
static fx s_rowBaseR, s_rowBaseG, s_rowBaseB;
static fx s_rowV;

void lg_bg_set_viewport(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    s_w = w; s_h = h;
    s_invW = fxd(FX_ONE, fxfromint(w));
    s_invH = fxd(FX_ONE, fxfromint(h));
    s_rowY = FX_MAXV;

    if (s_img) {
        // cover fit, same rule as engine.cover_affine()
        fx ar_vp  = fxd(fxfromint(w), fxfromint(h));
        fx ar_img = fxd(fxfromint(s_imgW), fxfromint(s_imgH));
        fx r = fxd(ar_vp, ar_img);
        if (r > FX_ONE) { s_imgSx = FX_ONE; s_imgSy = fxd(FX_ONE, r); }
        else            { s_imgSx = r;      s_imgSy = FX_ONE; }
        s_imgOx = (FX_ONE - s_imgSx) / 2;
        s_imgOy = (FX_ONE - s_imgSy) / 2;
    }
}

void lg_bg_set_image(const uint16_t *pixels, int w, int h) {
    s_img = pixels;
    s_imgW = w;
    s_imgH = h;
    s_rowY = FX_MAXV;
    if (!pixels || w < 1 || h < 1) { s_img = 0; return; }

    // Box-average the image down to 4x4 once, so the adaptive tint has a
    // cheap stand-in for the wide mip it uses on the GPU.
    for (int cy = 0; cy < 4; cy++) {
        for (int cx = 0; cx < 4; cx++) {
            uint32_t acc = 0, n = 0;
            int x0 = cx * w / 4, x1 = (cx + 1) * w / 4;
            int y0 = cy * h / 4, y1 = (cy + 1) * h / 4;
            int sx = (x1 - x0) / 8 + 1, sy = (y1 - y0) / 8 + 1;
            for (int y = y0; y < y1; y += sy) {
                for (int x = x0; x < x1; x += sx) {
                    uint16_t c = pixels[y * w + x];
                    uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, bch = c & 0x1F;
                    acc += (r * 54 * 8 + g * 183 * 4 + bch * 18 * 8) >> 8;
                    n++;
                }
            }
            s_imgAmb[cy * 4 + cx] = n ? (uint8_t)(acc / n) : 0;
        }
    }
    lg_bg_set_viewport(s_w, s_h);
}

// --------------------------------------------------------------------------
// bitmap sampling
// --------------------------------------------------------------------------

static void img_rgb(fx u, fx v, fx *r, fx *g, fx *b) {
    u = fxsat(fxm(u, s_imgSx) + s_imgOx);
    v = fxsat(fxm(v, s_imgSy) + s_imgOy);
    int px = fxfloor_i(fxm(u, fxfromint(s_imgW - 1)));
    int py = fxfloor_i(fxm(v, fxfromint(s_imgH - 1)));
    if (px < 0) px = 0; else if (px >= s_imgW) px = s_imgW - 1;
    if (py < 0) py = 0; else if (py >= s_imgH) py = s_imgH - 1;
    uint16_t c = s_img[py * s_imgW + px];
    // 5/6/5 -> Q16.16 [0,1] with the top bits replicated into the low ones,
    // so full-scale stays full-scale.
    uint32_t cr = (c >> 11) & 0x1F, cg = (c >> 5) & 0x3F, cb = c & 0x1F;
    *r = (fx)((cr * 2114) + (cr >> 3));
    *g = (fx)((cg * 1040) + (cg >> 4));
    *b = (fx)((cb * 2114) + (cb >> 3));
}

// --------------------------------------------------------------------------
// procedural dusk
// --------------------------------------------------------------------------
//
// Three sines per pixel is the whole budget. The vertical gradient carries the
// broad polarity and depends only on y, so it is hoisted into the row cache;
// what is left in the inner loop is two ribbons and one caustic.

static void grad_at(fx v, fx *r, fx *g, fx *b) {
    // indigo -> magenta -> ember -> dark water, the run a dusk sky makes
    fx t = fxsat(v);
    if (t < FX(0.42)) {
        fx k = fxd(t, FX(0.42));
        *r = fxlerp(FX(0.043), FX(0.310), k);
        *g = fxlerp(FX(0.055), FX(0.145), k);
        *b = fxlerp(FX(0.145), FX(0.330), k);
    } else if (t < FX(0.66)) {
        fx k = fxd(t - FX(0.42), FX(0.24));
        *r = fxlerp(FX(0.310), FX(0.960), k);
        *g = fxlerp(FX(0.145), FX(0.420), k);
        *b = fxlerp(FX(0.330), FX(0.180), k);
    } else {
        fx k = fxd(t - FX(0.66), FX(0.34));
        *r = fxlerp(FX(0.960), FX(0.070), k);
        *g = fxlerp(FX(0.420), FX(0.075), k);
        *b = fxlerp(FX(0.180), FX(0.115), k);
    }
}

static void proc_rgb(fx u, fx v, fx *r, fx *g, fx *b) {
    fx br, bg, bb;
    grad_at(v, &br, &bg, &bb);

    // Broad ribbons: two low-frequency diagonals beating against each other,
    // which is what gives the lens something wide to fold at the rim.
    fx a1 = fxm(u, FX(1.35)) + fxm(v, FX(-0.62));
    fx s1 = fxsin(a1 + FX(0.11));
    fx a2 = fxm(u, FX(-0.80)) + fxm(v, FX(1.90));
    fx s2 = fxsin(a2 + FX(0.47));
    fx ribbon = fxm(s1, FX(0.085)) + fxm(s2, FX(0.055));

    // Hairline caustics: a high-frequency band raised to a power so only the
    // crests survive. These are the detail that shows the compression rings.
    fx a3 = fxm(u, FX(7.30)) + fxm(v, FX(4.10));
    fx c = fxsat(fxsin(a3));
    fx c2 = fxm(c, c); c2 = fxm(c2, c2); c2 = fxm(c2, c2);   // c^8
    fx caust = fxm(c2, FX(0.42));

    *r = fxsat(br + ribbon + fxm(caust, FX(1.00)));
    *g = fxsat(bg + fxm(ribbon, FX(0.70)) + fxm(caust, FX(0.94)));
    *b = fxsat(bb + fxm(ribbon, FX(1.25)) + fxm(caust, FX(0.86)));
}

// --------------------------------------------------------------------------
// public
// --------------------------------------------------------------------------

void lg_bg_rgb(fx x, fx y, fx *r, fx *g, fx *b) {
    fx u = fxm(x, s_invW);
    fx v = fxm(y, s_invH);
    if (s_img) { img_rgb(u, v, r, g, b); return; }
    proc_rgb(fxclamp(u, 0, FX_ONE), fxclamp(v, 0, FX_ONE), r, g, b);
}

void lg_bg_prepare_row(fx y) {
    s_rowY = y;
    s_rowV = fxclamp(fxm(y, s_invH), 0, FX_ONE);
    if (!s_img) grad_at(s_rowV, &s_rowBaseR, &s_rowBaseG, &s_rowBaseB);
}

void lg_bg_rgb_row(fx x, fx *r, fx *g, fx *b) {
    if (s_img) { lg_bg_rgb(x, s_rowY, r, g, b); return; }
    fx u = fxclamp(fxm(x, s_invW), 0, FX_ONE);
    fx v = s_rowV;

    fx s1 = fxsin(fxm(u, FX(1.35)) + fxm(v, FX(-0.62)) + FX(0.11));
    fx s2 = fxsin(fxm(u, FX(-0.80)) + fxm(v, FX(1.90)) + FX(0.47));
    fx ribbon = fxm(s1, FX(0.085)) + fxm(s2, FX(0.055));

    fx c = fxsat(fxsin(fxm(u, FX(7.30)) + fxm(v, FX(4.10))));
    fx c2 = fxm(c, c); c2 = fxm(c2, c2); c2 = fxm(c2, c2);
    fx caust = fxm(c2, FX(0.42));

    *r = fxsat(s_rowBaseR + ribbon + caust);
    *g = fxsat(s_rowBaseG + fxm(ribbon, FX(0.70)) + fxm(caust, FX(0.94)));
    *b = fxsat(s_rowBaseB + fxm(ribbon, FX(1.25)) + fxm(caust, FX(0.86)));
}

fx lg_bg_ambient(fx x, fx y) {
    if (s_img) {
        fx u = fxsat(fxm(fxm(x, s_invW), s_imgSx) + s_imgOx);
        fx v = fxsat(fxm(fxm(y, s_invH), s_imgSy) + s_imgOy);
        int cx = fxfloor_i(fxm(u, FX(3.999)));
        int cy = fxfloor_i(fxm(v, FX(3.999)));
        if (cx < 0) cx = 0; else if (cx > 3) cx = 3;
        if (cy < 0) cy = 0; else if (cy > 3) cy = 3;
        return (fx)s_imgAmb[cy * 4 + cx] * 257;
    }
    // The gradient is the low-frequency content by construction, so its luma
    // *is* the wide mip. No blur needed, which is the whole trick.
    fx v = fxclamp(fxm(y, s_invH), 0, FX_ONE);
    fx r, g, b;
    grad_at(v, &r, &g, &b);
    return fxm(r, FX(0.2126)) + fxm(g, FX(0.7152)) + fxm(b, FX(0.0722));
}
