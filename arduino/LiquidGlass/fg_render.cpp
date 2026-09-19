// Scene construction, the Movie preset, and the stripe driver (engine.py's draw loop, minus the GPU).
#include "fg_glass.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

int fg_shade_pixel_sub(const FGScene *sc, const fg_tex_t *bg,
                       const unsigned char *fidx, int fn, const unsigned char *midx, int mn,
                       int interior_ok, float px, float py, float *r, float *g, float *b);

static int s_opt_rowlist = 1;
static int s_opt_interior = 1;
void fg_opt_set(int row_active_list, int interior_fast_path)
{
    s_opt_rowlist = row_active_list ? 1 : 0;
    s_opt_interior = interior_fast_path ? 1 : 0;
}

// ---------------------------------------------------------------------------
// the WebGL "Movie" preset
// ---------------------------------------------------------------------------
// presets.md, demo/video.html (branch `webgl`):
//   setDirection(-1); setIntensity(0.14); setMaterial({bend: 0.52});
//   setHighlight({angle: 0, bounce: 0.85, strength: 2.3, specular: 2.6, sharpen: 0.42, base: 0.14});
// applied over sourceParams(glass, isUi) in winliqglass.js. Every number below is that arithmetic done once.
// NOTHING here is tuned per widget. Per-kind opacity/tint tuning is exactly what made the first attempt at this
// port read as liquid OPAQUE rather than liquid glass; the preset is applied once and left alone.
void fg_params_movie(FGParams *p, int ui)
{
    const float glass = 0.14f;            // setIntensity
    const float direction = -1.0f;        // setDirection: uBend = bend * direction, i.e. INWARD
    const float hiStrength = 2.3f, hiSpecular = 2.6f, hiSharpen = 0.42f;

    memset(p, 0, sizeof *p);
    if (ui) {
        p->opacity = 0.10f + 0.18f * glass;          // 0.1252
        p->frost = 0.0f;
        p->bend = 21.0f * direction;                 // -21
        p->mergeK = 26.0f;
        p->edge = 7.0f;
        p->aniso = 0.80f;
        p->disp = 0.85f;
        p->travel = 0.85f;
        p->shadow = 0.0f;
        p->shadowR = 9.0f;
        p->spec = 0.34f * hiSpecular;                // 0.884
        p->shine = fmaxf(26.0f * hiSharpen, 1.5f);   // 10.92
        p->adapt = 0.48f;
        p->sat = 0.06f;
        p->rimLit = 0.34f * hiStrength;              // 0.782
    } else {
        p->opacity = glass;                          // 0.14
        p->frost = 0.0f;                             // material.frost * 22, frost = 0
        p->bend = 0.52f * 88.0f * direction;         // -45.76
        p->mergeK = 4.0f + 0.34f * 86.0f;            // 33.24
        p->edge = 12.0f;
        p->aniso = 1.0f;
        p->disp = 0.70f;
        p->travel = 0.85f;
        p->shadow = 0.0f;
        p->shadowR = 14.0f;
        p->spec = 0.30f * hiSpecular;                // 0.78
        p->shine = fmaxf(22.0f * hiSharpen, 1.5f);   // 9.24
        p->adapt = 0.50f;
        p->sat = 0.07f;
        p->rimLit = 0.30f * hiStrength;              // 0.69
    }

    // _axis(): angle 0, sway 0 -> [sin 0, -cos 0] = (0, -1), light from the top (pix.y runs DOWN).
    const float ax = 0.0f, ay = -1.0f;
    p->key[0] = ax * 0.62f;  p->key[1] = ay * 0.62f;  p->key[2] = 0.78f;
    p->fill[0] = -ax * 0.55f; p->fill[1] = -ay * 0.55f; p->fill[2] = 0.62f;
    p->hiDir[0] = ax; p->hiDir[1] = ay;
    p->hiBounce = 0.85f;
    p->hiSharp = 1.0f;          // max(sharpness, 0.05)
    p->hiBase = 0.14f;
    p->touchA = 0.0f;
    p->ss = FG_SS;        /* the library's DPR: supersampling only. It does not change the geometry. */
}

void fg_scene_init(FGScene *sc, int ui)
{
    sc->count = 0;
    fg_params_movie(&sc->p, ui);
}

FGShape *fg_scene_add(FGScene *sc)
{
    if (sc->count >= FG_MAX_SHAPES) return 0;
    FGShape *s = &sc->shapes[sc->count++];
    memset(s, 0, sizeof *s);
    s->kind = FG_CIRCLE;
    s->hw = s->hh = 40.0f;
    s->tint[0] = s->tint[1] = s->tint[2] = 1.0f;
    s->tint[3] = 0.0f;
    s->merge = 0.0f;      /* winliqglass.js normalizedShape(): `merge: rect.merge ? 1 : 0` — joining is opt-in */
    return s;
}

