#pragma once
// The background as a TEXTURE with a mip pyramid — what GLASS_FRAG's `sampler2D uBg` is.
//
// The shader needs three things the Arduino port faked: bilinear sampling, a small lod for the compressed rim
// (`aaLod` <= 1.1), and a WIDE mip for the adaptive body tint (`textureLod(..., 7.0)`). So the wall is box-filtered
// down to a pyramid once, in PSRAM, and sampled bilinearly within a level with a lerp between levels.
//
// Every level is RGB565, like the source: the box filter runs in float and quantises once per level. Floats would be
// 6x the bytes, and on the P4 a texture fetch is 4 random texels out of PSRAM, so the pyramid's WIDTH in bytes is
// what the fetch costs. Measured fidelity price of 565 levels vs float levels: see arduino/README.md.
// Level 0 is the source buffer itself, so the exact source pixels stay the source pixels.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The mip chain is sampled with a lerp BETWEEN levels (GL's LINEAR_MIPMAP_LINEAR), which doubles the texels every
// fetch touches. 0 picks the nearest level instead (LINEAR_MIPMAP_NEAREST). Only the rim band has a non-zero lod
// (aaLod <= 1.1), so this costs nothing in the interior and halves the fetch on the bevel — and on the P4 a fetch
// is four random texels out of PSRAM, which is what the ablation profile keeps naming.
// SHIPPED: 0. Moves the output by 0.139/255 on the video still and 0.039 on a UI frame; the error against the
// WebGL2 ground truth goes 1.636 -> 1.666 and 0.4965 -> 0.5135. 1.51-1.58x. No banding where the level switches:
// checked at 4x on the card end and the disc rim (the fidelity harness's stacked crops).
#ifndef FG_TRILINEAR
#define FG_TRILINEAR 0
#endif

#define FG_MAX_LEVELS 12

typedef struct {
    const uint16_t *src;            // level 0, RGB565, w*h
    int w, h;
    int levels;                     // including level 0
    uint16_t *lv[FG_MAX_LEVELS];    // lv[0] unused (level 0 is `src`); lv[n] is RGB565
    int lw[FG_MAX_LEVELS], lh[FG_MAX_LEVELS];
    float sx[FG_MAX_LEVELS], sy[FG_MAX_LEVELS];   // lw[L]/w and lh[L]/h: GL's uv*levelSize, folded once at bind
} fg_tex_t;

typedef void *(*fg_alloc_fn)(unsigned long size);
void fg_mem_set_alloc(fg_alloc_fn fn);          // default malloc; the device passes a PSRAM allocator
void fg_mem_set_alloc_small(fg_alloc_fn fn);    // for mip levels <= 16 KB; the device passes an INTERNAL allocator
void *fg_mem_alloc(unsigned long size);
void *fg_mem_alloc_small(unsigned long size);

// Build (or rebuild, if `src` changed) the pyramid for an RGB565 image. Returns 0 on allocation failure.
// The pyramid is cached on the (src, w, h) triple: calling it again with the same image is free.
int  fg_tex_bind(fg_tex_t *t, const uint16_t *src, int w, int h);
void fg_tex_free(fg_tex_t *t);

// textureLod(): absolute texel coordinates (x, y) in level-0 pixels, top-left origin, clamped at the edges.
void fg_tex_lod(const fg_tex_t *t, float x, float y, float lod, float *r, float *g, float *b);

#ifdef __cplusplus
}
#endif
