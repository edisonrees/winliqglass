// Stripe renderer.
//
// engine.py needs two passes because its glass samples a texture that must
// already contain the wallpaper. Here the background is a function, so a
// displaced sample is just another call and the whole thing collapses to one
// pass with no offscreen buffer.
//
// The frame is produced LG_STRIPE_H rows at a time and handed to a callback,
// so peak RAM is width * LG_STRIPE_H * 2 bytes whatever the panel's size.

#ifndef LG_RENDER_H
#define LG_RENDER_H

#include "lg_scene.h"

// Called once per stripe with RGB565 pixels, row-major, w*h of them.
typedef void (*lg_flush_fn)(int x, int y, int w, int h,
                            const uint16_t *pixels, void *user);

// Render one frame. `w` must be <= LG_MAX_WIDTH. Traces the edge rays, shades
// the glass, composites the rays, and flushes stripe by stripe.
void lg_render(const LGScene *sc, int w, int h, lg_flush_fn flush, void *user);

// Shade a single pixel to linear-ish RGB in [0, FX_ONE]. Exposed so a host
// tool can dump full-precision frames without going through 565.
void lg_shade_pixel(const LGScene *sc, fx px, fx py, fx *r, fx *g, fx *b);

#endif // LG_RENDER_H
