#include "lg_field.h"

// Pentagon constants (iq's, same as engine.py's _PK).
static const fx PK_X = FX(0.809016994);
static const fx PK_Y = FX(0.587785252);
static const fx PK_Z = FX(0.726542528);

static inline fx sd_circle(fx px, fx py, fx r) {
    return fxhypot(px, py) - r;
}

static inline fx sd_rrect(fx px, fx py, fx bx, fx by, fx r) {
    if (r > bx) r = bx;
    if (r > by) r = by;
    fx qx = fxabs(px) - bx + r;
    fx qy = fxabs(py) - by + r;
    fx outside = fxhypot(fxmax(qx, 0), fxmax(qy, 0));
    fx inside = fxmin(fxmax(qx, qy), 0);
    return outside + inside - r;
}

static inline fx sd_tri(fx px, fx py, fx s) {
    const fx k = FX(1.7320508);
    px = fxabs(px) - s;
    py = py + fxd(s, k);
    if (px + fxm(k, py) > 0) {
        fx nx = fxm(px - fxm(k, py), FX_HALF);
        fx ny = fxm(-fxm(k, px) - py, FX_HALF);
        px = nx; py = ny;
    }
    px -= fxclamp(px, -2 * s, 0);
    fx d = fxhypot(px, py);
    return py > 0 ? -d : d;
}

static inline fx sd_pentagon(fx px, fx py, fx r) {
    px = fxabs(px);
    fx dp = fxmin(fxm(-PK_X, px) + fxm(PK_Y, py), 0);
    px -= fxm(2 * dp, -PK_X);
    py -= fxm(2 * dp, PK_Y);
    dp = fxmin(fxm(PK_X, px) + fxm(PK_Y, py), 0);
    px -= fxm(2 * dp, PK_X);
    py -= fxm(2 * dp, PK_Y);
    px -= fxclamp(px, -fxm(r, PK_Z), fxm(r, PK_Z));
    py -= r;
    fx d = fxhypot(px, py);
    return py > 0 ? d : -d;
}

fx lg_shape_sdf(const LGShape *s, fx px, fx py) {
    fx dx = px - s->x;
    fx dy = py - s->y;
    if (s->rot != 0) {
        fx c = fxcos(s->rot), sn = fxsin(s->rot);
        fx rx = fxm(c, dx) - fxm(sn, dy);
        fx ry = fxm(sn, dx) + fxm(c, dy);
        dx = rx; dy = ry;
    }
    switch (s->kind) {
        case LG_CIRCLE: return sd_circle(dx, dy, s->hw);
        case LG_RRECT:  return sd_rrect(dx, dy, s->hw, s->hh, s->rad);
        case LG_TRI:    return sd_tri(dx, -dy, fxm(s->hw, FX(0.82))) - s->rad;
        case LG_PENT:   return sd_pentagon(dx, -dy, fxm(s->hw, FX(0.85))) - s->rad;
        default:        return fxabs(sd_circle(dx, dy, s->hw)) - s->rad;
    }
}

static inline fx smin(fx a, fx b, fx k) {
    fx h = fxsat(FX_HALF + fxm(FX_HALF, fxd(b - a, k)));
    return fxlerp(b, a, h) - fxm(k, fxm(h, FX_ONE - h));
}

fx lg_field(const LGScene *sc, fx px, fx py) {
    fx d = FX(4000.0);
    for (int i = 0; i < sc->count; i++) {
        const LGShape *s = &sc->shapes[i];
        fx k = s->merge ? sc->p.mergeK : FX_ONE;
        if (k < FX_ONE) k = FX_ONE;
        fx di = lg_shape_sdf(s, px, py);
        d = smin(d, di, k);
    }
    return d;
}

void lg_field_normal(const LGScene *sc, fx px, fx py, fx *nx, fx *ny) {
    const fx e = FX(1.25);
#if LG_CENTRAL_DIFF
    fx gx = lg_field(sc, px + e, py) - lg_field(sc, px - e, py);
    fx gy = lg_field(sc, px, py + e) - lg_field(sc, px, py - e);
#else
    fx d0 = lg_field(sc, px, py);
    fx gx = lg_field(sc, px + e, py) - d0;
    fx gy = lg_field(sc, px, py + e) - d0;
#endif
    if (gx == 0 && gy == 0) gx = 1;
    fxnormalize(&gx, &gy);
    *nx = gx;
    *ny = gy;
}

void lg_material_at(const LGScene *sc, fx px, fx py,
                    fx *tr, fx *tg, fx *tb, fx *ta, fx *rimb) {
    fx ar = 0, ag = 0, ab = 0, aa = 0, arim = 0;
    fx ws = FX(0.00002);
    for (int i = 0; i < sc->count; i++) {
        const LGShape *s = &sc->shapes[i];
        fx di = lg_shape_sdf(s, px, py);
        if (di < FX(-30.0)) di = FX(-30.0);
        // exp(-d/22): a soft, wide weight so a tint fades across a merge
        // instead of switching at the seam.
        fx w = fxexp_neg(fxd(di, FX(22.0)));
        ar += fxm(w, s->tintR);
        ag += fxm(w, s->tintG);
        ab += fxm(w, s->tintB);
        aa += fxm(w, s->tintA);
        arim += fxm(w, s->rim);
        ws += w;
    }
    *tr = fxd(ar, ws);
    *tg = fxd(ag, ws);
    *tb = fxd(ab, ws);
    *ta = fxd(aa, ws);
    *rimb = fxd(arim, ws);
}

fx lg_shape_bound(const LGShape *s, const LGParams *p) {
    fx r = fxhypot(s->hw, s->hh) + s->rad;
    // The smooth-min pulls the surface outward by up to k, and the lens band
    // plus the hairline live outside that again.
    if (s->merge) r += p->mergeK;
    return r + p->edge + FX(4.0);
}

void lg_field_project(const LGScene *sc, fx *px, fx *py, fx target, int iters) {
    for (int i = 0; i < iters; i++) {
        fx d = lg_field(sc, *px, *py);
        fx err = d - target;
        if (fxabs(err) < FX(0.05)) break;
        fx nx, ny;
        lg_field_normal(sc, *px, *py, &nx, &ny);
        // Under-relax: the smooth-min field is not unit-gradient near a merge,
        // so a full Newton step there overshoots and rings.
        fx step = fxm(err, FX(0.85));
        *px -= fxm(nx, step);
        *py -= fxm(ny, step);
    }
}
