// A line-for-line float port of shaders.py GLASS_FRAG, with the WebGL build's two-lobe hairline
// (webgl/winliqglass.js lines 276-288). GLSL on the left, C++ on the right; where a line moved, the comment
// says why. Nothing here is an approximation of the shader — the only omission is blurSample(), because the Movie
// preset sets frost = 0 and the branch is `if (uFrost > 0.5)`, so it is unreachable (arduino/README.md).
#include "fg_glass.h"
#include <math.h>
#include <string.h>

static unsigned s_prof;
void fg_prof_set(unsigned mask) { s_prof = mask; }

// ---------------------------------------------------------------------------
// GLSL builtins
// ---------------------------------------------------------------------------
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smoothstepf(float e0, float e1, float x)
{
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
static inline float luma(float r, float g, float b) { return r * 0.2126f + g * 0.7152f + b * 0.0722f; }

// shaders.py: `adapt += (ambCol - vec3(amb)) * 0.15` -- "a little of the mip's hue leaks through too". The WebGL
// build takes only the ambient's luma and has no such line. Worth 0.005/255 against the ground truth either way
// (measured), so this is a source-fidelity switch, not a look decision.
static const float kHueLeak = FG_AMBIENT_HUE ? 0.15f : 0.0f;

// ---------------------------------------------------------------------------
// SDFs — shaders.py sdCircle / sdRRect / sdTri / sdPentagon / squircleAmt
// ---------------------------------------------------------------------------
// shaders.py makes this a compile-time #define on purpose -- "it is a statement about the shape language, not a
// per-frame parameter". FG_CORNERS_CONTINUOUS (fg_glass.h) is the same switch, and 0 is the WebGL build's plain
// circular corner: winliqglass.js `sdRRect(vec2 p, vec2 b, float r)` has no squircle branch at all.
#define SQUIRCLE (FG_CORNERS_CONTINUOUS ? 1.0f : 0.0f)

static inline float squircleAmt(float bx, float by, float r)
{
    float t = r / fmaxf(fminf(bx, by), 1e-4f);
    return SQUIRCLE * (1.0f - smoothstepf(0.72f, 0.99f, t));
}

static float sdRRect(float px, float py, float bx, float by, float r, float sq)
{
    r = fminf(r, fminf(bx, by));
    float qx = fabsf(px) - bx + r;
    float qy = fabsf(py) - by + r;
    float mx = fmaxf(qx, 0.0f), my = fmaxf(qy, 0.0f);
    /* Inside the rrect both q components are <= 0, so m is (0,0), length(m) is 0 and the whole expression is
       max(qx, qy) - r. That is the shape of nearly every pixel of a card or a panel, and it was paying for a
       square root on each one. Exactly the same value, not an approximation: sqrtf(0) is 0 and 0 + x is x. */
    float d;
    if (mx == 0.0f && my == 0.0f) d = fmaxf(qx, qy) - r;
    else d = sqrtf(mx * mx + my * my) + fminf(fmaxf(qx, qy), 0.0f) - r;
    if (sq > 0.001f && mx > 0.0f && my > 0.0f) {
        float m2x = mx * mx, m2y = my * my;
        float s = m2x * m2x + m2y * m2y;            // |m|4^4
        float l4 = sqrtf(sqrtf(s));
        float g = sqrtf(m2x * m2x * m2x + m2y * m2y * m2y);   // sqrt(dot(m2*m2, m2))
        d = mixf(d, (l4 - r) * s / fmaxf(l4 * g, 1e-6f), sq);
    }
    return d;
}

static float sdTri(float px, float py, float s)
{
    const float k = 1.7320508f;
    px = fabsf(px) - s;
    py = py + s / k;
    if (px + k * py > 0.0f) {
        float nx = (px - k * py) * 0.5f;
        float ny = (-k * px - py) * 0.5f;
        px = nx; py = ny;
    }
    px -= clampf(px, -2.0f * s, 0.0f);
    float len = sqrtf(px * px + py * py);
    return -len * (py > 0.0f ? 1.0f : (py < 0.0f ? -1.0f : 0.0f));
}

static float sdPentagon(float px, float py, float r)
{
    const float kx = 0.809016994f, ky = 0.587785252f, kz = 0.726542528f;
    px = fabsf(px);
    float dp = fminf(-kx * px + ky * py, 0.0f);
    px -= 2.0f * dp * -kx;
    py -= 2.0f * dp * ky;
    dp = fminf(kx * px + ky * py, 0.0f);
    px -= 2.0f * dp * kx;
    py -= 2.0f * dp * ky;
    px -= clampf(px, -r * kz, r * kz);
    py -= r;
    float len = sqrtf(px * px + py * py);
    return len * (py > 0.0f ? 1.0f : (py < 0.0f ? -1.0f : 0.0f));
}

static float shapeSDF(const FGShape *s, float pix_x, float pix_y)
{
    float px = pix_x - s->x, py = pix_y - s->y;
    if (s->rot != 0.0f) {
        float c = cosf(s->rot), sn = sinf(s->rot);
        float rx = c * px - sn * py;
        float ry = sn * px + c * py;
        px = rx; py = ry;
    }
    switch (s->kind) {
    case FG_CIRCLE: return sqrtf(px * px + py * py) - s->hw;
    case FG_RRECT:  return sdRRect(px, py, s->hw, s->hh, s->rad, squircleAmt(s->hw, s->hh, s->rad));
    case FG_TRI:    return sdTri(px, -py, s->hw * 0.82f) - s->rad;
    case FG_PENT:   return sdPentagon(px, -py, s->hw * 0.85f) - s->rad;
    default:        return fabsf(sqrtf(px * px + py * py) - s->hw) - s->rad;   /* 3: selection ring */
    }
}

static inline float sminf(float a, float b, float k)
{
    float h = clampf(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return mixf(b, a, h) - k * h * (1.0f - h);
}

// The active-shape list (optimisation 1). Passing every index reproduces the shader's `for i < uCount` exactly.
//
// `sd` (optional) keeps each shape's own SDF, so materialAt() does not have to evaluate the same SDFs at the same
// point a second time. The shader has no reason to care — the GPU runs both loops in parallel — but on the P4 the
// second pass over the shapes was 9-14 % of the frame (the ablation profile in arduino/README.md).
static float field_sub(const FGScene *sc, const unsigned char *idx, int n, float px, float py, float *sd)
{
    if (n == 1) {
        /* smin(1e6, b, k) with k >= 1 is exactly b: h clamps to 0, so mix(b, 1e6, 0) = b and the k*h*(1-h) term
           is 0. Worth saying out loud because it is the shape of every widget bake — one pill, one card, one
           panel — and it is five field() evaluations per pixel. */
        float d0 = shapeSDF(&sc->shapes[idx[0]], px, py);
        if (sd) sd[0] = d0;
        return d0;
    }
    float d = 1e6f;
    for (int j = 0; j < n; j++) {
        const FGShape *s = &sc->shapes[idx[j]];
        float k = fmaxf(sc->p.mergeK * s->merge, 1.0f);
        float di = shapeSDF(s, px, py);
        if (sd) sd[j] = di;
        d = sminf(d, di, k);
    }
    return d;
}

// exp(-max(di, -30)/22) is a CONSTANT wherever di <= -30 — which is most of the interior of anything bigger than
// a chip — and that was one newlib expf() per pixel for a number that never changes. Evaluated once, at static
// initialisation, with the SAME expression the general path uses: `-fmaxf(di, -30.0f)` is exactly `30.0f` there
// and the divide is the same operation, so it is bit-identical rather than merely close. Not hand-written as a
// literal — a hand-rounded constant would be one ulp out, which is how this kind of fold usually goes wrong.
static const float kWDeepV = expf(-(-30.0f) / 22.0f);

static void materialAt_sub(const FGScene *sc, const unsigned char *midx, int mn,
                           const unsigned char *fidx, int fn, const float *fsd,
                           float px, float py, float *tnt, float *rimb)
{
    float acc[4] = { 0, 0, 0, 0 };
    float racc = 0.0f, ws = 1e-5f;
    /* Both lists are built in ascending shape order and the material list is a superset of the field list
       (fg_render.cpp's material bound is the wider one), so one cursor walks them together. */
    int c = 0;
    for (int j = 0; j < mn; j++) {
        const FGShape *s = &sc->shapes[midx[j]];
        while (c < fn && fidx[c] < midx[j]) {
            c++;
        }
        float di;
        if (fsd && c < fn && fidx[c] == midx[j]) di = fsd[c];
        else di = shapeSDF(s, px, py);
        float w = di <= -30.0f ? kWDeepV : expf(-di / 22.0f);
        acc[0] += w * s->tint[0];
        acc[1] += w * s->tint[1];
        acc[2] += w * s->tint[2];
        acc[3] += w * s->tint[3];
        racc += w * s->rimb;
        ws += w;
    }
    for (int i = 0; i < 4; i++) tnt[i] = acc[i] / ws;
    *rimb = racc / ws;
}

// spectrum(t): the shader's three narrow Gaussians, unchanged.
static inline void spectrum(float t, float *r, float *g, float *b)
{
    float a = (t - 0.84f) * 3.55f, c = (t - 0.50f) * 3.55f, e = (t - 0.16f) * 3.55f;
    *r = expf(-a * a);
    *g = expf(-c * c);
    *b = expf(-e * e);
}

// The refraction loop asks for spectrum() at t = (i + 0.5)/8 for i in 0..7 — eight CONSTANTS, which the GPU happily
// recomputes 24 exp() per fragment because it has the ALUs to burn. The P4 does not: that was 24 newlib expf calls
// per glass pixel, and folding them costs nothing in fidelity (the same eight float triples, computed once) and is
// the single biggest win in the renderer. Same for their sums, which the shader accumulates per pixel.
static float kSpecW[FG_SPECTRAL_TAPS][3];
static float kSpecSum[3];
static float kSpecMoment;        /* 0.5 - tbar(red) = -(0.5 - tbar(blue)); green's tbar is exactly 0.5 */
static void spectrum_tables(void)
{
    if (kSpecSum[0] != 0.0f) return;
    float mom[3] = { 0, 0, 0 };
    for (int i = 0; i < FG_SPECTRAL_TAPS; i++) {
        float t = ((float)i + 0.5f) / (float)FG_SPECTRAL_TAPS;
        spectrum(t, &kSpecW[i][0], &kSpecW[i][1], &kSpecW[i][2]);
        for (int c = 0; c < 3; c++) {
            kSpecSum[c] += kSpecW[i][c];
            mom[c] += kSpecW[i][c] * t;
        }
    }
    /* The first moment of the RED weight, in the shader's own `s = 1 + (0.5 - t) * spread` units. Derived from
       spectrum() rather than written down, so it cannot drift from the weights it summarises. */
    kSpecMoment = 0.5f - mom[0] / kSpecSum[0];
}

static inline void norm3(float *x, float *y, float *z)
{
    float l = sqrtf(*x * *x + *y * *y + *z * *z);
    if (l > 0.0f) { *x /= l; *y /= l; *z /= l; }
}

// blurSample(): the shader's 16-tap golden-angle spiral disk blur with a per-pixel rotation. Ported for exactly one
// surface — the Control Centre panel — where the material alone let the tiles behind read through the segmented rows
// (thickening the glass with opacity or tint is off the table -- the preset is not tuned per surface -- so the
//   shader's own frost dial is the one sanctioned lever). Every other surface
// runs frost = 0 and never enters this function.
//   vec2 o = vec2(cos(a), sin(a)) * r / uRes;  is a uv offset with v running UP, so in this y-down pixel space the
//   y component flips sign. Everything else is the GLSL unchanged.
static void blurSample(const fg_tex_t *bg, float ux, float uy, float radius, float sx, float sy,
                       float *outR, float *outG, float *outB)
{
    float lod = log2f(1.0f + radius * 0.5f);
    float h = sinf(sx * 12.9898f + sy * 78.233f) * 43758.5453f;
    float rot = 6.2831853f * (h - floorf(h));
    float accR = 0.0f, accG = 0.0f, accB = 0.0f;
    for (int i = 0; i < 16; i++) {
        float fi = (float)i;
        float a = fi * 2.3999632f + rot;
        float r = radius * sqrtf((fi + 0.5f) / 16.0f);
        float sr, sg, sb;
        fg_tex_lod(bg, ux + cosf(a) * r, uy - sinf(a) * r, lod, &sr, &sg, &sb);
        accR += sr; accG += sg; accB += sb;
    }
    *outR = accR / 16.0f;
    *outG = accG / 16.0f;
    *outB = accB / 16.0f;
}

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------
// `pix` in the shader is (vUV.x, 1 - vUV.y) * uRes: x right, y DOWN. Screen pixels are already that, and because the
// window renders 1:1 into the background, bgUV's affine is the identity and `offUV` in uv becomes a pixel offset with
// the y sign flipped back (offUV.y = -off.y / uRes.y, and uv y runs UP). So: sample at (px + off.x, py + off.y).

int fg_shade_pixel_sub(const FGScene *sc, const fg_tex_t *bg,
                       const unsigned char *fidx, int fn, const unsigned char *midx, int mn,
                       int interior_ok, float dev_x, float dev_y, float *outR, float *outG, float *outB)
{
    const FGParams *P = &sc->p;
    /* `pix` and the texture grid are the same space for us: the CSS box IS the panel. */
    const float pix_x = dev_x;
    const float pix_y = dev_y;

    float fsd[FG_MAX_SHAPES];
    float d = field_sub(sc, fidx, fn, pix_x, pix_y, fsd);

    float bgr, bgg, bgb;
    fg_tex_lod(bg, dev_x, dev_y, 0.0f, &bgr, &bgg, &bgb);

    /* uShadow is 0 in every shipped preset, so `base` is `bg` (the branch is kept, and skipped, like the shader). */
    float baseR = bgr, baseG = bgg, baseB = bgb;
    if (P->shadow > 0.001f) {
        float ds = field_sub(sc, fidx, fn, pix_x, pix_y - P->shadowR * 0.42f, 0);
        float sh = expf(-fmaxf(ds, 0.0f) / fmaxf(P->shadowR, 1.0f));
        float ar, ag, ab;
        fg_tex_lod(bg, dev_x, dev_y, 7.0f, &ar, &ag, &ab);
        sh *= P->shadow * mixf(0.35f, 1.0f, smoothstepf(0.10f, 0.55f, luma(ar, ag, ab)));
        float k = 1.0f - sh * smoothstepf(-2.0f, 6.0f, d);
        baseR *= k; baseG *= k; baseB *= k;
    }

    if (d > 2.0f) { *outR = baseR; *outG = baseG; *outB = baseB; return 0; }

    /* --- interior fast path (optimisation 2) ------------------------------
       For d <= -uEdge: inside = 1, rim = 0, prof = 0, so off = 0, aaLod = 0, dispW = 0 and spread = 0 (one plain
       sample at this very pixel), bevel = smoothstep(0.14, 0.72, 0) = 0 so sKey = sFill = 0, N = (0,0,1) so fres = 0,
       the rim darkening is 1 - 0.06*0^5 = 1, and line = exp(-((d+0.85)/0.6)^2) is below 1e-20 for |d| >= 4 — with
       uEdge 7 (UI) and 12 (content) it is exp(-105) and exp(-345). alpha = smoothstep(1,-1,d) = 1. What is left is the
       saturation, the body tint, the material tint and the touch light. Proven BIT-identical against the path it
       replaces — every float of every pixel of eight demo frames, not merely every 565 pixel. */
    if (interior_ok && d <= -P->edge) {
        float r = bgr, g = bgg, b = bgb;
        if (P->frost > 0.5f && !(s_prof & FG_PROF_FROST)) {
            /* off = 0 deep inside, so the blur is centred on this pixel — but it is still a blur, and skipping it
               would leave a sharp disc in the middle of a frosted panel. */
            float sr, sg, sb;
            blurSample(bg, dev_x, dev_y, P->frost, pix_x, pix_y, &sr, &sg, &sb);
            float t = clampf((P->frost - 0.5f) / 2.0f, 0.0f, 1.0f);
            r = mixf(r, sr, t); g = mixf(g, sg, t); b = mixf(b, sb, t);
        }
        float rl = luma(r, g, b);
        r = clampf(mixf(rl, r, 1.0f + P->sat), 0.0f, 1.0f);
        g = clampf(mixf(rl, g, 1.0f + P->sat), 0.0f, 1.0f);
        b = clampf(mixf(rl, b, 1.0f + P->sat), 0.0f, 1.0f);

        float ar = 0.5f, ag = 0.5f, ab = 0.5f;
        if (!(s_prof & FG_PROF_AMBIENT)) fg_tex_lod(bg, dev_x, dev_y, 7.0f, &ar, &ag, &ab);
        float amb = luma(ar, ag, ab);
        float ss = smoothstepf(0.16f, 0.60f, amb);
        float adR = mixf(0.055f, 1.0f, ss) + (ar - amb) * kHueLeak;
        float adG = mixf(0.060f, 1.0f, ss) + (ag - amb) * kHueLeak;
        float adB = mixf(0.072f, 1.0f, ss) + (ab - amb) * kHueLeak;
        float neutral = mixf(rl, 1.0f, 0.35f);
        r = mixf(r, mixf(neutral, adR, P->adapt), P->opacity);
        g = mixf(g, mixf(neutral, adG, P->adapt), P->opacity);
        b = mixf(b, mixf(neutral, adB, P->adapt), P->opacity);

        float tnt[4] = { 1, 1, 1, 0 }, rimb = 0.0f;
        if (!(s_prof & FG_PROF_MATERIAL)) materialAt_sub(sc, midx, mn, fidx, fn, fsd, pix_x, pix_y, tnt, &rimb);
        float ta = clampf(tnt[3], 0.0f, 1.0f);
        r = mixf(r, tnt[0], ta);
        g = mixf(g, tnt[1], ta);
        b = mixf(b, tnt[2], ta);

        if (P->touchA > 0.001f) {
            float dx = pix_x - P->touch[0], dy = pix_y - P->touch[1];
            float t2 = P->touchA * expf(-sqrtf(dx * dx + dy * dy) / 70.0f);   /* inside == 1 here */
            r += t2; g += t2; b += t2;
        }
        *outR = clampf(r, 0.0f, 1.0f);
        *outG = clampf(g, 0.0f, 1.0f);
        *outB = clampf(b, 0.0f, 1.0f);
        return 1;
    }

    const float e = 1.25f;
    float nx, ny;
    if (s_prof & FG_PROF_NORMAL) {
        nx = 1.0f; ny = 0.0f;
    } else {
        nx = field_sub(sc, fidx, fn, pix_x + e, pix_y, 0) - field_sub(sc, fidx, fn, pix_x - e, pix_y, 0);
        /* GLSL's +y is the shader's pix.y, which runs DOWN — the same axis as ours, so no sign change here. */
        ny = field_sub(sc, fidx, fn, pix_x, pix_y + e, 0) - field_sub(sc, fidx, fn, pix_x, pix_y - e, 0);
    }
    nx += 1e-5f; ny += 1e-5f;
    {
        float l = sqrtf(nx * nx + ny * ny);
        if (l > 0.0f) { nx /= l; ny /= l; }
    }

    float inside = clampf(-d / P->edge, 0.0f, 1.0f);
    float rim = 1.0f - inside;
    float prof = 1.0f - sqrtf(fmaxf(1.0f - rim * rim, 0.0f));
    float offx = nx * prof * P->bend;
    float offy = ny * prof * P->bend;
    offx *= P->aniso;

    /* uBend is NEGATIVE in the Movie preset (direction -1) and log2 of a negative argument is undefined in GLSL too
       (a driver returns NaN or clamps). The quantity meant here is "how far the sample travelled", so take the
       magnitude — that is what "just enough lod to stop the compressed rim aliasing" is measuring. */
    float aaLod = clampf(log2f(1.0f + prof * fabsf(P->bend) * 0.025f), 0.0f, 1.1f);

    float dispW = powf(smoothstepf(0.20f, 0.98f, rim), 1.15f);
    float spread = 1.25f * P->disp * dispW * clampf(fabsf(P->bend) / 60.0f, 0.0f, 1.6f);
    /* GLSL is `normalize(uKey.xy + vec2(1e-6))`: the epsilon is added to the COMPONENTS, before normalising, so
       that a zero vector still has a direction. Adding it to the length instead is a different number. */
    float kxn = P->key[0] + 1e-6f, kyn = P->key[1] + 1e-6f;
    {
        float l = sqrtf(kxn * kxn + kyn * kyn);
        if (l > 0.0f) {
            kxn /= l;
            kyn /= l;
        }
    }
    float couple = nx * kxn + ny * kyn;
    spread *= 1.0f + P->travel * (0.5f - 0.5f * couple);
    if (spread > 2.0f) spread = 2.0f;

    float dox = offx, doy = offy;
    float pr, pg, pb;
    fg_tex_lod(bg, dev_x + dox, dev_y + doy, aaLod, &pr, &pg, &pb);
    float rr = pr, rg = pg, rb = pb;
    if (s_prof & FG_PROF_SPECTRAL) spread = 0.0f;
    if (spread > 0.004f) {
        spectrum_tables();
        float dresR, dresG, dresB;
#if FG_SPECTRAL_MOMENT
        /* One tap per channel at its own weighted-mean wavelength. Green's mean is exactly 0.5, i.e. s = 1, which
           IS `plain` — so its residual is zero by construction and only two fetches are left. */
        {
            float sR = 1.0f - kSpecMoment * spread;
            float sB = 1.0f + kSpecMoment * spread;
            float ar2, ag2, ab2, br2, bg2, bb2;
            fg_tex_lod(bg, dev_x + dox * sR, dev_y + doy * sR, aaLod, &ar2, &ag2, &ab2);
            fg_tex_lod(bg, dev_x + dox * sB, dev_y + doy * sB, aaLod, &br2, &bg2, &bb2);
            dresR = ar2 - pr;
            dresG = 0.0f;
            dresB = bb2 - pb;
        }
#else
        float accR = 0, accG = 0, accB = 0;
        for (int i = 0; i < FG_SPECTRAL_TAPS; i++) {
            float t = ((float)i + 0.5f) / (float)FG_SPECTRAL_TAPS;
            float s = 1.0f + (0.5f - t) * spread;
            float sr, sg, sb;
            fg_tex_lod(bg, dev_x + dox * s, dev_y + doy * s, aaLod, &sr, &sg, &sb);
            accR += kSpecW[i][0] * sr; accG += kSpecW[i][1] * sg; accB += kSpecW[i][2] * sb;
        }
        dresR = accR / kSpecSum[0] - pr;
        dresG = accG / kSpecSum[1] - pg;
        dresB = accB / kSpecSum[2] - pb;
#endif
        float dl = (dresR + dresG + dresB) * 0.33333f;
        float gain = 1.0f + 1.5f * P->disp;
        rr = clampf(pr + dl + (dresR - dl) * gain, 0.0f, 1.0f);
        rg = clampf(pg + dl + (dresG - dl) * gain, 0.0f, 1.0f);
        rb = clampf(pb + dl + (dresB - dl) * gain, 0.0f, 1.0f);
    }
    if (P->frost > 0.5f && !(s_prof & FG_PROF_FROST)) {
        float sr, sg, sb;
        blurSample(bg, dev_x + dox, dev_y + doy, P->frost, pix_x, pix_y, &sr, &sg, &sb);
        float t = clampf((P->frost - 0.5f) / 2.0f, 0.0f, 1.0f);
        rr = mixf(rr, sr, t);
        rg = mixf(rg, sg, t);
        rb = mixf(rb, sb, t);
    }

    float rl = luma(rr, rg, rb);
    rr = clampf(mixf(rl, rr, 1.0f + P->sat), 0.0f, 1.0f);
    rg = clampf(mixf(rl, rg, 1.0f + P->sat), 0.0f, 1.0f);
    rb = clampf(mixf(rl, rb, 1.0f + P->sat), 0.0f, 1.0f);

    float ar = 0.5f, ag = 0.5f, ab = 0.5f;
    if (!(s_prof & FG_PROF_AMBIENT)) fg_tex_lod(bg, dev_x, dev_y, 7.0f, &ar, &ag, &ab);
    float amb = luma(ar, ag, ab);
    float ss = smoothstepf(0.16f, 0.60f, amb);
    float adR = mixf(0.055f, 1.0f, ss) + (ar - amb) * kHueLeak;
    float adG = mixf(0.060f, 1.0f, ss) + (ag - amb) * kHueLeak;
    float adB = mixf(0.072f, 1.0f, ss) + (ab - amb) * kHueLeak;
    float neutral = mixf(rl, 1.0f, 0.35f);
    float gR = mixf(rr, mixf(neutral, adR, P->adapt), P->opacity);
    float gG = mixf(rg, mixf(neutral, adG, P->adapt), P->opacity);
    float gB = mixf(rb, mixf(neutral, adB, P->adapt), P->opacity);

    float tnt[4] = { 1, 1, 1, 0 }, rimb = 0.0f;
    if (!(s_prof & FG_PROF_MATERIAL)) materialAt_sub(sc, midx, mn, fidx, fn, fsd, pix_x, pix_y, tnt, &rimb);
    float ta = clampf(tnt[3], 0.0f, 1.0f);
    gR = mixf(gR, tnt[0], ta);
    gG = mixf(gG, tnt[1], ta);
    gB = mixf(gB, tnt[2], ta);

    /* ---- lighting ---- */
    float Nx = nx * prof, Ny = ny * prof, Nz = fmaxf(1.0f - prof, 0.06f);
    norm3(&Nx, &Ny, &Nz);
    float Kx = P->key[0], Ky = P->key[1], Kz = P->key[2];
    norm3(&Kx, &Ky, &Kz);
    float Fx = P->fill[0], Fy = P->fill[1], Fz = P->fill[2];
    norm3(&Fx, &Fy, &Fz);
    float hkx = Kx, hky = Ky, hkz = Kz + 1.0f;
    norm3(&hkx, &hky, &hkz);
    float hfx = Fx, hfy = Fy, hfz = Fz + 1.0f;
    norm3(&hfx, &hfy, &hfz);

    float bevel = smoothstepf(0.14f, 0.72f, prof);
    float sKey = 0.0f, sFill = 0.0f, fres = 0.0f;
    if (!(s_prof & FG_PROF_SPEC)) {
        sKey = powf(fmaxf(hkx * Nx + hky * Ny + hkz * Nz, 0.0f), P->shine) * bevel;
        sFill = powf(fmaxf(hfx * Nx + hfy * Ny + hfz * Nz, 0.0f), P->shine * 0.65f) * bevel;
        /* pow(x, 6) and pow(rim, 5) below are INTEGER powers, and a powf() is an exp and a log. Folded into
           multiplies they came out BIT-IDENTICAL on all six reference scenes (0 differing pixels, mean |diff|
           exactly 0), which is more than the ulp argument promises but is what the harness measured. On a core
           with no hardware transcendentals it is two library calls gone from every bevel pixel. */
        float fz = 1.0f - clampf(Nz, 0.0f, 1.0f);
        float fz2 = fz * fz;
        fres = fz2 * fz2 * fz2;
    }
    float lift = mixf(1.25f, 0.85f, smoothstepf(0.12f, 0.62f, amb));
    float spec = (sKey + 0.38f * sFill) * P->spec * lift;

    float rim2 = rim * rim;
    float dk = 1.0f - 0.06f * (rim2 * rim2 * rim);
    gR *= dk; gG *= dk; gB *= dk;
    float add = spec + fres * 0.04f * P->spec * lift;
    gR += add; gG += add; gB += add;

    /* The WebGL two-lobe hairline (winliqglass.js): the source lights the facing edge, and whatever it bounces off
       lights the far one, so a pane carries a line top AND bottom with the sides between them dark. One lobe alone
       reads as a sticker with a bright corner. */
    /* `line` is exp(-((d + 0.85)/0.6)^2) — a hairline about a pixel either side of the silhouette. Past
       ((d+0.85)/0.6)^2 = 105 the exponential UNDERFLOWS to exactly 0.0f (exp(-105) = 2.5e-46, below half the
       smallest float denormal), so skipping the block there is bit-identical rather than merely negligible: a
       looser threshold would be "small enough not to matter", which is not the same claim. With uEdge 12 that
       retires the inner 42 % of the content bevel, and with it two pow() and one exp(). */
    if (!(s_prof & FG_PROF_HAIR) && (d + 0.85f) * (d + 0.85f) < 105.0f * 0.36f) {
        float ln = (d + 0.85f) / 0.60f;
        float line = expf(-ln * ln);
        float hd = nx * P->hiDir[0] + ny * P->hiDir[1];
        /* uHiSharp is max(highlight.sharpness, 0.05) and every shipped preset leaves sharpness at 1.0, so this
           was two full powf() calls per bevel pixel to return their own argument. powf(x, 1.0f) is exactly x. */
        float lobe;
        if (P->hiSharp == 1.0f) lobe = fmaxf(fmaxf(hd, 0.0f), fmaxf(-hd, 0.0f) * P->hiBounce);
        else lobe = fmaxf(powf(fmaxf(hd, 0.0f), P->hiSharp), powf(fmaxf(-hd, 0.0f), P->hiSharp) * P->hiBounce);
        float facing = P->hiBase + (1.0f - P->hiBase) * lobe;
        float hair = line * P->rimLit * facing * lift;
        gR += hair; gG += hair; gB += hair;

        /* winliqglass.js: `line * rimb * (0.45 + 0.40 * abs(hd))` -- the HIGHLIGHT axis, not the key light.
           They are the same vector for every shipped preset (key.xy = axis * 0.62), but hd is the one the source
           uses and it is already in hand. */
        float ehair = line * rimb * (0.45f + 0.40f * fabsf(hd));
        gR += ehair; gG += ehair; gB += ehair;
    }

    if (P->touchA > 0.001f) {
        float dx = pix_x - P->touch[0], dy = pix_y - P->touch[1];
        float t2 = P->touchA * expf(-sqrtf(dx * dx + dy * dy) / 70.0f) * inside;
        gR += t2; gG += t2; gB += t2;
    }

    float alpha = smoothstepf(1.0f, -1.0f, d);
    *outR = mixf(baseR, clampf(gR, 0.0f, 1.0f), alpha);
    *outG = mixf(baseG, clampf(gG, 0.0f, 1.0f), alpha);
    *outB = mixf(baseB, clampf(gB, 0.0f, 1.0f), alpha);
    return 1;
}

void fg_shade_pixel(const FGScene *sc, const fg_tex_t *bg, float px, float py, float *r, float *g, float *b)
{
    unsigned char all[FG_MAX_SHAPES];
    int n = sc->count > FG_MAX_SHAPES ? FG_MAX_SHAPES : sc->count;
    for (int i = 0; i < n; i++) all[i] = (unsigned char)i;
    fg_shade_pixel_sub(sc, bg, all, n, all, n, 0, px, py, r, g, b);
}
