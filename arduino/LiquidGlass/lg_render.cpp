#include "lg_render.h"
#include "lg_field.h"
#include "lg_bg.h"
#include "lg_rays.h"

static uint16_t s_stripe[LG_MAX_WIDTH * LG_STRIPE_H];

static inline fx luma(fx r, fx g, fx b) {
    return fxm(r, FX(0.2126)) + fxm(g, FX(0.7152)) + fxm(b, FX(0.0722));
}

// ---------------------------------------------------------------------------
// 565 packing
// ---------------------------------------------------------------------------
//
// A dusk gradient across 32 blue levels bands badly, and the banding lands
// exactly where the lens is compressing the background — right at the rim,
// where it reads as a rendering fault rather than a display limit. A 4x4
// ordered dither costs one add and buys back most of the missing depth.

static const uint8_t kBayer[16] = {
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5
};

static inline uint16_t pack565(fx r, fx g, fx b, int x, int y) {
    int32_t d = (int32_t)kBayer[((y & 3) << 2) | (x & 3)] * 4096 + 2048;
    int32_t ri = (int32_t)((((int64_t)fxsat(r) * 31) + d) >> 16);
    int32_t gi = (int32_t)((((int64_t)fxsat(g) * 63) + d) >> 16);
    int32_t bi = (int32_t)((((int64_t)fxsat(b) * 31) + d) >> 16);
    if (ri > 31) ri = 31;
    if (gi > 63) gi = 63;
    if (bi > 31) bi = 31;
    // Native byte order: the ray compositor reads these back and adds to them.
    // Any panel-side swap happens once per stripe, after that is done.
    return (uint16_t)((ri << 11) | (gi << 5) | bi);
}

// ---------------------------------------------------------------------------
// the lens
// ---------------------------------------------------------------------------
//
// A line-for-line port of main() in GLASS_FRAG, with two deliberate omissions:
//
//   frost   - zero in every shipped preset. Heavy backdrop blur is the look
//             Apple moved away from and it is what makes a render read as
//             fogged plastic; there is no reason to spend a 16-tap disk blur
//             here to reproduce something the desktop build turns off.
//   shadow  - also off upstream. The rim hairline carries the separation.
//
// The spectral taps are not omitted, they are moved: see lg_rays.h.

