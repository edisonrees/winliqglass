// Shape model and material parameters.
//
// One-to-one with engine.py's Shape and the GLASS_FRAG uniform block, names
// kept identical so a value tuned in the desktop studio can be typed straight
// in here. Floats become fx, vec2 becomes a pair, and the uniform arrays
// become a plain array of structs.

#ifndef LG_SCENE_H
#define LG_SCENE_H

#include "lg_config.h"
#include "lg_fixed.h"

enum LGKind {
    LG_CIRCLE = 0,
    LG_RRECT  = 1,
    LG_TRI    = 2,
    LG_RING   = 3,
    LG_PENT   = 4
};

struct LGShape {
    uint8_t kind;
    fx x, y;            // centre, pixels
    fx hw, hh;          // half extents (circle/ring: hw is the radius)
    fx rad;             // corner rounding (ring: half thickness)
    fx rot;             // rotation, turns
    fx tintR, tintG, tintB, tintA;
    fx rim;             // per-shape hairline override: >0 light, <0 contour
    uint8_t merge;      // participates in the smooth-min field
};

static inline void lg_shape_init(LGShape *s) {
    s->kind = LG_CIRCLE;
    s->x = s->y = 0;
    s->hw = s->hh = FX(40.0);
    s->rad = 0;
    s->rot = 0;
    s->tintR = s->tintG = s->tintB = FX_ONE;
    s->tintA = 0;
    s->rim = 0;
    s->merge = 1;
}

struct LGParams {
    fx mergeK;      // uMergeK  smooth-min radius
    fx opacity;     // uOpacity body mix
    fx bend;        // uBend    refraction depth, px
    fx edge;        // uEdge    lens band width, px
    fx aniso;       // uAniso   horizontal bend damping
    fx spec;        // uSpec    specular strength
    fx shine;       // uShine   specular exponent
    fx adapt;       // uAdapt   adaptive body tint amount
    fx sat;         // uSat     saturation concentration
    fx rimLit;      // uRimLit  hairline strength
    fx keyX, keyY, keyZ;    // uKey
    fx fillX, fillY, fillZ; // uFill
    fx touchX, touchY;      // uTouch
    fx touchA;              // uTouchA

    // --- edge light rays (replaces the shader's per-pixel uDisp taps)
    fx rayGain;     // overall brightness of the edge rays
    fx rayDisp;     // spectral fan width, px at full path length
    fx rayDecay;    // per-step survival inside the fillet, 0..1
    fx rayBounce;   // how hard the light zig-zags between the fillet walls
    fx rayPhase;    // animation phase, turns
};

// The desktop app's ui_params(): the tighter of its two presets, and the one
// that reads best on a small panel where the glass is only a few dozen pixels
// across.
static inline void lg_params_default(LGParams *p) {
    p->mergeK  = FX(26.0);
    p->opacity = FX(0.16);
    p->bend    = FX(21.0);
    p->edge    = FX(7.0);
    p->aniso   = FX(0.80);
    p->spec    = FX(0.34);
    p->shine   = FX(26.0);
    p->adapt   = FX(0.48);
    p->sat     = FX(0.06);
    p->rimLit  = FX(0.34);
    p->keyX  = FX(-0.44); p->keyY  = FX(-0.44); p->keyZ  = FX(0.78);
    p->fillX = FX(0.39);  p->fillY = FX(0.39);  p->fillZ = FX(0.62);
    p->touchX = 0; p->touchY = 0; p->touchA = 0;

    // Loss along a filleted edge is low — that is the point of a light pipe —
    // so the ring's brightness gradient comes from where light couples in, not
    // from the light dying before it gets round.
    p->rayGain   = FX(1.35);
    p->rayDisp   = FX(3.2);
    p->rayDecay  = FX(0.965);
    p->rayBounce = FX(0.16);
    p->rayPhase  = 0;
}

struct LGScene {
    LGShape shapes[LG_MAX_SHAPES];
    int count;
    LGParams p;
};

static inline void lg_scene_init(LGScene *sc) {
    sc->count = 0;
    lg_params_default(&sc->p);
}

static inline LGShape *lg_scene_add(LGScene *sc) {
    if (sc->count >= LG_MAX_SHAPES) return 0;
    LGShape *s = &sc->shapes[sc->count++];
    lg_shape_init(s);
    return s;
}

#endif // LG_SCENE_H
