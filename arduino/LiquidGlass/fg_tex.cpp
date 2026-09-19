#include "fg_tex.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void *default_alloc(unsigned long size) { return malloc((size_t)size); }
static fg_alloc_fn s_alloc = default_alloc;
static fg_alloc_fn s_alloc_small = default_alloc;
void fg_mem_set_alloc(fg_alloc_fn fn) { s_alloc = fn ? fn : default_alloc; }
void fg_mem_set_alloc_small(fg_alloc_fn fn) { s_alloc_small = fn ? fn : default_alloc; }
void *fg_mem_alloc(unsigned long size) { return s_alloc(size); }
void *fg_mem_alloc_small(unsigned long size)
{
    /* Anything this small is worth internal RAM if the platform has it; the caller's hook decides. */
    void *p = size <= 16384UL ? s_alloc_small(size) : 0;
    return p ? p : s_alloc(size);
}

static inline void unpack565(uint16_t c, float *r, float *g, float *b)
{
    unsigned cr = (c >> 11) & 0x1F, cg = (c >> 5) & 0x3F, cb = c & 0x1F;
    *r = (float)((cr << 3) | (cr >> 2)) * (1.0f / 255.0f);
    *g = (float)((cg << 2) | (cg >> 4)) * (1.0f / 255.0f);
    *b = (float)((cb << 3) | (cb >> 2)) * (1.0f / 255.0f);
}

void fg_tex_free(fg_tex_t *t)
{
    for (int i = 1; i < FG_MAX_LEVELS; i++) {
        if (t->lv[i]) free(t->lv[i]);
        t->lv[i] = 0;
    }
    t->levels = 0;
    t->src = 0;
}

int fg_tex_bind(fg_tex_t *t, const uint16_t *src, int w, int h)
{
    if (t->src == src && t->w == w && t->h == h && t->levels > 0) return 1;
    for (int i = 1; i < FG_MAX_LEVELS; i++) {
        t->lv[i] = 0;
    }
    t->src = src;
    t->w = w;
    t->h = h;
    t->lw[0] = w;
    t->lh[0] = h;
    t->sx[0] = 1.0f;
    t->sy[0] = 1.0f;
    t->levels = 1;

    int pw = w, ph = h;
    for (int L = 1; L < FG_MAX_LEVELS && (pw > 1 || ph > 1); L++) {
        int nw = pw > 1 ? pw / 2 : 1;
        int nh = ph > 1 ? ph / 2 : 1;
        /* Small levels go to INTERNAL RAM when there is room: the adaptive body tint fetches lod 7 for every glass
           pixel, and on the P4 that is otherwise a PSRAM round trip per pixel for a 10x6 image. */
        uint16_t *dst = (uint16_t *)fg_mem_alloc_small((unsigned long)nw * nh * 2);
        if (!dst) break;
        /* GL's own reduction, not a plain 2x2 box. When a level's dimension is ODD the spec's Manual Mipmap
           Generation (GL 4.6 8.14.4 / ARB_texture_non_power_of_two, inherited by ES 3.0) reduces it with a
           WEIGHTED 3-tap, because a 2-tap box would silently drop the last row or column. The port dropped it, and
           over eight halvings of a 960x400 still that moved the lod-7 ambient by ~0.05 in luma -- which the
           adaptive body tint turns into a visible cast over the whole interior of every shape. Measured against
           the real WebGL2 renderer on the three-disc still, with this and the texel convention below:
           body 2.077 -> 1.611, rim 1.926 -> 1.679 (mean |diff| /255, identical backgrounds). Each ALONE is worse
           than neither -- they are two halves of one convention. */
        const int nx = (pw & 1) && pw > 1 ? 3 : 2;
        const int ny = (ph & 1) && ph > 1 ? 3 : 2;
        float wx[3], wy[3];
        for (int y = 0; y < nh; y++) {
            if (ny == 3) {
                wy[0] = (float)(nh - y) / (float)ph; wy[1] = (float)nh / (float)ph; wy[2] = (float)(y + 1) / (float)ph;
            } else {
                wy[0] = wy[1] = 0.5f; wy[2] = 0.0f;
            }
            for (int x = 0; x < nw; x++) {
                if (nx == 3) {
                    wx[0] = (float)(nw - x) / (float)pw; wx[1] = (float)nw / (float)pw; wx[2] = (float)(x + 1) / (float)pw;
                } else {
                    wx[0] = wx[1] = 0.5f; wx[2] = 0.0f;
                }
                float acc[3] = { 0, 0, 0 };
                for (int dy = 0; dy < ny; dy++) {
                    int sy = y * 2 + dy;
                    if (sy > ph - 1) sy = ph - 1;
                    for (int dx = 0; dx < nx; dx++) {
                        int sx = x * 2 + dx;
                        if (sx > pw - 1) sx = pw - 1;
                        float r, g, b;
                        unpack565(L == 1 ? src[(size_t)sy * w + sx] : t->lv[L - 1][(size_t)sy * pw + sx], &r, &g, &b);
                        float wgt = wx[dx] * wy[dy];
                        acc[0] += wgt * r; acc[1] += wgt * g; acc[2] += wgt * b;
                    }
                }
                float inv = 1.0f;
                int ri = (int)(acc[0] * inv * 31.0f + 0.5f), gi = (int)(acc[1] * inv * 63.0f + 0.5f);
                int bi = (int)(acc[2] * inv * 31.0f + 0.5f);
                if (ri > 31) ri = 31;
                if (gi > 63) gi = 63;
                if (bi > 31) bi = 31;
                dst[(size_t)y * nw + x] = (uint16_t)((ri << 11) | (gi << 5) | bi);
            }
        }
        t->lv[L] = dst;
        t->lw[L] = nw;
        t->lh[L] = nh;
        t->sx[L] = (float)nw / (float)w;
        t->sy[L] = (float)nh / (float)h;
        t->levels = L + 1;
        pw = nw;
        ph = nh;
    }
    return t->levels > 0;
}

