#pragma once
// Liquid Glass, ported from the DESKTOP material: shaders.py GLASS_FRAG + engine.py's two passes,
// plus the WebGL build's two-lobe hairline (webgl/winliqglass.js uHiDir/uHiBounce/uHiSharp/uHiBase).
// Float, line for line — the P4 has a single-precision FPU, so the fixed-point approximations the Arduino tree made
// (traced rays instead of spectral taps, a procedural wall, a 4x4 ambient grid) are not needed and are not used.
//
// See arduino/README.md. The preset is WebGL "Movie" and is NOT tuned per widget.
//
// WHERE THE CITED FILES ARE. `shaders.py`, `engine.py`, `app.py` and `scene.json` are at the root of `main` — the
// desktop studio. `webgl/winliqglass.js`, `webgl/README.md`, `presets.md` and `demo/video.html` are on the `webgl`
// branch. Both are this repository; the port reads them as one material with four known disagreements (below).
#include "fg_tex.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FG_MAX_SHAPES 24

// ---------------------------------------------------------------------------
// WHERE THE TWO UPSTREAM BUILDS DISAGREE
// ---------------------------------------------------------------------------
// `shaders.py` GLASS_FRAG and `webgl/winliqglass.js` FRAG are the same shader with FOUR
// material differences, and no more (whole-file diff, 2026-09-19):
//   1. the desktop build gives rrects a CONTINUOUS (squircle) corner; the WebGL build's sdRRect has no such branch;
//   2. the desktop build leaks the ambient mip's hue into the body tint; the WebGL build takes only its luma;
//   3. the WebGL build takes abs(uBend) for aaLod AND for the dispersion spread; the desktop build does not
//      (and with direction -1 that silently zeroes the desktop build's dispersion) -- we follow the WebGL build;
//   4. the WebGL build has the TWO-LOBE hairline (uHiDir/uHiBounce/uHiSharp/uHiBase); the desktop build has one
//      lobe -- we follow the WebGL build, which is the only form that works with the Movie preset.
// 3 and 4 are settled: the WebGL form is the only one that works with this preset. 1 and 2 are a genuine choice,
// so they are switches, and the DEFAULTS ARE THE DESKTOP BUILD -- shaders.py is the porting source here, and the
// corner is a visible design decision rather than a tolerance. Setting both to 0 makes the port match the WebGL2
// ground truth to its own 8-bit output step (mean |diff| 0.245/255 over a whole 1280x720 UI frame; 99.47 % of
// channel samples within 0.5/255). The table is in arduino/README.md.
#ifndef FG_CORNERS_CONTINUOUS
#define FG_CORNERS_CONTINUOUS 1     /* 1 = shaders.py's squircle corner, 0 = winliqglass.js's circular one */
#endif
#ifndef FG_AMBIENT_HUE
#define FG_AMBIENT_HUE 1            /* 1 = shaders.py's `adapt += (ambCol - amb) * 0.15`, 0 = the WebGL build */
#endif

// ---------------------------------------------------------------------------
// THE SPECTRAL TAPS — the one place where the P4's cost is worth trading output for
// ---------------------------------------------------------------------------
// The shader integrates the dispersion with 8 texture fetches along one line, weighted by three narrow Gaussians
// in wavelength (`spectrum()`), and that integral is 30-39 % of the frame on this hardware (ablation profile,
// arduino/README.md). Two ways to spend less, both measured against the WebGL2 ground truth:
//
//   FG_SPECTRAL_TAPS   N midpoint taps instead of 8 — the SAME quadrature, coarser. 8 is upstream.
//   FG_SPECTRAL_MOMENT one tap per CHANNEL, at that channel's own weighted-mean wavelength. The three means are
//                      t = 0.767 / 0.500 / 0.233, so the GREEN tap is the plain sample the shader already has and
//                      only two fetches remain. It is the first moment of the same weight function, so it is
//                      exact for a locally linear background and loses the smoothing the eight taps give; the
//                      fringe comes out slightly stronger. Overrides FG_SPECTRAL_TAPS when set.
//
// SHIPPED: 6 taps. The rule applied to any output-affecting optimisation here is "keep anything that costs <= 0.5/255
// over the floor". Six taps moves the output by 0.108/255 on the harshest scene (the transport discs over a video
// still) and 0.005 on a UI frame, and the ERROR against the WebGL2 ground truth goes 1.636 -> 1.671 there and
// 0.4965 -> 0.4976 on the UI frame: inside the budget on either reading, for 1.29-1.46x. Four taps also qualifies
// (0.222 moved, error 1.636 -> 1.744; 1.38-1.45x) and is one -D away. Six is the pick inside the qualifying set
// because the midpoint quadrature's SPAN narrows as N falls (+-0.4167*spread at 6 against upstream's +-0.4375,
// but +-0.375 at 4), so six keeps the fringe the same WIDTH and only samples it more coarsely -- four would be a
// systematic narrowing dressed up as noise. FG_SPECTRAL_MOMENT is NOT shipped: 2.63 moved, error 1.636 -> 3.935,
// five times over the budget, and the crops show it plainly.
// Restore upstream exactly with -DFG_SPECTRAL_TAPS=8 -DFG_TRILINEAR=1. Full table: arduino/README.md.
#ifndef FG_SPECTRAL_TAPS
#define FG_SPECTRAL_TAPS 6
#endif
#ifndef FG_SPECTRAL_MOMENT
#define FG_SPECTRAL_MOMENT 0
#endif

// FG_TRILINEAR (the mip level lerp) is the third switch; it lives in fg_tex.h, beside the sampler that
// consumes it, because fg_tex.cpp does not include this header.

