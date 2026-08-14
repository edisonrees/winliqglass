// Refraction colour as light rays running around the fillet.
//
// WHAT THIS REPLACES
//
// shaders.py gets its spectral fringe by sampling the background eight times
// per pixel, once per wavelength, each at a slightly different displacement,
// and recombining through a narrow RGB response. That is eight dependent
// texture fetches for every pixel of every rim, which is nothing on a GPU and
// completely out of reach here.
//
// So the dispersion is turned inside out: instead of asking every pixel what
// colour the light arriving at it separated into, trace the light. The cost
// stops depending on resolution and starts depending on how many rays you ask
// for, which is a number you control.
//
// THE OPTICS
//
// The rim of the glass is a fillet — a quarter-round rolling from the flat top
// down to the silhouette. A filleted glass edge is a light pipe. Light that
// couples in near the silhouette is travelling almost along the surface, so it
// hits the far wall of the fillet well past the critical angle, totally
// internally reflects, and stays trapped. Trapped light follows the pipe, and
// the pipe follows the outline: around the corners, along the straights, all
// the way around the shape. It is why a real glass edge glows along its whole
// length from one light source, and why the glow is coloured.
//
// The colour is the second half. Every reflection and every bit of curvature
// bends the bundle, and the index of refraction is wavelength-dependent, so
// each bend separates the wavelengths a little further. The fan therefore
// widens with distance travelled — tight where the light couples in, spread
// into a full spectrum by the time it has rounded a corner.
//
// THE MODEL
//
// Per ray:
//   * seed on the merged silhouette, spaced around the whole perimeter;
//   * step along the tangent, then project back onto an isocontour of the
//     field, so the path is pinned inside the fillet and inherits its curve;
//   * oscillate the isocontour it is pinned to, which is the zig-zag of the
//     internal reflections. This is a stand-in for solving Snell at each wall,
//     not a derivation of it — it produces the right bounce spacing and the
//     right shimmer for a tenth of the arithmetic;
//   * accumulate a fan width from the turning done so far;
//   * fade with distance, and start bright only where the surface faces the
//     key light, so the ring is not uniform.
//
// Per drawn segment the fan is expanded into LG_RAY_BANDS wavelengths, offset
// across the pipe, and drawn additively. Sweeping the offset across the band
// is what puts the orange -> green -> cyan -> magenta run on the edge.

#ifndef LG_RAYS_H
#define LG_RAYS_H

#include "lg_scene.h"

// Trace the whole edge into the ray pool. Call once per frame, after moving
// shapes and before rendering stripes.
void lg_rays_trace(const LGScene *sc, int viewW, int viewH);

// Composite the traced rays over one stripe of the framebuffer. `yTop` is the
// stripe's first row in viewport coordinates, `h` its height.
void lg_rays_draw(uint16_t *buf, int w, int yTop, int h, const LGParams *p);

// How many rays survived seeding, for diagnostics.
int lg_rays_count(void);

#endif // LG_RAYS_H