// Bilinear fetch inside one level. Level coordinates are the level-0 coordinate scaled by the level's own size
// over the base size, with the usual half-texel convention, and clamped — same as GL_CLAMP_TO_EDGE + GL_LINEAR.
//
// It fetches only the texels the weights actually use. That is not a cheat: GL's bilinear at tx = 0 returns
// c0 + (c1 - c0) * 0, which in IEEE is exactly c0 for any finite c1, so skipping the read is bit-identical and
// the fidelity harness proves it. It matters because the shader's MOST COMMON fetch is the plain background
// sample at lod 0, where the pixel centre lands exactly on a texel centre: tx and ty are both 0 and the general
// path was reading four texels, unpacking four RGB565 words and doing nine lerps to return the first one.
static void level_bilinear(const fg_tex_t *t, int L, float x0, float y0, float *r, float *g, float *b)
{
    int lw = t->lw[L], lh = t->lh[L];
    /* GL: texel coord = uv * levelSize - 0.5, and uv = pixel / level0Size. levelSize is the LEVEL'S OWN size,
       which for a non-power-of-two texture is not level0Size * 2^-L: level 7 of 400 rows is 3, not 3.125. Using
       the power-of-two scale stretched every deep fetch by up to 7 %. At level 0 this is the identity. */
    float fx = x0 * t->sx[L] - 0.5f;
    float fy = y0 * t->sy[L] - 0.5f;
    int ix = (int)floorf(fx), iy = (int)floorf(fy);
    float tx = fx - (float)ix, ty = fy - (float)iy;
    const int kx0 = ix, ky0 = iy;          /* the deep-level cache key: BEFORE the clamp (see below) */
    int x1 = ix + 1, y1 = iy + 1;
    if (ix < 0) ix = 0; else if (ix > lw - 1) ix = lw - 1;
    if (x1 < 0) x1 = 0; else if (x1 > lw - 1) x1 = lw - 1;
    if (iy < 0) iy = 0; else if (iy > lh - 1) iy = lh - 1;
    if (y1 < 0) y1 = 0; else if (y1 > lh - 1) y1 = lh - 1;

    /* DEEP-LEVEL CACHE. The adaptive body tint fetches lod 7 for EVERY glass pixel, and at level 7 one texel
       covers 128 source pixels, so along a row the same four texels are unpacked 128 times in a row: 9-22 % of
       the frame (ablation, below). Caching the four UNPACKED triples on (texture, level, ix, iy) and redoing only
       the three lerps is bit-identical — the lerps see the same inputs — and it costs nothing on the hot lod-0
       and aaLod fetches because the cache is only consulted from level 4 up, where a texel is at least 16 px.
       The caller serialises every entry on one mutex, so a single shared entry is safe; if
       that ever stops being true this has to become per-call state. */
    const uint16_t *base = (L == 0) ? t->src : t->lv[L];
    const int stride = (L == 0) ? t->w : lw;
    const uint16_t *p0 = base + (size_t)iy * stride;
    const uint16_t *p1 = base + (size_t)y1 * stride;

    if (L >= 4) {
        /* Keyed on the UNCLAMPED cell, not the clamped one: at the texture's low edge the unclamped -1 and 0
           both clamp ix to 0 but give x1 = 0 and x1 = 1 — different texels under the same key. That is not a
           theoretical corner, it is the top-left of the toolbar pill on a 1280x720 wall, and it showed up as 854
           differing pixels the moment the bit-exactness check was run. */
        static const fg_tex_t *cT;
        static int cL = -1, cix = 1 << 30, ciy = 1 << 30;
        static float cc[4][3];
        if (cT != t || cL != L || cix != kx0 || ciy != ky0) {
            unpack565(p0[ix], &cc[0][0], &cc[0][1], &cc[0][2]);
            unpack565(p0[x1], &cc[1][0], &cc[1][1], &cc[1][2]);
            unpack565(p1[ix], &cc[2][0], &cc[2][1], &cc[2][2]);
            unpack565(p1[x1], &cc[3][0], &cc[3][1], &cc[3][2]);
            cT = t; cL = L; cix = kx0; ciy = ky0;
        }
        if (tx == 0.0f) {
            if (ty == 0.0f) {
                *r = cc[0][0]; *g = cc[0][1]; *b = cc[0][2];
                return;
            }
            *r = cc[0][0] + (cc[2][0] - cc[0][0]) * ty;
            *g = cc[0][1] + (cc[2][1] - cc[0][1]) * ty;
            *b = cc[0][2] + (cc[2][2] - cc[0][2]) * ty;
            return;
        }
        for (int k = 0; k < 3; k++) {
            float top = cc[0][k] + (cc[1][k] - cc[0][k]) * tx;
            float v = (ty == 0.0f) ? top : top + ((cc[2][k] + (cc[3][k] - cc[2][k]) * tx) - top) * ty;
            if (k == 0) *r = v; else if (k == 1) *g = v; else *b = v;
        }
        return;
    }

    if (tx == 0.0f) {
        float r0, g0, b0;
        unpack565(p0[ix], &r0, &g0, &b0);
        if (ty == 0.0f) {
            *r = r0; *g = g0; *b = b0;
            return;
        }
        float r1, g1, b1;
        unpack565(p1[ix], &r1, &g1, &b1);
        *r = r0 + (r1 - r0) * ty;
        *g = g0 + (g1 - g0) * ty;
        *b = b0 + (b1 - b0) * ty;
        return;
    }

    float c[4][3];
    unpack565(p0[ix], &c[0][0], &c[0][1], &c[0][2]);
    unpack565(p0[x1], &c[1][0], &c[1][1], &c[1][2]);
    if (ty == 0.0f) {
        *r = c[0][0] + (c[1][0] - c[0][0]) * tx;
        *g = c[0][1] + (c[1][1] - c[0][1]) * tx;
        *b = c[0][2] + (c[1][2] - c[0][2]) * tx;
        return;
    }
    unpack565(p1[ix], &c[2][0], &c[2][1], &c[2][2]);
    unpack565(p1[x1], &c[3][0], &c[3][1], &c[3][2]);
    for (int k = 0; k < 3; k++) {
        float top = c[0][k] + (c[1][k] - c[0][k]) * tx;
        float bot = c[2][k] + (c[3][k] - c[2][k]) * tx;
        float v = top + (bot - top) * ty;
        if (k == 0) *r = v; else if (k == 1) *g = v; else *b = v;
    }
}

void fg_tex_lod(const fg_tex_t *t, float x, float y, float lod, float *r, float *g, float *b)
{
    if (!t->src) { *r = *g = *b = 0.0f; return; }
    if (lod < 0.0f) lod = 0.0f;
    float maxl = (float)(t->levels - 1);
    if (lod > maxl) lod = maxl;
#if FG_TRILINEAR
    int L0 = (int)lod;
    float f = lod - (float)L0;
#else
    /* LINEAR_MIPMAP_NEAREST: GL rounds the level, and the fetch touches half the texels. */
    int L0 = (int)(lod + 0.5f);
    if (L0 > t->levels - 1) L0 = t->levels - 1;
    const float f = 0.0f;
#endif
    float r0, g0, b0;
    level_bilinear(t, L0, x, y, &r0, &g0, &b0);
    if (f <= 0.0f || L0 + 1 >= t->levels) { *r = r0; *g = g0; *b = b0; return; }
    float r1, g1, b1;
    level_bilinear(t, L0 + 1, x, y, &r1, &g1, &b1);
    *r = r0 + (r1 - r0) * f;
    *g = g0 + (g1 - g0) * f;
    *b = b0 + (b1 - b0) * f;
}
