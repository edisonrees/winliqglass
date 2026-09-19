// The demo scene and its wall, shared by the sketch and the host preview so both show the same thing.
// `t` is a normalised loop position in turns: 0.0 and 1.0 are the same frame.

#ifndef FG_DEMO_H
#define FG_DEMO_H

#include "fg_glass.h"

// Paint the dusk wall into an RGB565 buffer of w*h pixels. This is the same wall the fixed-point port evaluated as
// a FUNCTION (its lg_bg.cpp): the same gradient run, the same two ribbons, the same caustic band, in float. What
// changed is where it ends up. GLASS_FRAG samples `uBg` bilinearly, at a small lod for the compressed rim and at
// lod 7 for the adaptive body tint, so the background has to be a TEXTURE WITH A MIP PYRAMID -- a function cannot
// answer "the average of this neighbourhood" without integrating it. Bake it once, bind it once (fg_tex_bind), and
// the whole of the shader's sampling works as written.
void fg_demo_wall(uint16_t *dst, int w, int h);

// Build the demo scene for loop position `t`, laid out against a 240x240 reference and scaled to the panel.
void fg_demo_scene(FGScene *sc, int w, int h, float t);

#endif // FG_DEMO_H