enum FGKind { FG_CIRCLE = 0, FG_RRECT = 1, FG_TRI = 2, FG_RING = 3, FG_PENT = 4 };

typedef struct {
    int   kind;
    float x, y;                 // centre, CSS px = layout px
    float hw, hh;               // half extents (circle/ring: hw is the radius)
    float rad;                  // corner radius (ring: half thickness)
    float rot;                  // radians
    float tint[4];              // uTint[i]
    float rimb;                 // uRimB[i]
    float merge;                // uMerge[i]
} FGShape;

// DEVICE PIXEL RATIO — what it actually is in this library, corrected 2026-09-19 against the source and the WebGL
// ground-truth renders. `resize(w, h, dpr)` sets the CSS box to w x h and the BACKING STORE to w*dpr x h*dpr; the
// fragment shader runs per backing-store pixel but works in CSS coordinates (`pix = vUV * uRes`), and the compositor
// resolves the backing store down into the CSS box. So **DPR is supersampling and nothing else** — it does not
// change how wide the lens band is relative to a shape. The reference build proves it: DPR 1 vs DPR 2.5 differ on
// 0.37-0.50 % of pixels, max delta 20-26/255, concentrated in the bevel (measured; arduino/README.md).
//
// Our CSS box is the panel: 1280x720 on the Tab5, the same grid the UI lays out on. So shape coordinates go
// in UNSCALED, and FG_SS is how many samples per axis the renderer averages. 1 = no supersampling.
#define FG_SS 1

typedef struct {               // one-to-one with GLASS_FRAG's uniform block
    float mergeK, opacity, frost, bend, edge, aniso, disp, travel;
    float shadow, shadowR, spec, shine;
    float key[3], fill[3];
    float adapt, sat, rimLit;
    float hiDir[2], hiBounce, hiSharp, hiBase;
    float touch[2], touchA;      // CSS px
    int   ss;                    // supersampling, samples per axis (the library's DPR). 1 = none.
} FGParams;

typedef struct {
    FGShape shapes[FG_MAX_SHAPES];
    int count;
    FGParams p;
} FGScene;

// The WebGL "Movie" preset (presets.md and demo/video.html on the `webgl` branch), `direction -1` already in bend.
// `ui` picks sourceParams(glass, true) vs (glass, false) — engine.py's UI pass vs content pass.
void fg_params_movie(FGParams *p, int ui);

void fg_scene_init(FGScene *sc, int ui);
FGShape *fg_scene_add(FGScene *sc);

// One fragment of GLASS_FRAG. `px, py` are absolute CSS pixels, which for us is also the background texture's own
// grid, so bgUV's affine collapses to the identity and a pixel offset is a pixel offset.
void fg_shade_pixel(const FGScene *sc, const fg_tex_t *bg, float px, float py, float *r, float *g, float *b);

typedef void (*fg_flush_fn)(int x, int y, int w, int h, const unsigned short *pixels, void *user);

// Render the w*h window whose top-left is (x0, y0) of `bg`, and flush it stripe by stripe in viewport coordinates.
void fg_render_region(const FGScene *sc, const fg_tex_t *bg, int x0, int y0, int w, int h,
                      fg_flush_fn flush, void *user);

// Both optimisations default ON. The interior fast path is BIT-identical against them off (every float of eight
// demo frames). The row active list is exact in arithmetic but not in IEEE — it changes the length of a float
// fold — and measures at most 0.026/255 pre-dither, one flipped RGB565 pixel per frame. See arduino/README.md.
void fg_opt_set(int row_active_list, int interior_fast_path);

// PROFILING BY ABLATION. Timing a stage with a counter inside the inner loop distorts the thing being measured, so
// instead each stage can be switched off and the difference in frame time IS its cost. The output is wrong with any
// bit set — this is an instrument, not a quality knob, and `fg_prof_set(0)` restores the real renderer.
#define FG_PROF_NORMAL   (1u << 0)   /* the 4 extra field() evaluations for the gradient */
#define FG_PROF_SPECTRAL (1u << 1)   /* the 8 dispersion taps (8 texture fetches) */
#define FG_PROF_MATERIAL (1u << 2)   /* materialAt(): one exp + one SDF per shape */
#define FG_PROF_SPEC     (1u << 3)   /* the two speculars and the Fresnel term (3 pow) */
#define FG_PROF_HAIR     (1u << 4)   /* the two-lobe hairline (exp + 2 pow) */
#define FG_PROF_AMBIENT  (1u << 5)   /* the lod-7 fetch for the adaptive body tint */
#define FG_PROF_FROST    (1u << 6)   /* blurSample()'s 16 taps */
void fg_prof_set(unsigned mask);

// FIDELITY TAP. The renderer's output is RGB565 with a Bayer dither, and a dither step is 8/255 in red/blue — far
// coarser than the material differences this port is tuned on. So `fg_render_region` can also write the PRE-QUANTISED
// float triple of every pixel into a caller-supplied w*h*3 buffer (window-local order, RGB). Host instrument only;
// the firmware never sets it and the write is behind a null check.
void fg_render_set_f32(float *dst);

// The 4x4 Bayer dither is ON by default and stays on for the panel (the wallpaper gradient bands without it).
// It is NOT idempotent, though: expanding 565 to float and re-quantising with a dither flips one LSB on 16 of the
// 512 (value, phase) pairs, so every extra pass of a multi-pass host render adds ~0.47/255 of noise that has
// nothing to do with the material. Plain rounding IS exactly idempotent, so the fidelity harness turns the dither
// off for the INTERMEDIATE passes. Host instrument; the firmware never calls it.
void fg_render_set_dither(int on);

#ifdef __cplusplus
}
#endif
