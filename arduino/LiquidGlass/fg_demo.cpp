#include "fg_demo.h"
#include <math.h>

// ---------------------------------------------------------------------------
// the wall
// ---------------------------------------------------------------------------
// Ported from the fixed-point tree's lg_bg.cpp with the constants unchanged, because the reasoning behind them is
// unchanged: the lens has nothing to show unless the wall has something to compress. The gradient carries the broad
// polarity, two low-frequency diagonals give the rim something wide to fold, and a high-frequency band raised to the
// eighth power leaves only crests -- those are what make the compression rings visible at the bevel.
//
// The desktop build ships Golden Gate for the same reason. If you have a photograph in flash, bind that instead:
// `fg_tex_bind(&tex, your_rgb565, w, h)` takes any RGB565 image and this file stops being interesting.

static void grad_at(float v, float *r, float *g, float *b)
{
    // indigo -> magenta -> ember -> dark water, the run a dusk sky makes
    float t = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    if (t < 0.42f) {
        float k = t / 0.42f;
        *r = 0.043f + (0.310f - 0.043f) * k;
        *g = 0.055f + (0.145f - 0.055f) * k;
        *b = 0.145f + (0.330f - 0.145f) * k;
    } else if (t < 0.66f) {
        float k = (t - 0.42f) / 0.24f;
        *r = 0.310f + (0.960f - 0.310f) * k;
        *g = 0.145f + (0.420f - 0.145f) * k;
        *b = 0.330f + (0.180f - 0.330f) * k;
    } else {
        float k = (t - 0.66f) / 0.34f;
        *r = 0.960f + (0.070f - 0.960f) * k;
        *g = 0.420f + (0.075f - 0.420f) * k;
        *b = 0.180f + (0.115f - 0.180f) * k;
    }
}

static inline float sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void fg_demo_wall(uint16_t *dst, int w, int h)
{
    if (!dst || w < 1 || h < 1) return;
    const float invW = 1.0f / (float)w, invH = 1.0f / (float)h;
    for (int y = 0; y < h; y++) {
        float v = ((float)y + 0.5f) * invH;
        float br, bg, bb;
        grad_at(v, &br, &bg, &bb);           // depends only on y, so it is hoisted out of the row
        for (int x = 0; x < w; x++) {
            float u = ((float)x + 0.5f) * invW;

            float s1 = sinf(u * 1.35f + v * -0.62f + 0.11f);
            float s2 = sinf(u * -0.80f + v * 1.90f + 0.47f);
            float ribbon = s1 * 0.085f + s2 * 0.055f;

            float c = sat(sinf(u * 7.30f + v * 4.10f));
            float c2 = c * c; c2 = c2 * c2; c2 = c2 * c2;      // c^8
            float caust = c2 * 0.42f;

            float r = sat(br + ribbon + caust);
            float g = sat(bg + ribbon * 0.70f + caust * 0.94f);
            float b = sat(bb + ribbon * 1.25f + caust * 0.86f);

            /* Plain rounding, no dither: this is the SOURCE the lens samples, and a dither here would be
               high-frequency noise for the rim to magnify. The dither belongs at the output, which is where
               fg_render_region puts it. */
            int ri = (int)(r * 31.0f + 0.5f), gi = (int)(g * 63.0f + 0.5f), bi = (int)(b * 31.0f + 0.5f);
            if (ri > 31) ri = 31;
            if (gi > 63) gi = 63;
            if (bi > 31) bi = 31;
            dst[(size_t)y * w + x] = (uint16_t)((ri << 11) | (gi << 5) | bi);
        }
    }
}

// ---------------------------------------------------------------------------
// the scene
// ---------------------------------------------------------------------------
// Laid out against a 240x240 reference and scaled to whatever panel is attached, so the material reads the same on a
// 128px round display as on a 320px rectangle.

void fg_demo_scene(FGScene *sc, int w, int h, float t)
{
    /* engine.py's UI pass. At k = 1 its dials (bend -21, edge 7, mergeK 26) are exactly the numbers the fixed-point
       demo used at 240x240, so this is the same scene the old preview drew, under the real material. */
    fg_scene_init(sc, 1);

    const float cx = (float)w * 0.5f, cy = (float)h * 0.5f;
    const float k = (float)(w < h ? w : h) / 240.0f;
    #define S(v) ((v) * k)

    // A drifting pair that meets in the middle of the loop. Watching the two silhouettes fuse is the clearest way to
    // see that the lens band follows the MERGED outline rather than the shapes that made it -- smin() is doing the
    // joining, and the bevel, the dispersion and the hairline all run on the joined field.
    const float swing = sinf(t * 6.2831853f);

    FGShape *card = fg_scene_add(sc);
    card->kind = FG_RRECT;
    card->x = cx;
    card->y = cy - S(34.0f);
    card->hw = S(76.0f);
    card->hh = S(40.0f);
    card->rad = S(28.0f);

    FGShape *pill = fg_scene_add(sc);
    pill->kind = FG_RRECT;
    pill->x = cx - S(46.0f) + swing * S(26.0f);
    pill->y = cy + S(56.0f);
    pill->hw = S(40.0f);
    pill->hh = S(19.0f);
    pill->rad = S(19.0f);
    /* normalizedShape() in winliqglass.js defaults merge to 0: joining is OPT-IN, per shape. These two opt in; the
       card above does not, so it keeps its own silhouette while they pass under it. */
    pill->merge = 1.0f;

    FGShape *dot = fg_scene_add(sc);
    dot->kind = FG_CIRCLE;
    dot->x = cx + S(52.0f) - swing * S(22.0f);
    dot->y = cy + S(56.0f);
    dot->hw = S(24.0f);
    dot->hh = S(24.0f);
    dot->merge = 1.0f;

    FGParams *p = &sc->p;

    // Bend, edge and mergeK are in CSS pixels, so on a panel that is not the 1280x720 box the preset was dialled
    // against they have to follow the panel, or a small display gets a lens band wider than the shape it sits on.
    // This scales the SCENE, not the material: every shape above is scaled by the same k.
    p->bend *= k;
    p->edge = fmaxf(p->edge * k, 3.0f);
    p->mergeK *= k;

    // The highlight angle is a scene dial the library exposes (setHighlight({angle})), not a change to the material,
    // so drifting it is fair game and it is most of what sells the surface as glass rather than a decal. _axis() in
    // winliqglass.js is [sin(a), -cos(a)]; key, fill and uHiDir are all derived from it exactly as fg_params_movie
    // derives them at angle 0.
    const float a = 0.10f * sinf(t * 6.2831853f);
    const float ax = sinf(a), ay = -cosf(a);
    p->key[0] = ax * 0.62f;   p->key[1] = ay * 0.62f;   p->key[2] = 0.78f;
    p->fill[0] = -ax * 0.55f; p->fill[1] = -ay * 0.55f; p->fill[2] = 0.62f;
    p->hiDir[0] = ax;         p->hiDir[1] = ay;

    #undef S
}
