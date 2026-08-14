// The demo scene, shared by the sketch and the host preview so both show the
// same thing. `t` is a normalised loop position in turns: 0 and FX_ONE are the
// same frame.

#ifndef LG_DEMO_H
#define LG_DEMO_H

#include "lg_scene.h"

void lg_demo_scene(LGScene *sc, int w, int h, fx t);

#endif // LG_DEMO_H
