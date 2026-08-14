// Q16.16 fixed-point scalar math for the Liquid Glass port.
//
// The desktop renderer is a fragment shader, so every quantity there is a
// float and the GPU does not care. A microcontroller does: an AVR has no FPU
// at all, and even on an ESP32 the point of the port is a renderer whose cost
// is predictable to the cycle. So the whole pipeline runs on int32 in Q16.16 —
// one format everywhere, no float library linked in, identical results on
// every board.
//
// Range: +-32767.99998, step 1/65536. Screen coordinates and SDF distances are
// pixels, so a 320px display uses 1% of the range and leaves plenty of headroom
// for intermediate products.

#ifndef LG_FIXED_H
#define LG_FIXED_H

#include <stdint.h>

typedef int32_t fx;

#define FX_SHIFT 16
#define FX_ONE   ((fx)0x00010000)
#define FX_HALF  ((fx)0x00008000)
#define FX_MAXV  ((fx)0x7FFFFFFF)

// Literal constructor. The double math folds at compile time, so no floating
// point survives into the object file — check the map file if you doubt it.
#define FX(x) ((fx)((double)(x) * 65536.0 + ((x) < 0 ? -0.5 : 0.5)))

// ---------------------------------------------------------------------------
// core ops
// ---------------------------------------------------------------------------

// The 64-bit intermediate is what keeps a*b honest when both operands are
// large. On AVR gcc synthesises it from 16x16 multiplies and it is the single
// most expensive thing in the renderer; on anything 32-bit it is a couple of
// instructions.
static inline fx fxm(fx a, fx b) {
    return (fx)(((int64_t)a * (int64_t)b) >> FX_SHIFT);
}

static inline fx fxd(fx a, fx b) {
    if (b == 0) return a >= 0 ? FX_MAXV : -FX_MAXV;
    return (fx)((((int64_t)a) << FX_SHIFT) / (int64_t)b);
}

static inline fx fxabs(fx a)          { return a < 0 ? -a : a; }
static inline fx fxmin(fx a, fx b)    { return a < b ? a : b; }
static inline fx fxmax(fx a, fx b)    { return a > b ? a : b; }
static inline fx fxclamp(fx v, fx lo, fx hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline fx fxsat(fx v)          { return fxclamp(v, 0, FX_ONE); }
static inline fx fxlerp(fx a, fx b, fx t) { return a + fxm(b - a, t); }

static inline fx fxfromint(int32_t i) { return (fx)i << FX_SHIFT; }
static inline int32_t fxfloor_i(fx a) { return (int32_t)(a >> FX_SHIFT); }
static inline int32_t fxround_i(fx a) { return (int32_t)((a + FX_HALF) >> FX_SHIFT); }
static inline fx fxfrac(fx a)         { return a & 0xFFFF; }

// ---------------------------------------------------------------------------
// roots
// ---------------------------------------------------------------------------

// Classic restoring integer square root. Exact, no table, no division.
static inline uint32_t lg_isqrt64(uint64_t v) {
    uint64_t res = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; }
        else                { res >>= 1; }
        bit >>= 2;
    }
    return (uint32_t)res;
}

static inline fx fxsqrt(fx a) {
    if (a <= 0) return 0;
    // sqrt(a * 2^16) in Q16.16 == isqrt(a * 2^32) >> 0, since 2^32 = (2^16)^2.
    return (fx)lg_isqrt64(((uint64_t)(uint32_t)a) << FX_SHIFT);
}

// hypot without ever forming a Q16.16 square, which would overflow past 181px.
// Squaring the raw ints gives Q32.32; the integer sqrt of that lands back in
// Q16.16 directly.
static inline fx fxhypot(fx a, fx b) {
    uint64_t s = (uint64_t)((int64_t)a * (int64_t)a) + (uint64_t)((int64_t)b * (int64_t)b);
    return (fx)lg_isqrt64(s);
}

