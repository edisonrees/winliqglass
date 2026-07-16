"""GLSL sources for the Liquid Glass renderer."""

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
# MAXS shapes, then shades the interior as a lens — a circular thickness
# profile bends the background hard at the rim with chromatic dispersion,
# frost is a real multi-tap disk blur, and the edge is defined by the
# compression of the refracted image rather than painted highlights.
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
uniform float uShadow;

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

    vec3 base = bg;
    if (uShadow > 0.001){
        float ds = field(pix - vec2(0.0, 9.0));
        float sh = exp(-max(ds, 0.0) / 22.0) * uShadow;
        base *= 1.0 - sh * smoothstep(-1.0, 3.0, d);
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

    // chromatic dispersion grows with how hard the light is being bent;
    // slight lod keeps the heavily-compressed rim from aliasing
    float disp = clamp(0.38 * clamp(uBend / 70.0, 0.0, 1.0) * uDisp, 0.0, 2.4);
    float aaLod = clamp(log2(1.0 + prof * uBend * 0.10), 0.0, 2.5);
    vec3 refr;
    refr.r = textureLod(uBg, bgUV(vUV + offUV * (1.0 + disp)), aaLod).r;
    refr.g = textureLod(uBg, bgUV(vUV + offUV               ), aaLod).g;
    refr.b = textureLod(uBg, bgUV(vUV + offUV * (1.0 - disp)), aaLod).b;

    if (uFrost > 0.5){
        vec3 soft = blurSample(vUV + offUV, uFrost, pix);
        refr = mix(refr, soft, clamp((uFrost - 0.5) / 2.0, 0.0, 1.0));
    }

    float lum = dot(refr, vec3(0.299, 0.587, 0.114));
    vec3 milk = mix(vec3(lum), vec3(1.0), 0.35);
    vec3 glass = mix(refr, milk, uOpacity);

    vec4 tnt;
    float rimb;
    materialAt(pix, tnt, rimb);
    glass = mix(glass, tnt.rgb, clamp(tnt.a, 0.0, 1.0));

    vec2 L = normalize(vec2(-0.55, -0.83));
    float lit = dot(n, L);

    // compression shading near the rim defines the edge — no painted ring
    glass *= 1.0 - 0.10 * pow(rim, 3.0);

    // hairline edge, strength set per shape (kept near zero everywhere)
    float line = exp(-pow((d + 1.5) / 1.1, 2.0));
    glass += vec3(line * rimb * (0.45 + 0.40 * abs(lit)));

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
