"""GLSL sources for the Liquid Glass renderer.

The lens model follows what Apple's material actually does on device, read off
magnified captures of an iOS 26 lock screen:

* the rim is a *bevel* — a circular thickness profile that bends background
  light outward, hard enough near the boundary that distant background folds
  into visible compression rings;
* dispersion is spectral, not an R/B split. Sampling eight wavelengths across
  the visible band and recombining them gives the orange -> green -> cyan ->
  magenta run seen hugging high-curvature corners, and it fades to nothing in
  the flat interior;
* two lights (key above-left, fill below-right) produce the double specular
  arc and the bright hairline that traces every silhouette;
* the body tint is adaptive: it takes the polarity of whatever is behind it,
  going near-black over dark content and near-white over light content, so
  glyphs on top keep their contrast either way;
* the drop shadow, when enabled, is adaptive too — present over light
  backgrounds, almost gone over dark ones. It ships off: `uShadow` is zero and
  the rim hairline carries the separation.
"""

VERT = """
#version 330
in vec2 in_pos;
out vec2 vUV;
void main(){
    vUV = in_pos * 0.5 + 0.5;
    gl_Position = vec4(in_pos, 0.0, 1.0);
}
"""

# One pass of "liquid glass": evaluates a smooth-min SDF field over up to
# MAXS shapes, then shades the interior as a lens.
GLASS_FRAG = """
#version 330
uniform sampler2D uBg;
uniform vec2  uRes;
uniform vec2  uUvA;      // affine uv transform into the background/scene
uniform vec2  uUvB;
uniform int   uCount;
uniform float uMergeK;
uniform float uOpacity;
uniform float uFrost;    // blur radius, px
uniform float uBend;     // refraction depth, px
uniform float uEdge;     // lens band width, px
uniform float uAniso;    // horizontal bend damping (1 = isotropic)
uniform float uDisp;     // chromatic dispersion multiplier (1 = default)
uniform float uShadow;   // drop shadow strength
uniform float uShadowR;  // drop shadow softness, px
uniform float uSpec;     // specular strength
uniform float uShine;    // specular exponent
uniform vec3  uKey;      // key light direction
uniform vec3  uFill;     // fill light direction
uniform float uAdapt;    // adaptive light/dark body tint amount
uniform float uSat;      // saturation concentration
uniform float uRimLit;   // bright hairline strength
uniform vec2  uTouch;    // interactive illumination centre, px
uniform float uTouchA;   // interactive illumination strength

#define MAXS 48
uniform int   uKind[MAXS];
uniform vec2  uPos[MAXS];
uniform vec2  uSize[MAXS];
uniform float uRad[MAXS];
uniform float uRot[MAXS];
uniform vec4  uTint[MAXS];
uniform float uMerge[MAXS];
uniform float uRimB[MAXS];

in vec2 vUV;
out vec4 fragColor;

float sdCircle(vec2 p, float r){ return length(p) - r; }

float sdRRect(vec2 p, vec2 b, float r){
    r = min(r, min(b.x, b.y));
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float sdTri(vec2 p, float s){
    const float k = 1.7320508;
    p.x = abs(p.x) - s;
    p.y = p.y + s / k;
    if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) * 0.5;
    p.x -= clamp(p.x, -2.0 * s, 0.0);
    return -length(p) * sign(p.y);
}

float sdPentagon(vec2 p, float r){
    const vec3 k = vec3(0.809016994, 0.587785252, 0.726542528);
    p.x = abs(p.x);
    p -= 2.0 * min(dot(vec2(-k.x, k.y), p), 0.0) * vec2(-k.x, k.y);
    p -= 2.0 * min(dot(vec2( k.x, k.y), p), 0.0) * vec2( k.x, k.y);
    p -= vec2(clamp(p.x, -r * k.z, r * k.z), r);
    return length(p) * sign(p.y);
}

float shapeSDF(int i, vec2 pix){
    vec2 p = pix - uPos[i];
    float c = cos(uRot[i]), s = sin(uRot[i]);
    p = mat2(c, -s, s, c) * p;
    int k = uKind[i];
    if (k == 0) return sdCircle(p, uSize[i].x);
    if (k == 1) return sdRRect(p, uSize[i], uRad[i]);
    if (k == 2) return sdTri(vec2(p.x, -p.y), uSize[i].x * 0.82) - uRad[i];
    if (k == 4) return sdPentagon(vec2(p.x, -p.y), uSize[i].x * 0.85) - uRad[i];
    return abs(sdCircle(p, uSize[i].x)) - uRad[i];   // 3: selection ring
}

float smin(float a, float b, float k){
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float field(vec2 pix){
    float d = 1e6;
    for (int i = 0; i < uCount; i++){
        float k = max(uMergeK * uMerge[i], 1.0);
        d = smin(d, shapeSDF(i, pix), k);
    }
    return d;
}

void materialAt(vec2 pix, out vec4 tnt, out float rimb){
    vec4 acc = vec4(0.0);
    float racc = 0.0;
    float ws = 1e-5;
    for (int i = 0; i < uCount; i++){
        float di = shapeSDF(i, pix);
        float w = exp(-max(di, -30.0) / 22.0);
        acc += w * uTint[i];
        racc += w * uRimB[i];
        ws += w;
    }
    tnt = acc / ws;
    rimb = racc / ws;
}

vec2 bgUV(vec2 uv){ return clamp(uv * uUvA + uUvB, uUvB, uUvA + uUvB); }

float luma(vec3 c){ return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Smooth RGB response for a normalised wavelength t in [0,1]: blue at the
// short end, green mid, red long. Recombining offset taps weighted by this
// yields a continuous spectral fringe rather than three hard colour ghosts.
// The bumps have to stay narrow: widen them and each channel ends up
// averaging most of the offset range, which blurs the fringe out instead of
// separating it into colour.
vec3 spectrum(float t){
    return vec3(exp(-pow((t - 0.84) * 3.55, 2.0)),
                exp(-pow((t - 0.50) * 3.55, 2.0)),
                exp(-pow((t - 0.16) * 3.55, 2.0)));
}

// 16-tap golden-angle spiral disk blur with per-pixel rotation: smooth
// frost at any radius, no mip blockiness.
vec3 blurSample(vec2 uv, float radius, vec2 seed){
    float lod = log2(1.0 + radius * 0.5);
    float rot = 6.2831853 * fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 16; i++){
        float fi = float(i);
        float a = fi * 2.3999632 + rot;
        float r = radius * sqrt((fi + 0.5) / 16.0);
        vec2 o = vec2(cos(a), sin(a)) * r / uRes;
        acc += textureLod(uBg, bgUV(uv + o), lod).rgb;
    }
    return acc / 16.0;
}

void main(){
    vec2 pix = vec2(vUV.x, 1.0 - vUV.y) * uRes;
    float d = field(pix);
    vec3 bg = texture(uBg, bgUV(vUV)).rgb;

    // ---- adaptive drop shadow -------------------------------------------
    // Wide and soft, offset down. Apple's reads clearly over light content
    // and all but disappears over dark content, so scale it by how bright
    // the wall behind the contact point is.
    vec3 base = bg;
    if (uShadow > 0.001){
        float ds = field(pix - vec2(0.0, uShadowR * 0.42));
        float sh = exp(-max(ds, 0.0) / max(uShadowR, 1.0));
        float amb = luma(textureLod(uBg, bgUV(vUV), 7.0).rgb);
        sh *= uShadow * mix(0.35, 1.0, smoothstep(0.10, 0.55, amb));
        base *= 1.0 - sh * smoothstep(-2.0, 6.0, d);
    }

    if (d > 2.0){ fragColor = vec4(base, 1.0); return; }

    float e = 1.25;
    vec2 n = vec2(field(pix + vec2(e, 0.0)) - field(pix - vec2(e, 0.0)),
                  field(pix + vec2(0.0, e)) - field(pix - vec2(0.0, e)));
    n = normalize(n + vec2(1e-5));

    float inside = clamp(-d / uEdge, 0.0, 1.0);   // 0 at rim -> 1 deep
    float rim = 1.0 - inside;

    // circular lens profile: calm centre, asymptotically steep at the edge.
    // Zero at the medial axis, so top and bottom halves each bend their own
    // side (T->B above, B->T below) and hand over cleanly with no fold.
    float prof = 1.0 - sqrt(max(1.0 - rim * rim, 0.0));
    vec2 off = n * prof * uBend;                  // pixels, outward
    off.x *= uAniso;
    vec2 offUV = vec2(off.x, -off.y) / uRes;

    // Just enough lod to stop the compressed rim aliasing. This has to stay
    // small: at 0.10/2.5 it put the rim band on mip 2.5, which blurred the
    // silhouette into a halo — the single biggest source of haze.
    float aaLod = clamp(log2(1.0 + prof * uBend * 0.025), 0.0, 1.1);

    // ---- spectral refraction --------------------------------------------
    // Dispersion is an edge phenomenon: it tracks the lens profile, so the
    // flat interior stays clean and the fringe piles up where curvature is
    // highest (corners of a rounded rect, the poles of a capsule).
    // The angle of incidence climbs across the whole bevel, so the fringe
    // tracks `rim` rather than the much later-rising displacement profile —
    // keyed to `prof` the coloured band is barely three pixels wide.
    float dispW = pow(smoothstep(0.20, 0.98, rim), 1.15);
    float spread = 1.25 * uDisp * dispW * clamp(uBend / 60.0, 0.0, 1.6);
    vec3 plain = textureLod(uBg, bgUV(vUV + offUV), aaLod).rgb;
    vec3 refr = plain;
    if (spread > 0.004){
        vec3 acc = vec3(0.0), wsum = vec3(0.0);
        for (int i = 0; i < 8; i++){
            float t = (float(i) + 0.5) / 8.0;
            float s = 1.0 + (0.5 - t) * spread;   // short wavelengths bend more
            vec3 w = spectrum(t);
            acc += w * textureLod(uBg, bgUV(vUV + offUV * s), aaLod).rgb;
            wsum += w;
        }
        // Averaging eight taps recovers the mean but washes the colour out.
        // Split the residual and amplify only its chromatic part: the fringe
        // gets Apple's saturation without the luminance ringing that scaling
        // the whole residual would put on high-contrast edges.
        vec3 dres = (acc / wsum) - plain;
        float dl = dot(dres, vec3(0.33333));
        // Gain stays modest. It was raised to punch through the old haze;
        // against a crisp hairline the same value reads as broken chromatic
        // aberration rather than the fringe you only notice when magnified.
        refr = clamp(plain + vec3(dl) + (dres - vec3(dl)) * (1.0 + 1.5 * uDisp),
                     0.0, 1.0);
    }

    if (uFrost > 0.5){
        vec3 soft = blurSample(vUV + offUV, uFrost, pix);
        refr = mix(refr, soft, clamp((uFrost - 0.5) / 2.0, 0.0, 1.0));
    }

    // glass concentrates colour a little as it bends it
    float rl = luma(refr);
    refr = clamp(mix(vec3(rl), refr, 1.0 + uSat), 0.0, 1.0);

    // ---- adaptive body tint ----------------------------------------------
    // A wide mip of the wall behind gives the ambient level; the body takes
    // that polarity so overlaid glyphs keep contrast on any wallpaper. A
    // little of the mip's hue leaks through too: colourful content behind
    // the glass should tint it faintly, not just shift it toward black/white.
    vec3 ambCol = textureLod(uBg, bgUV(vUV), 7.0).rgb;
    float amb = luma(ambCol);
    vec3 adapt = mix(vec3(0.055, 0.060, 0.072), vec3(1.0),
                     smoothstep(0.16, 0.60, amb));
    adapt += (ambCol - vec3(amb)) * 0.15;
    vec3 body = mix(mix(vec3(rl), vec3(1.0), 0.35), adapt, uAdapt);
    vec3 glass = mix(refr, body, uOpacity);

    vec4 tnt;
    float rimb;
    materialAt(pix, tnt, rimb);
    glass = mix(glass, tnt.rgb, clamp(tnt.a, 0.0, 1.0));

    // ---- lighting ---------------------------------------------------------
    // Lift the 2D gradient into a bevel normal: flat on top, rolling over to
    // face outward at the silhouette.
    vec3 N = normalize(vec3(n * prof, max(1.0 - prof, 0.06)));
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 K = normalize(uKey);
    vec3 F = normalize(uFill);

    // Only the rolled edge reflects, and only its outer part. A flat top
    // facing the viewer returns a near-1 half-vector dot and washes the whole
    // interior; spreading the highlight inward across the bevel reads as haze
    // rather than glass. Apple's sit right on the silhouette.
    float bevel = smoothstep(0.14, 0.72, prof);
    float sKey  = pow(max(dot(normalize(K + V), N), 0.0), uShine) * bevel;
    float sFill = pow(max(dot(normalize(F + V), N), 0.0), uShine * 0.65) * bevel;
    float fres  = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 6.0);

    // over dark content the highlights have to work harder to read
    float lift = mix(1.25, 0.85, smoothstep(0.12, 0.62, amb));
    float spec = (sKey + 0.38 * sFill) * uSpec * lift;

    // compression shading near the rim defines the edge — no painted ring
    glass *= 1.0 - 0.06 * pow(rim, 5.0);
    glass += vec3(spec) + vec3(fres * 0.04 * uSpec * lift);

    // A hairline, not a halo: roughly one pixel either side of the
    // silhouette, brightest where it faces the key light.
    float line = exp(-pow((d + 0.85) / 0.60, 2.0));
    float facing = 0.34 + 0.66 * max(dot(n, normalize(K.xy)), 0.0);
    glass += vec3(line * uRimLit * facing * lift);

    // per-shape rim override (selection ring, contour hints)
    glass += vec3(line * rimb * (0.45 + 0.40 * abs(dot(n, normalize(K.xy)))));

    // interactive illumination: pressing lights the glass from the touch point
    if (uTouchA > 0.001){
        float td = length(pix - uTouch);
        glass += vec3(uTouchA * exp(-td / 70.0) * inside);
    }

    float alpha = smoothstep(1.0, -1.0, d);
    fragColor = vec4(mix(base, clamp(glass, 0.0, 1.0), alpha), 1.0);
}
"""

HUD_FRAG = """
#version 330
uniform sampler2D uTex;
in vec2 vUV;
out vec4 fragColor;
void main(){
    fragColor = texture(uTex, vUV);
}
"""