// Unit-length (dx, dy). Degenerate input resolves to +x rather than NaN.
static inline void fxnormalize(fx *x, fx *y) {
    fx len = fxhypot(*x, *y);
    if (len < 4) { *x = FX_ONE; *y = 0; return; }
    *x = fxd(*x, len);
    *y = fxd(*y, len);
}

// ---------------------------------------------------------------------------
// trig, angles measured in turns
// ---------------------------------------------------------------------------
//
// Turns rather than radians because wrapping is then a bitwise AND on the
// fractional part: no fmod, no range reduction, no table.

// sin(pi/2 * w) for w in [0,1]. Minimax polynomial, |err| < 2e-4 — a tenth of
// a Q16.16 step at the amplitudes we use it at.
static inline fx lg_sin_quarter(fx w) {
    fx w2 = fxm(w, w);
    fx r = FX(-0.00467376);
    r = FX(0.07968968) + fxm(w2, r);
    r = FX(-0.64596371) + fxm(w2, r);
    r = FX(1.57079633) + fxm(w2, r);
    return fxm(w, r);
}

static inline fx fxsin(fx turns) {
    uint32_t f = (uint32_t)(turns & 0xFFFF);   // fraction of a turn, Q16
    uint32_t quad = f >> 14;                   // which quarter
    fx w = (fx)((f & 0x3FFF) << 2);            // position in quarter, Q16.16
    switch (quad) {
        case 0:  return  lg_sin_quarter(w);
        case 1:  return  lg_sin_quarter(FX_ONE - w);
        case 2:  return -lg_sin_quarter(w);
        default: return -lg_sin_quarter(FX_ONE - w);
    }
}

static inline fx fxcos(fx turns) { return fxsin(turns + FX(0.25)); }

// ---------------------------------------------------------------------------
// curves
// ---------------------------------------------------------------------------

// exp(-x) for x >= 0. Integer part by repeated halving through a small table,
// fraction by a 5-term series. Used for the rim hairline and every falloff, so
// it has to be cheap rather than exact.
static inline fx fxexp_neg(fx x) {
    if (x <= 0) return FX_ONE;
    if (x >= FX(11.0)) return 0;
    static const fx kE[12] = {
        FX(1.0),        FX(0.36787944), FX(0.13533528), FX(0.04978707),
        FX(0.01831564), FX(0.00673795), FX(0.00247875), FX(0.00091188),
        FX(0.00033546), FX(0.00012341), FX(0.00004540), FX(0.00001670)
    };
    int32_t i = fxfloor_i(x);
    fx f = fxfrac(x);
    // 1 - f + f^2/2 - f^3/6 + f^4/24 - f^5/120, Horner
    fx r = FX(-0.00833333);
    r = FX(0.04166667) + fxm(f, r);
    r = FX(-0.16666667) + fxm(f, r);
    r = FX(0.5) + fxm(f, r);
    r = FX(-1.0) + fxm(f, r);
    r = FX_ONE + fxm(f, r);
    return fxm(kE[i], r);
}

// GLSL smoothstep.
static inline fx fxsmoothstep(fx e0, fx e1, fx x) {
    if (e1 == e0) return x < e0 ? 0 : FX_ONE;
    fx t = fxsat(fxd(x - e0, e1 - e0));
    return fxm(fxm(t, t), FX(3.0) - fxm(FX(2.0), t));
}

static inline fx fxpow5(fx x)  { fx x2 = fxm(x, x); return fxm(fxm(x2, x2), x); }

// x^n for small integer n. The specular exponent is the only caller and it
// wants n in the twenties, so square-and-multiply rather than exp/log.
static inline fx fxpowi(fx x, int32_t n) {
    fx r = FX_ONE;
    while (n > 0) {
        if (n & 1) r = fxm(r, x);
        x = fxm(x, x);
        n >>= 1;
        if (x == 0) break;
    }
    return r;
}

#endif // LG_FIXED_H