// ---------------------------------------------------------------------------
// culling bounds
// ---------------------------------------------------------------------------
// The shader has no culling — every fragment loops every shape. Skipping a shape is exact when smin() provably
// returns the running minimum unchanged, which it does once its SDF exceeds that minimum by k (h saturates at 1 and
// the k*h*(1-h) term vanishes); and materialAt()'s weight exp(-d/22) is below 1e-12 past 620 px, which is under a
// float ULP of the accumulators here.
//
// Exact in arithmetic is not the same as bit-identical, and this one is not bit-identical: smin() is a FOLD, and
// float addition is not associative, so changing which shapes are in the fold changes the ROUNDING even where it
// cannot change the value. (smin's own saturated case is `mixf(b, a, 1.0f)` = `b + (a - b)`, which is exact for
// nearby operands and not for distant ones.) Measured against the unculled path over eight demo frames, pre-dither:
// about 4 300 of 307 200 floats differ, by at most 0.026/255 — three orders below the RGB565 step the output is
// quantised to, and worth at most ONE flipped 565 pixel per frame. The interior fast path below IS bit-identical.
//
// WHICH k, THOUGH. field_sub() folds shape j in with `sminf(d, dj, k_j)` — the k of the shape being ADDED, applied
// to an accumulator that already carries every earlier shape. So an earlier shape stops mattering only once it
// clears the running minimum by the k of every smin still to come, not by its own. Using its own k is too tight
// the moment a scene mixes merge settings: a merge = 0 card (k = 1) gets a bound 34 px narrower than the
// merge = 1 pill that will later blend it at k = 34.67, and dropping it moves the field. The demo scene here does
// exactly that, and it showed up as 290 pixels differing from --noopt with a maximum of 148/255 — a visible notch
// in the join, not a rounding difference. So the bound takes the SCENE's largest k. Widening a cull bound can only
// put shapes back, never take them away, so this is safe by construction; where every shape shares one k (any
// scene with no merging, which is most of them) it is the same number as before and the same pixels.
//
// Found by packaging this port for Arduino, because the demo scene is the first one to merge two shapes under a
// third that does not. It is a bug in the culling, not in the material.
static float extent_axis(const FGShape *s, int axis)
{
    if (s->rot != 0.0f) return -1.0f;
    switch (s->kind) {
    case FG_CIRCLE: return s->hw;
    case FG_RRECT:  return axis ? s->hh : s->hw;
    case FG_RING:   return s->hw + s->rad;
    default:        return -1.0f;
    }
}

static float bound_field(const FGShape *s, const FGParams *p, float kmax, int axis)
{
    float e = extent_axis(s, axis);
    if (e < 0.0f) e = sqrtf(s->hw * s->hw + s->hh * s->hh) + s->rad;
    return e + kmax + fabsf(p->edge) + 4.0f;
}

static float bound_material(const FGShape *s, int axis)
{
    float e = extent_axis(s, axis);
    if (e < 0.0f) e = sqrtf(s->hw * s->hw + s->hh * s->hh) + s->rad;
    return e + 620.0f;      /* exp(-620/22) = 8e-13 */
}

// ---------------------------------------------------------------------------
// the frame
// ---------------------------------------------------------------------------
#define FG_STRIPE_H 8

static unsigned short *s_stripe;
static int s_stripe_w;

/* Fidelity tap (host instrument, see fg_glass.h). Null on the device, so the branch below is one never-taken,
   perfectly-predicted test per pixel. */
static float *s_f32;
void fg_render_set_f32(float *dst) { s_f32 = dst; }

static int s_dither = 1;
void fg_render_set_dither(int on) { s_dither = on ? 1 : 0; }

static const unsigned char kBayer[16] = {
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5
};

static inline unsigned short pack565(float r, float g, float b, int x, int y)
{
    float d = s_dither ? (((float)kBayer[((y & 3) << 2) | (x & 3)] + 0.5f) / 16.0f - 0.5f) : 0.0f;
    int ri = (int)floorf(r * 31.0f + d + 0.5f);
    int gi = (int)floorf(g * 63.0f + d + 0.5f);
    int bi = (int)floorf(b * 31.0f + d + 0.5f);
    if (ri < 0) ri = 0; else if (ri > 31) ri = 31;
    if (gi < 0) gi = 0; else if (gi > 63) gi = 63;
    if (bi < 0) bi = 0; else if (bi > 31) bi = 31;
    return (unsigned short)((ri << 11) | (gi << 5) | bi);
}

