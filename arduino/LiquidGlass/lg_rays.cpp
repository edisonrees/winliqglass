#include "lg_rays.h"
#include "lg_field.h"

// Path points are stored in Q9.6: 1/64px is finer than the Wu blend can
// resolve, and int16 keeps the pool at 6 bytes a point.
#define PT_SHIFT 6
#define PT_ONE   (1 << PT_SHIFT)

struct LGRayPt {
    int16_t x, y;
    uint8_t inten;   // 0..255
    uint8_t fan;     // accumulated dispersion, 0..255
};

static LGRayPt s_pts[LG_RAY_COUNT * LG_RAY_STEPS];
static uint8_t s_len[LG_RAY_COUNT];
static int     s_rays = 0;

int lg_rays_count(void) { return s_rays; }

// The shader's spectrum(): three narrow Gaussians centred at t = 0.16, 0.50
// and 0.84, sampled at eight wavelengths. Evaluated once, here, rather than
// per pixel — these are the same numbers the GPU build recombines.
static const uint8_t kSpectrum[8][3] = {
    {   0,  23, 226 },
    {   1,  74, 253 },
    {   8, 164, 190 },
    {  33, 243,  97 },
    {  97, 243,  33 },
    { 190, 164,   8 },
    { 253,  74,   1 },
    { 226,  23,   0 }
};

static void spectral(fx t, uint8_t *r, uint8_t *g, uint8_t *b) {
    fx i = fxm(fxsat(t), FX(7.0));
    int i0 = fxfloor_i(i);
    if (i0 > 6) i0 = 6;
    fx f = i - fxfromint(i0);
    for (int c = 0; c < 3; c++) {
        int32_t a = kSpectrum[i0][c], bb = kSpectrum[i0 + 1][c];
        int32_t v = a + fxround_i(fxm(fxfromint(bb - a), f));
        uint8_t out = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
        if (c == 0) *r = out; else if (c == 1) *g = out; else *b = out;
    }
}

// ---------------------------------------------------------------------------
// seeding
// ---------------------------------------------------------------------------

// A point on the shape's own outline at parameter t in [0,1). This is only a
// starting guess; the projection below drags it onto the merged silhouette,
// which is what the light actually runs along.
static void outline_point(const LGShape *s, fx t, fx *ox, fx *oy) {
    fx lx, ly;
    if (s->kind == LG_RRECT) {
        // Walk the perimeter so seeds stay evenly spaced on long sides. The
        // four runs below cover 2hw + 2hh + 2hw + 2hh, so the parameter has to
        // span all of it — half of it seeds two edges and leaves the other two
        // dark, which is exactly what "all around" is not.
        fx per = 4 * (s->hw + s->hh);
        fx d = fxm(t, per);
        if (d < 2 * s->hw)              { lx = -s->hw + d;              ly = -s->hh; }
        else if (d < 2 * s->hw + 2 * s->hh) { lx = s->hw;               ly = -s->hh + (d - 2 * s->hw); }
        else if (d < 4 * s->hw + 2 * s->hh) { lx = s->hw - (d - 2 * s->hw - 2 * s->hh); ly = s->hh; }
        else                            { lx = -s->hw;                  ly = s->hh - (d - 4 * s->hw - 2 * s->hh); }
    } else {
        fx r = s->hw;
        lx = fxm(r, fxcos(t));
        ly = fxm(r, fxsin(t));
    }
    if (s->rot != 0) {
        // shape SDFs rotate the sample point by +rot, so the outline rotates back
        fx c = fxcos(s->rot), sn = fxsin(s->rot);
        fx rx = fxm(c, lx) + fxm(sn, ly);
        fx ry = -fxm(sn, lx) + fxm(c, ly);
        lx = rx; ly = ry;
    }
    *ox = s->x + lx;
    *oy = s->y + ly;
}

static fx shape_perimeter(const LGShape *s) {
    if (s->kind == LG_RRECT) return 4 * (s->hw + s->hh);
    return fxm(FX(6.2831853), s->hw);
}