void lg_shade_pixel(const LGScene *sc, fx px, fx py, fx *outR, fx *outG, fx *outB) {
    const LGParams *P = &sc->p;

    fx bgr, bgg, bgb;
    lg_bg_rgb(px, py, &bgr, &bgg, &bgb);

    fx d = lg_field(sc, px, py);
    if (d > FX(2.0)) { *outR = bgr; *outG = bgg; *outB = bgb; return; }

    fx nx, ny;
    lg_field_normal(sc, px, py, &nx, &ny);

    fx inside = fxsat(fxd(-d, P->edge));
    fx rim = FX_ONE - inside;

    // Circular lens profile: calm centre, asymptotically steep at the edge.
    fx prof = FX_ONE - fxsqrt(fxmax(FX_ONE - fxm(rim, rim), 0));

    fx offx = fxm(fxm(nx, prof), P->bend);
    fx offy = fxm(fxm(ny, prof), P->bend);
    offx = fxm(offx, P->aniso);

    fx r, g, b;
    lg_bg_rgb(px + offx, py + offy, &r, &g, &b);

    // glass concentrates colour a little as it bends it
    fx rl = luma(r, g, b);
    fx s1 = FX_ONE + P->sat;
    r = fxsat(rl + fxm(r - rl, s1));
    g = fxsat(rl + fxm(g - rl, s1));
    b = fxsat(rl + fxm(b - rl, s1));

    // ---- adaptive body tint ----------------------------------------------
    fx amb = lg_bg_ambient(px, py);
    fx pol = fxsmoothstep(FX(0.16), FX(0.60), amb);
    fx adaptR = fxlerp(FX(0.055), FX_ONE, pol);
    fx adaptG = fxlerp(FX(0.060), FX_ONE, pol);
    fx adaptB = fxlerp(FX(0.072), FX_ONE, pol);

    fx neutral = fxlerp(rl, FX_ONE, FX(0.35));
    fx bodyR = fxlerp(neutral, adaptR, P->adapt);
    fx bodyG = fxlerp(neutral, adaptG, P->adapt);
    fx bodyB = fxlerp(neutral, adaptB, P->adapt);

    r = fxlerp(r, bodyR, P->opacity);
    g = fxlerp(g, bodyG, P->opacity);
    b = fxlerp(b, bodyB, P->opacity);

    // materialAt() costs an exp per shape, so skip it entirely unless some
    // shape actually carries a tint or a rim override — the common case.
    fx tr = 0, tg = 0, tb = 0, ta = 0, rimb = 0;
    for (int i = 0; i < sc->count; i++) {
        if (sc->shapes[i].tintA != 0 || sc->shapes[i].rim != 0) {
            lg_material_at(sc, px, py, &tr, &tg, &tb, &ta, &rimb);
            break;
        }
    }
    if (ta > 0) {
        fx a = fxsat(ta);
        r = fxlerp(r, tr, a);
        g = fxlerp(g, tg, a);
        b = fxlerp(b, tb, a);
    }

    // ---- lighting ---------------------------------------------------------
    // Lift the 2D gradient into a bevel normal: flat on top, rolling over to
    // face outward at the silhouette.
    fx Nx = fxm(nx, prof), Ny = fxm(ny, prof);
    fx Nz = fxmax(FX_ONE - prof, FX(0.06));
    {
        fx len = fxsqrt(fxm(Nx, Nx) + fxm(Ny, Ny) + fxm(Nz, Nz));
        if (len > 0) { Nx = fxd(Nx, len); Ny = fxd(Ny, len); Nz = fxd(Nz, len); }
    }

    // half vectors against V = (0,0,1)
    fx hkx = P->keyX, hky = P->keyY, hkz = P->keyZ + FX_ONE;
    {
        fx len = fxsqrt(fxm(hkx, hkx) + fxm(hky, hky) + fxm(hkz, hkz));
        if (len > 0) { hkx = fxd(hkx, len); hky = fxd(hky, len); hkz = fxd(hkz, len); }
    }
    fx hfx_ = P->fillX, hfy = P->fillY, hfz = P->fillZ + FX_ONE;
    {
        fx len = fxsqrt(fxm(hfx_, hfx_) + fxm(hfy, hfy) + fxm(hfz, hfz));
        if (len > 0) { hfx_ = fxd(hfx_, len); hfy = fxd(hfy, len); hfz = fxd(hfz, len); }
    }

    // Only the rolled edge reflects, and only its outer part: a flat top
    // facing the viewer would wash the whole interior with constant sheen.
    fx bevel = fxsmoothstep(FX(0.14), FX(0.72), prof);
    int32_t shineI = fxround_i(P->shine);
    if (shineI < 1) shineI = 1;
    fx dk = fxsat(fxm(hkx, Nx) + fxm(hky, Ny) + fxm(hkz, Nz));
    fx df = fxsat(fxm(hfx_, Nx) + fxm(hfy, Ny) + fxm(hfz, Nz));
    fx sKey  = fxm(fxpowi(dk, shineI), bevel);
    fx sFill = fxm(fxpowi(df, (shineI * 65) / 100 + 1), bevel);
    fx fres  = fxpow5(FX_ONE - fxsat(Nz));
    fres = fxm(fres, FX_ONE - fxsat(Nz));               // ^6

    // over dark content the highlights have to work harder to read
    fx lift = fxlerp(FX(1.25), FX(0.85), fxsmoothstep(FX(0.12), FX(0.62), amb));
    fx spec = fxm(fxm(sKey + fxm(FX(0.38), sFill), P->spec), lift);

    // compression shading near the rim defines the edge — no painted ring
    fx darken = FX_ONE - fxm(FX(0.06), fxpow5(rim));
    r = fxm(r, darken); g = fxm(g, darken); b = fxm(b, darken);

    fx add = spec + fxm(fxm(fxm(fres, FX(0.04)), P->spec), lift);
    r += add; g += add; b += add;

    // A hairline, not a halo: roughly one pixel either side of the silhouette,
    // brightest where it faces the key light.
    fx t = fxd(d + FX(0.85), FX(0.60));
    fx line = fxexp_neg(fxm(t, t));
    fx kx = P->keyX, ky = P->keyY;
    fxnormalize(&kx, &ky);
    fx facing = FX(0.34) + fxm(FX(0.66), fxmax(fxm(nx, kx) + fxm(ny, ky), 0));
    fx hair = fxm(fxm(fxm(line, P->rimLit), facing), lift);
    r += hair; g += hair; b += hair;

    if (rimb != 0) {
        fx e = fxm(line, fxm(rimb, FX(0.45) + fxm(FX(0.40), fxabs(fxm(nx, kx) + fxm(ny, ky)))));
        r += e; g += e; b += e;
    }

    // interactive illumination: pressing lights the glass from the touch point
    if (P->touchA > FX(0.001)) {
        fx td = fxhypot(px - P->touchX, py - P->touchY);
        fx t2 = fxm(fxm(P->touchA, fxexp_neg(fxd(td, FX(70.0)))), inside);
        r += t2; g += t2; b += t2;
    }

    r = fxsat(r); g = fxsat(g); b = fxsat(b);

    fx alpha = fxsmoothstep(FX(1.0), FX(-1.0), d);
    *outR = fxlerp(bgr, r, alpha);
    *outG = fxlerp(bgg, g, alpha);
    *outB = fxlerp(bgb, b, alpha);
}

