// The signed distance field: same shapes, same smooth-min, same normals as
// shaders.py, in fixed point.

#ifndef LG_FIELD_H
#define LG_FIELD_H

#include "lg_scene.h"

// Signed distance to one shape, in pixels. Negative inside.
fx lg_shape_sdf(const LGShape *s, fx px, fx py);

// Smooth-min of every shape in the scene. This is the surface the lens and the
// edge rays both work against, so a merged pair of blobs gets one continuous
// fillet running around the union rather than two overlapping ones.
fx lg_field(const LGScene *sc, fx px, fx py);

// Outward unit normal of the field, by differencing. Only ever called for
// pixels already known to be at or inside the rim.
void lg_field_normal(const LGScene *sc, fx px, fx py, fx *nx, fx *ny);

// Distance-weighted blend of the per-shape tint and rim override, matching
// materialAt() in the shader.
void lg_material_at(const LGScene *sc, fx px, fx py,
                    fx *tr, fx *tg, fx *tb, fx *ta, fx *rimb);

// Conservative radius covering a shape plus the merge blend and the lens band,
// used for per-row culling.
fx lg_shape_bound(const LGShape *s, const LGParams *p);

// Push a point onto the isocontour field(p) == target, by walking down the
// gradient. Two or three passes converge for the distances we use it at; more
// than that and the field is not locally distance-like anyway.
void lg_field_project(const LGScene *sc, fx *px, fx *py, fx target, int iters);

#endif // LG_FIELD_H