// ---------------------------------------------------------------------------
// tracing
// ---------------------------------------------------------------------------

void lg_rays_trace(const LGScene *sc, int viewW, int viewH) {
    s_rays = 0;
    if (sc->count == 0) return;

    const LGParams *P = &sc->p;
    const fx edge = P->edge;
    const fx core = -fxm(edge, FX(0.5));           // middle of the pipe
    const fx swing = fxm(edge, fxm(P->rayBounce, FX(0.5)));
    const fx stepLen = fxmax(fxm(edge, FX(0.62)), FX(2.0));

    // Key light in screen space (+y down). Coupling is strongest where the
    // surface faces it, which is what stops the ring reading as a painted
    // outline.
    fx kx = P->keyX, ky = P->keyY;
    fxnormalize(&kx, &ky);

    fx totalPer = 0;
    for (int i = 0; i < sc->count; i++) totalPer += shape_perimeter(&sc->shapes[i]);
    if (totalPer <= 0) return;

    const fx viewWfx = fxfromint(viewW);
    const fx viewHfx = fxfromint(viewH);

    for (int si = 0; si < sc->count && s_rays < LG_RAY_COUNT; si++) {
        const LGShape *sh = &sc->shapes[si];
        if (sh->kind == LG_RING) continue;          // selection rings do not glow

        // seeds proportional to how much of the outline this shape owns
        int quota = fxround_i(fxm(fxfromint(LG_RAY_COUNT), fxd(shape_perimeter(sh), totalPer)));
        if (quota < 1) quota = 1;

        for (int q = 0; q < quota && s_rays < LG_RAY_COUNT; q++) {
            fx t = fxd(fxfromint(q), fxfromint(quota));
            t += P->rayPhase;                        // drifts the whole ring
            t &= 0xFFFF;

            fx px, py;
            outline_point(sh, t, &px, &py);
            lg_field_project(sc, &px, &py, core, 5);

            // A seed that would not settle is one whose shape is buried inside
            // the merge; there is no rim there to run along, so drop it.
            fx d = lg_field(sc, px, py);
            if (fxabs(d - core) > fxm(edge, FX(0.45))) continue;

            fx nx, ny;
            lg_field_normal(sc, px, py, &nx, &ny);

            // Coupling is strongest where the surface faces the key light, but
            // it is never zero: the wall behind lights the whole rim. The
            // floor is what keeps the far side of the ring alive.
            fx facing = FX(0.45) + fxm(FX(0.55), fxmax(fxm(nx, kx) + fxm(ny, ky), 0));
            fx inten = facing;

            // Light entering the pipe propagates both ways round it. Seeding
            // alternate directions costs nothing and is what makes the ring
            // even instead of bright on the lit side and starved opposite.
            int sgn = (q & 1) ? -1 : 1;

            fx bouncePhase = fxm(t, FX(3.0));        // decorrelate neighbours
            fx fan = 0;
            fx prevTx = sgn > 0 ? -ny : ny;
            fx prevTy = sgn > 0 ?  nx : -nx;

            uint8_t n = 0;
            LGRayPt *out = &s_pts[s_rays * LG_RAY_STEPS];

            for (int step = 0; step < LG_RAY_STEPS; step++) {
                if (px < FX(-8.0) || py < FX(-8.0) ||
                    px > viewWfx + FX(8.0) || py > viewHfx + FX(8.0)) break;

                out[n].x = (int16_t)((px * PT_ONE) >> FX_SHIFT);
                out[n].y = (int16_t)((py * PT_ONE) >> FX_SHIFT);
                out[n].inten = (uint8_t)fxround_i(fxm(fxsat(inten), FX(255.0)));
                out[n].fan = (uint8_t)fxround_i(fxm(fxsat(fan), FX(255.0)));
                n++;

                // travel along the pipe
                fx tx = sgn > 0 ? -ny : ny;
                fx ty = sgn > 0 ?  nx : -nx;
                px += fxm(tx, stepLen);
                py += fxm(ty, stepLen);

                // ...and bounce between its walls: the isocontour the ray is
                // pinned to sweeps in and out, which is the internal reflection
                // rendered as a path rather than solved as an angle.
                bouncePhase += FX(0.17);
                fx target = core + fxm(swing, fxsin(bouncePhase));
                lg_field_project(sc, &px, &py, target, 2);

                lg_field_normal(sc, px, py, &nx, &ny);

                // Turning is where wavelengths separate, so the fan grows with
                // |cross| of successive tangents plus a floor for path length.
                fx ntx = sgn > 0 ? -ny : ny;
                fx nty = sgn > 0 ?  nx : -nx;
                fx cross = fxabs(fxm(prevTx, nty) - fxm(prevTy, ntx));
                fan += fxm(cross, FX(1.30)) + FX(0.045);
                if (fan > FX_ONE) fan = FX_ONE;
                prevTx = ntx; prevTy = nty;

                inten = fxm(inten, P->rayDecay);
                if (inten < FX(0.012)) break;
            }

            if (n >= 2) { s_len[s_rays] = n; s_rays++; }
        }
    }
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------

// Additive stamp in 5/6/5. Unpacking and repacking per pixel is cheaper than
// carrying an 8-bit-per-channel stripe, and because the rays only ever add
// light the rounding never accumulates into a visible step.
static inline void add_px(uint16_t *buf, int idx,
                          uint32_t r, uint32_t g, uint32_t b, uint32_t cov) {
    if (cov == 0) return;
    uint16_t c = buf[idx];
    uint32_t cr = (c >> 11) & 0x1F, cg = (c >> 5) & 0x3F, cb = c & 0x1F;
    cr += (r * cov) >> 13;      // r:0..255 * cov:0..256 -> 0..31
    cg += (g * cov) >> 12;      // -> 0..63
    cb += (b * cov) >> 13;
    if (cr > 31) cr = 31;
    if (cg > 63) cg = 63;
    if (cb > 31) cb = 31;
    buf[idx] = (uint16_t)((cr << 11) | (cg << 5) | cb);
}

// Wu-style antialiased additive line, clipped to the stripe. Antialiasing is
// not vanity here: at 240px across, an aliased ray reads as a dotted line and
// the whole effect falls apart.
static void add_line(uint16_t *buf, int w, int yTop, int h,
                     fx x0, fx y0, fx x1, fx y1,
                     uint32_t r, uint32_t g, uint32_t b, uint32_t gain) {
    if (gain == 0) return;
    fx dx = x1 - x0, dy = y1 - y0;
    fx adx = fxabs(dx), ady = fxabs(dy);
    int steps;
    fx sx, sy;

    if (adx >= ady) {
        steps = fxround_i(adx);
        if (steps < 1) steps = 1;
        sx = dx > 0 ? FX_ONE : -FX_ONE;
        sy = fxd(dy, fxfromint(steps));
    } else {
        steps = fxround_i(ady);
        if (steps < 1) steps = 1;
        sy = dy > 0 ? FX_ONE : -FX_ONE;
        sx = fxd(dx, fxfromint(steps));
    }
    if (steps > 64) steps = 64;                 // a traced segment is never this long

    fx px = x0, py = y0;
    for (int i = 0; i <= steps; i++) {
        int ix = fxfloor_i(px);
        int iy = fxfloor_i(py) - yTop;
        fx fxr = fxfrac(px), fyr = fxfrac(py);
        uint32_t wx1 = (uint32_t)(fxr >> 8), wx0 = 256 - wx1;
        uint32_t wy1 = (uint32_t)(fyr >> 8), wy0 = 256 - wy1;

        if (iy >= 0 && iy < h) {
            if (ix >= 0 && ix < w)     add_px(buf, iy * w + ix,     r, g, b, (wx0 * wy0 * gain) >> 16);
            if (ix + 1 >= 0 && ix + 1 < w) add_px(buf, iy * w + ix + 1, r, g, b, (wx1 * wy0 * gain) >> 16);
        }
        if (iy + 1 >= 0 && iy + 1 < h) {
            if (ix >= 0 && ix < w)     add_px(buf, (iy + 1) * w + ix,     r, g, b, (wx0 * wy1 * gain) >> 16);
            if (ix + 1 >= 0 && ix + 1 < w) add_px(buf, (iy + 1) * w + ix + 1, r, g, b, (wx1 * wy1 * gain) >> 16);
        }
        px += sx;
        py += sy;
    }
}

void lg_rays_draw(uint16_t *buf, int w, int yTop, int h, const LGParams *p) {
    if (s_rays == 0 || p->rayGain <= 0) return;

    // Keep total brightness roughly independent of how many wavelengths the
    // board can afford, so LG_RAY_BANDS is a quality knob and not an exposure
    // control.
    const fx bandScale = fxd(FX(3.4), fxfromint(LG_RAY_BANDS));
    const fx yTopFx = fxfromint(yTop);
    const fx yBotFx = fxfromint(yTop + h);

    uint8_t bandR[LG_RAY_BANDS], bandG[LG_RAY_BANDS], bandB[LG_RAY_BANDS];
    fx bandOff[LG_RAY_BANDS];
    for (int b = 0; b < LG_RAY_BANDS; b++) {
        fx t = fxd(fxfromint(2 * b + 1), fxfromint(2 * LG_RAY_BANDS));
        spectral(t, &bandR[b], &bandG[b], &bandB[b]);
        // The response is a luminance curve, so summed across every band it
        // carries 25% more green than red or blue and the whole ring goes
        // olive. The shader dodges this by amplifying only the chromatic part
        // of a residual; adding light directly, the fix is to trim green back
        // until the sum is neutral and let the separation supply the colour.
        bandG[b] = (uint8_t)(((uint32_t)bandG[b] * 205) >> 8);
        // short wavelengths sit deeper in the pipe, long ones ride the skin
        bandOff[b] = fxm(t - FX_HALF, FX(2.0));
    }

    for (int ri = 0; ri < s_rays; ri++) {
        const LGRayPt *pt = &s_pts[ri * LG_RAY_STEPS];
        int n = s_len[ri];
        for (int i = 0; i + 1 < n; i++) {
            fx ax = ((fx)pt[i].x << FX_SHIFT) >> PT_SHIFT;
            fx ay = ((fx)pt[i].y << FX_SHIFT) >> PT_SHIFT;
            fx bx = ((fx)pt[i + 1].x << FX_SHIFT) >> PT_SHIFT;
            fx by = ((fx)pt[i + 1].y << FX_SHIFT) >> PT_SHIFT;

            fx fan = fxm(fxfromint(pt[i].fan), FX(1.0 / 255.0));
            fx spread = fxm(fan, p->rayDisp);

            // cheap conservative reject: the fan can only push a segment this
            // far off its own bounding box
            fx lo = (ay < by ? ay : by) - spread - FX(2.0);
            fx hi = (ay > by ? ay : by) + spread + FX(2.0);
            if (hi < yTopFx || lo > yBotFx) continue;

            // across-the-pipe direction, which is the field normal to within
            // the step we just took
            fx ex = bx - ax, ey = by - ay;
            fxnormalize(&ex, &ey);
            fx ox = -ey, oy = ex;

            fx inten = fxm(fxfromint(pt[i].inten), FX(1.0 / 255.0));
            fx amp = fxm(fxm(inten, p->rayGain), bandScale);
            uint32_t gain = (uint32_t)fxround_i(fxm(fxsat(amp), FX(256.0)));
            if (gain > 256) gain = 256;
            if (gain == 0) continue;

            for (int b = 0; b < LG_RAY_BANDS; b++) {
                fx d = fxm(bandOff[b], spread);
                fx dx = fxm(ox, d), dy = fxm(oy, d);
                add_line(buf, w, yTop, h,
                         ax + dx, ay + dy, bx + dx, by + dy,
                         bandR[b], bandG[b], bandB[b], gain);
            }
        }
    }
}