void fg_render_region(const FGScene *sc, const fg_tex_t *bg, int x0, int y0, int w, int h,
                      fg_flush_fn flush, void *user)
{
    if (w < 1 || h < 1) return;
    /* One buffer, panel-width, allocated once through the hook (PSRAM on device): .bss is internal RAM on the P4 and
       this firmware runs with ~49 KB of it free. 1280 * 8 * 2 = 20 480 B. */
    if (!s_stripe) {
        s_stripe = (unsigned short *)fg_mem_alloc(1280UL * FG_STRIPE_H * 2);
        s_stripe_w = 1280;
    }
    if (!s_stripe || w > s_stripe_w) return;

    int nsh = sc->count > FG_MAX_SHAPES ? FG_MAX_SHAPES : sc->count;
    /* The largest k any smin in this scene will use — see bound_field above for why it is the scene's and not
       each shape's. `1.0f` is the floor field_sub() applies to every shape, so a scene with no merging lands
       here and the bounds are what they always were. */
    float kmax = 1.0f;
    for (int i = 0; i < nsh; i++) {
        float ki = fmaxf(sc->p.mergeK * sc->shapes[i].merge, 1.0f);
        if (ki > kmax) kmax = ki;
    }
    float bx[FG_MAX_SHAPES], by[FG_MAX_SHAPES], bmy[FG_MAX_SHAPES];
    for (int i = 0; i < nsh; i++) {
        bx[i] = bound_field(&sc->shapes[i], &sc->p, kmax, 0);
        by[i] = bound_field(&sc->shapes[i], &sc->p, kmax, 1);
        bmy[i] = bound_material(&sc->shapes[i], 1);
    }

    const int ss = sc->p.ss > 0 ? sc->p.ss : 1;
    const float inv_ss = 1.0f / (float)ss;

    for (int sy0 = 0; sy0 < h; sy0 += FG_STRIPE_H) {
        int sh = h - sy0;
        if (sh > FG_STRIPE_H) sh = FG_STRIPE_H;

        for (int sy = 0; sy < sh; sy++) {
            int vy = sy0 + sy;
            float dev_y = (float)(y0 + vy) + 0.5f;
            float py = dev_y;
            unsigned short *row = &s_stripe[sy * w];

            unsigned char fidx[FG_MAX_SHAPES], midx[FG_MAX_SHAPES];
            int fn = 0, mn = 0;
            int gx0 = w, gx1 = -1;
            for (int i = 0; i < nsh; i++) {
                const FGShape *s = &sc->shapes[i];
                float dy = fabsf(py - s->y);
                if (dy <= bmy[i]) midx[mn++] = (unsigned char)i;
                if (dy > by[i]) continue;
                fidx[fn++] = (unsigned char)i;
                int a = (int)floorf(s->x - bx[i]) - x0;
                int bxx = (int)floorf(s->x + bx[i]) + 1 - x0;
                if (a < gx0) gx0 = a;
                if (bxx > gx1) gx1 = bxx;
            }
            if (!s_opt_rowlist) {
                fn = mn = nsh;
                for (int i = 0; i < nsh; i++) { fidx[i] = (unsigned char)i; midx[i] = (unsigned char)i; }
                gx0 = 0;
                gx1 = w - 1;
            }
            if (gx0 < 0) gx0 = 0;
            if (gx1 > w - 1) gx1 = w - 1;

            for (int vx = 0; vx < w; vx++) {
                float dev_x = (float)(x0 + vx) + 0.5f;
                float r, g, b;
                if (vx < gx0 || vx > gx1) {
                    fg_tex_lod(bg, dev_x, dev_y, 0.0f, &r, &g, &b);
                } else if (ss == 1) {
                    fg_shade_pixel_sub(sc, bg, fidx, fn, midx, mn, s_opt_interior, dev_x, dev_y, &r, &g, &b);
                } else {
                    /* The library's DPR: shade ss*ss samples inside this CSS pixel and resolve, which is what the
                       compositor does with a dpr-times-larger backing store. Geometry is untouched. */
                    float ar = 0, ag = 0, ab = 0;
                    for (int sj = 0; sj < ss; sj++) {
                        float sy2 = (float)(y0 + vy) + ((float)sj + 0.5f) * inv_ss;
                        for (int si = 0; si < ss; si++) {
                            float sx2 = (float)(x0 + vx) + ((float)si + 0.5f) * inv_ss;
                            float tr, tg, tb;
                            fg_shade_pixel_sub(sc, bg, fidx, fn, midx, mn, s_opt_interior, sx2, sy2, &tr, &tg, &tb);
                            ar += tr; ag += tg; ab += tb;
                        }
                    }
                    float inv = inv_ss * inv_ss;
                    r = ar * inv; g = ag * inv; b = ab * inv;
                }
                if (s_f32) {
                    float *o = &s_f32[((size_t)vy * (size_t)w + (size_t)vx) * 3];
                    o[0] = r;
                    o[1] = g;
                    o[2] = b;
                }
                row[vx] = pack565(r, g, b, vx, vy);
            }
        }
        if (flush) flush(0, sy0, w, sh, s_stripe, user);
    }
}
