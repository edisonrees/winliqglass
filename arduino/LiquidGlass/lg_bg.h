// The wall behind the glass.
//
// On the desktop this is a 20MB photograph in a mipmapped texture. There is no
// such thing on a microcontroller, so the background is a *function* instead:
// bg(x, y) evaluated on demand. That turns out to be the single best thing
// about the port — refraction is "evaluate the background at the displaced
// coordinate", which needs no framebuffer, no second pass and no memory at
// all, and the two-pass renderer in engine.py collapses to one.
//
// The default is a dusk gradient with broad ribbons and hairline caustics,
// chosen for the same reason the desktop build ships Golden Gate: the lens
// needs high-frequency detail to compress at the rim, or there is nothing to
// see. Point it at your own RGB565 bitmap with lg_bg_set_image().

#ifndef LG_BG_H
#define LG_BG_H

#include "lg_fixed.h"

// Tell the background how large the viewport is; ribbons scale to fit.
void lg_bg_set_viewport(int w, int h);

// Optional bitmap background, RGB565, cover-fitted. Pass 0 to go back to the
// procedural one. The pointer is kept, not copied — keep the data alive.
void lg_bg_set_image(const uint16_t *pixels, int w, int h);

// Colour at a pixel coordinate, components in [0, FX_ONE]. Coordinates outside
// the viewport clamp, matching the shader's bgUV().
void lg_bg_rgb(fx x, fx y, fx *r, fx *g, fx *b);

// Row-coherent fast path for spans that are pure background: everything that
// depends only on y is hoisted out of the inner loop.
void lg_bg_prepare_row(fx y);
void lg_bg_rgb_row(fx x, fx *r, fx *g, fx *b);

// Low-frequency luma, standing in for the shader's textureLod(uBg, uv, 7.0).
// The adaptive body tint keys off this, so it must track the broad polarity of
// the wall and ignore the detail.
fx lg_bg_ambient(fx x, fx y);

#endif // LG_BG_H
