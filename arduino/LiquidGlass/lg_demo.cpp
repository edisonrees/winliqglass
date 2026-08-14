#include "lg_demo.h"

// Laid out against a 240x240 reference and scaled to whatever panel is
// attached, so the material reads the same on a 128px round display as on a
// 320px rectangle.

void lg_demo_scene(LGScene *sc, int w, int h, fx t) {
    lg_scene_init(sc);

    fx cx = fxfromint(w) / 2;
    fx cy = fxfromint(h) / 2;
    fx k = fxd(fxfromint(w < h ? w : h), FX(240.0));
    #define S(v) fxm(FX(v), k)

    // A drifting pair that meets in the middle of the loop. Watching the two
    // silhouettes fuse is the clearest way to see that the rays follow the
    // merged outline rather than the shapes that made it.
    fx swing = fxsin(t);

    LGShape *card = lg_scene_add(sc);
    card->kind = LG_RRECT;
    card->x = cx;
    card->y = cy - S(34.0);
    card->hw = S(76.0);
    card->hh = S(40.0);
    card->rad = S(28.0);

    LGShape *pill = lg_scene_add(sc);
    pill->kind = LG_RRECT;
    pill->x = cx - S(46.0) + fxm(swing, S(26.0));
    pill->y = cy + S(56.0);
    pill->hw = S(40.0);
    pill->hh = S(19.0);
    pill->rad = S(19.0);

    LGShape *dot = lg_scene_add(sc);
    dot->kind = LG_CIRCLE;
    dot->x = cx + S(52.0) - fxm(swing, S(22.0));
    dot->y = cy + S(56.0);
    dot->hw = S(24.0);
    dot->hh = S(24.0);

    LGParams *p = &sc->p;
    lg_params_default(p);

    // Bend and edge are in pixels, so they have to follow the panel or a small
    // display gets a lens band wider than the shape it sits on.
    p->bend = fxm(FX(21.0), k);
    p->edge = fxmax(fxm(FX(7.0), k), FX(3.0));
    p->mergeK = fxm(FX(26.0), k);
    p->rayDisp = fxmax(fxm(FX(3.2), k), FX(1.6));

    // Key above-left, fill below-right, drifting the way the desktop build
    // does — the travelling specular is most of what sells the material as
    // glass rather than a decal. Screen space here, so +y is down.
    fx a = FX(0.625) + fxm(FX(0.035), fxsin(t));
    fx ca = fxcos(a), sa = fxsin(a);
    p->keyX  = fxm(ca, FX(0.62));  p->keyY  = fxm(sa, FX(0.62));  p->keyZ  = FX(0.78);
    p->fillX = fxm(-ca, FX(0.55)); p->fillY = fxm(-sa, FX(0.55)); p->fillZ = FX(0.62);

    // Light coupling into the fillet travels around it, so the whole ring
    // drifts rather than the highlight alone.
    p->rayPhase = t;

    #undef S
}