// ---------------------------------------------------------------------------
// frame
// ---------------------------------------------------------------------------

void lg_render(const LGScene *sc, int w, int h, lg_flush_fn flush, void *user) {
    if (w > LG_MAX_WIDTH) w = LG_MAX_WIDTH;
    lg_bg_set_viewport(w, h);
    lg_rays_trace(sc, w, h);

    // Per-row bounds: outside this span nothing can be glass, so the row skips
    // five field evaluations per pixel and just draws the wall. On a typical
    // scene that is 80% of the frame.
    fx bound[LG_MAX_SHAPES];
    for (int i = 0; i < sc->count; i++) bound[i] = lg_shape_bound(&sc->shapes[i], &sc->p);

    for (int y0 = 0; y0 < h; y0 += LG_STRIPE_H) {
        int sh = h - y0;
        if (sh > LG_STRIPE_H) sh = LG_STRIPE_H;

        for (int sy = 0; sy < sh; sy++) {
            int y = y0 + sy;
            fx fy = fxfromint(y) + FX_HALF;
            uint16_t *row = &s_stripe[sy * w];

            int gx0 = w, gx1 = -1;
            for (int i = 0; i < sc->count; i++) {
                const LGShape *s = &sc->shapes[i];
                fx dy = fxabs(fy - s->y);
                if (dy > bound[i]) continue;
                int a = fxfloor_i(s->x - bound[i]);
                int bx = fxfloor_i(s->x + bound[i]) + 1;
                if (a < gx0) gx0 = a;
                if (bx > gx1) gx1 = bx;
            }
            if (gx0 < 0) gx0 = 0;
            if (gx1 > w - 1) gx1 = w - 1;

            lg_bg_prepare_row(fy);
            for (int x = 0; x < w; x++) {
                fx fxp = fxfromint(x) + FX_HALF;
                fx r, g, b;
                if (x < gx0 || x > gx1) {
                    lg_bg_rgb_row(fxp, &r, &g, &b);
                } else {
                    lg_shade_pixel(sc, fxp, fy, &r, &g, &b);
                }
                row[x] = pack565(r, g, b, x, y);
            }
        }

        // The rays sit on top of the composed glass: they are light leaving the
        // edge, not something the lens bends.
        lg_rays_draw(s_stripe, w, y0, sh, &sc->p);

#if LG_SWAP_BYTES
        for (int i = 0, n = w * sh; i < n; i++) {
            uint16_t c = s_stripe[i];
            s_stripe[i] = (uint16_t)((c >> 8) | (c << 8));
        }
#endif
        if (flush) flush(0, y0, w, sh, s_stripe, user);
    }
}
