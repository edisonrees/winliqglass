/**
 * Exact WebGL2 browser port of the renderer in:
 * C:/Users/ediso/Downloads/liqglass/alcatraz-claude/winliqglass
 *
 * The browser keeps the original two-pass model: content glass first into an
 * offscreen mipmapped scene texture, then UI glass sampling that composed
 * scene. DOM glyphs and controls sit above the canvas as the Python HUD does.
 */

const MAX_SHAPES = 48;
const GLASS_BUILD = "alcatraz-exact-hierarchical-v2";

const VERT = `#version 300 es
precision highp float;
in vec2 aPos;
out vec2 vUV;
void main(){
  vUV = aPos * 0.5 + 0.5;
  gl_Position = vec4(aPos, 0.0, 1.0);
}`;

const FRAG = `#version 300 es
precision highp float;
precision highp int;

#define MAXS 48

uniform sampler2D uBg;
uniform vec2  uRes;
uniform vec2  uUvA;
uniform vec2  uUvB;
uniform int   uCount;
uniform float uMergeK;
uniform float uOpacity;
uniform float uFrost;
uniform float uBend;
uniform float uEdge;
uniform float uAniso;
uniform float uDisp;
uniform float uShadow;
uniform float uShadowR;
uniform float uSpec;
uniform float uShine;
uniform vec3  uKey;
uniform vec3  uFill;
uniform float uAdapt;
uniform float uSat;
uniform float uRimLit;
uniform vec2  uTouch;
uniform float uTouchA;

layout(std140) uniform ShapeBlock {
  vec4 uShapeA[MAXS];
  vec4 uShapeB[MAXS];
  vec4 uShapeC[MAXS];
  vec4 uShapeD[MAXS];
};

in vec2 vUV;
out vec4 fragColor;

void shapeInfo(int i, out vec2 pos, out vec2 size, out float rad,
               out float rot, out int kind, out float merge,
               out vec4 tint, out float rimb){
  vec4 a = uShapeA[i];
  vec4 b = uShapeB[i];
  vec4 c = uShapeC[i];
  vec4 d = uShapeD[i];
  pos = a.xy;
  size = a.zw;
  rad = b.x;
  rot = b.y;
  kind = int(b.z + 0.5);
  merge = b.w;
  tint = c;
  rimb = d.x;
}

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

float shapeSDFFromInfo(vec2 pix, vec2 pos, vec2 size,
                       float rad, float rot, int kind){
  vec2 p = pix - pos;
  float c = cos(rot), s = sin(rot);
  p = mat2(c, -s, s, c) * p;
  if (kind == 0) return sdCircle(p, size.x);
  if (kind == 1) return sdRRect(p, size, rad);
  if (kind == 2) return sdTri(vec2(p.x, -p.y), size.x * 0.82) - rad;
  if (kind == 4) return sdPentagon(vec2(p.x, -p.y), size.x * 0.85) - rad;
  return abs(sdCircle(p, size.x)) - rad;
}

float smin(float a, float b, float k){
  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return mix(b, a, h) - k * h * (1.0 - h);
}

float field(vec2 pix){
  float d = 1e6;
  for (int i = 0; i < MAXS; i++){
    if (i >= uCount) break;
    vec2 pos, size;
    float rad, rot, merge, rimb;
    int kind;
    vec4 tint;
    shapeInfo(i, pos, size, rad, rot, kind, merge, tint, rimb);
    float k = max(uMergeK * merge, 1.0);
    d = smin(d, shapeSDFFromInfo(pix, pos, size, rad, rot, kind), k);
  }
  return d;
}

void materialAt(vec2 pix, out vec4 tnt, out float rimb){
  vec4 acc = vec4(0.0);
  float racc = 0.0;
  float ws = 1e-5;
  for (int i = 0; i < MAXS; i++){
    if (i >= uCount) break;
    vec2 pos, size;
    float rad, rot, merge, rb;
    int kind;
    vec4 tint;
    shapeInfo(i, pos, size, rad, rot, kind, merge, tint, rb);
    float di = shapeSDFFromInfo(pix, pos, size, rad, rot, kind);
    float w = exp(-max(di, -30.0) / 22.0);
    acc += w * tint;
    racc += w * rb;
    ws += w;
  }
  tnt = acc / ws;
  rimb = racc / ws;
}

vec2 bgUV(vec2 uv){ return clamp(uv * uUvA + uUvB, uUvB, uUvA + uUvB); }
float luma(vec3 c){ return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 spectrum(float t){
  return vec3(exp(-pow((t - 0.84) * 3.55, 2.0)),
              exp(-pow((t - 0.50) * 3.55, 2.0)),
              exp(-pow((t - 0.16) * 3.55, 2.0)));
}

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

  float inside = clamp(-d / uEdge, 0.0, 1.0);
  float rim = 1.0 - inside;
  float prof = 1.0 - sqrt(max(1.0 - rim * rim, 0.0));
  vec2 off = n * prof * uBend;
  off.x *= uAniso;
  vec2 offUV = vec2(off.x, -off.y) / uRes;
  float aaLod = clamp(log2(1.0 + prof * uBend * 0.025), 0.0, 1.1);

  float dispW = pow(smoothstep(0.20, 0.98, rim), 1.15);
  float spread = 1.25 * uDisp * dispW * clamp(uBend / 60.0, 0.0, 1.6);
  vec3 plain = textureLod(uBg, bgUV(vUV + offUV), aaLod).rgb;
  vec3 refr = plain;
  if (spread > 0.004){
    vec3 acc = vec3(0.0), wsum = vec3(0.0);
    for (int i = 0; i < 8; i++){
      float t = (float(i) + 0.5) / 8.0;
      float s = 1.0 + (0.5 - t) * spread;
      vec3 w = spectrum(t);
      acc += w * textureLod(uBg, bgUV(vUV + offUV * s), aaLod).rgb;
      wsum += w;
    }
    vec3 dres = (acc / wsum) - plain;
    float dl = dot(dres, vec3(0.33333));
    refr = clamp(plain + vec3(dl) + (dres - vec3(dl)) * (1.0 + 1.5 * uDisp), 0.0, 1.0);
  }

  if (uFrost > 0.5){
    vec3 soft = blurSample(vUV + offUV, uFrost, pix);
    refr = mix(refr, soft, clamp((uFrost - 0.5) / 2.0, 0.0, 1.0));
  }

  float rl = luma(refr);
  refr = clamp(mix(vec3(rl), refr, 1.0 + uSat), 0.0, 1.0);
  float amb = luma(textureLod(uBg, bgUV(vUV), 7.0).rgb);
  vec3 adapt = mix(vec3(0.055, 0.060, 0.072), vec3(1.0), smoothstep(0.16, 0.60, amb));
  vec3 body = mix(mix(vec3(rl), vec3(1.0), 0.35), adapt, uAdapt);
  vec3 glass = mix(refr, body, uOpacity);

  vec4 tnt;
  float rimb;
  materialAt(pix, tnt, rimb);
  glass = mix(glass, tnt.rgb, clamp(tnt.a, 0.0, 1.0));

  vec3 N = normalize(vec3(n * prof, max(1.0 - prof, 0.06)));
  vec3 V = vec3(0.0, 0.0, 1.0);
  vec3 K = normalize(uKey);
  vec3 F = normalize(uFill);
  float bevel = smoothstep(0.14, 0.72, prof);
  float sKey = pow(max(dot(normalize(K + V), N), 0.0), uShine) * bevel;
  float sFill = pow(max(dot(normalize(F + V), N), 0.0), uShine * 0.65) * bevel;
  float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 6.0);
  float lift = mix(1.25, 0.85, smoothstep(0.12, 0.62, amb));
  float spec = (sKey + 0.38 * sFill) * uSpec * lift;

  glass *= 1.0 - 0.06 * pow(rim, 5.0);
  glass += vec3(spec) + vec3(fres * 0.04 * uSpec * lift);
  float line = exp(-pow((d + 0.85) / 0.60, 2.0));
  float facing = 0.34 + 0.66 * max(dot(n, normalize(K.xy)), 0.0);
  glass += vec3(line * uRimLit * facing * lift);
  glass += vec3(line * rimb * (0.45 + 0.40 * abs(dot(n, normalize(K.xy)))));

  if (uTouchA > 0.001){
    float td = length(pix - uTouch);
    glass += vec3(uTouchA * exp(-td / 70.0) * inside);
  }

  float alpha = smoothstep(1.0, -1.0, d);
  fragColor = vec4(mix(base, clamp(glass, 0.0, 1.0), alpha), 1.0);
}`;

function compile(gl, type, source) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    const message = gl.getShaderInfoLog(shader) || "shader compile failed";
    gl.deleteShader(shader);
    throw new Error(message);
  }
  return shader;
}

function createProgram(gl) {
  const program = gl.createProgram();
  gl.attachShader(program, compile(gl, gl.VERTEX_SHADER, VERT));
  gl.attachShader(program, compile(gl, gl.FRAGMENT_SHADER, FRAG));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    throw new Error(gl.getProgramInfoLog(program) || "program link failed");
  }
  return program;
}

function sourceParams(glass, isUi) {
  if (isUi) {
    return {
      opacity: 0.10 + 0.18 * glass,
      frost: 0,
      bend: 21,
      mergeK: 26,
      edge: 7,
      aniso: 0.80,
      disp: 0.85,
      shadow: 0,
      shadowR: 9,
      spec: 0.34,
      shine: 26,
      adapt: 0.48,
      sat: 0.06,
      rimLit: 0.34,
    };
  }
  return {
    opacity: glass,
    frost: 0,
    bend: 0.42 * 88,
    mergeK: 4 + 0.34 * 86,
    edge: 12,
    aniso: 1,
    disp: 0.70,
    shadow: 0,
    shadowR: 14,
    spec: 0.30,
    shine: 22,
    adapt: 0.50,
    sat: 0.07,
    rimLit: 0.30,
  };
}

function normalizedShape(rect) {
  return {
    kind: rect.kind ?? 1,
    x: rect.x + rect.w / 2,
    y: rect.y + rect.h / 2,
    hw: rect.w / 2,
    hh: rect.h / 2,
    rad: rect.rad ?? Math.min(rect.w, rect.h) * 0.22,
    rot: rect.rot ?? 0,
    merge: rect.merge ? 1 : 0,
    rim: rect.rim ?? 0,
    tint: rect.tint ?? [1, 1, 1, 0],
  };
}

export class WinLiqGlass {
  constructor(canvas, opts = {}) {
    const gl = canvas.getContext("webgl2", {
      alpha: false,
      antialias: false,
      depth: false,
      stencil: false,
      preserveDrawingBuffer: false,
      powerPreference: "high-performance",
    });
    if (!gl) throw new Error("The exact Liquid Glass renderer requires WebGL2");

    this.canvas = canvas;
    this.gl = gl;
    this.isWebGL2 = true;
    this.build = GLASS_BUILD;
    this.program = createProgram(gl);
    this.glass = opts.intensity ?? 0.10;
    this.startedAt = performance.now();
    this.material = { frost: 0, bend: 0.42, merge: 0.34 };
    this.touch = [-10000, -10000, 0];
    this.contentShapes = [];
    this.uiShapes = [];
    this.topShapes = [];
    this.cssW = 1;
    this.cssH = 1;
    this.bgWidth = 1;
    this.bgHeight = 1;
    this._locations = new Map();

    this.buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);

    this.bgTexture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.bgTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([20, 30, 45, 255]));
    gl.generateMipmap(gl.TEXTURE_2D);

    this.shapeData = new Float32Array(MAX_SHAPES * 4 * 4);
    this.contentShapeBuffer = gl.createBuffer();
    this.uiShapeBuffer = gl.createBuffer();
    this.topShapeBuffer = gl.createBuffer();
    for (const shapeBuffer of [this.contentShapeBuffer, this.uiShapeBuffer, this.topShapeBuffer]) {
      gl.bindBuffer(gl.UNIFORM_BUFFER, shapeBuffer);
      gl.bufferData(gl.UNIFORM_BUFFER, this.shapeData.byteLength, gl.DYNAMIC_DRAW);
    }
    const shapeBlock = gl.getUniformBlockIndex(this.program, "ShapeBlock");
    if (shapeBlock === gl.INVALID_INDEX || shapeBlock === 0xffffffff) {
      throw new Error("Liquid Glass shape uniform block is unavailable");
    }
    gl.uniformBlockBinding(this.program, shapeBlock, 0);
    gl.bindBufferBase(gl.UNIFORM_BUFFER, 0, this.contentShapeBuffer);

    this.sceneTexture = null;
    this.sceneFramebuffer = null;
    this.surfaceTexture = null;
    this.surfaceFramebuffer = null;
    this.resize(1, 1);
  }

  _loc(name) {
    if (!this._locations.has(name)) this._locations.set(name, this.gl.getUniformLocation(this.program, name));
    return this._locations.get(name);
  }

  _targets(width, height) {
    const gl = this.gl;
    for (const texture of [this.sceneTexture, this.surfaceTexture]) {
      if (texture) gl.deleteTexture(texture);
    }
    for (const framebuffer of [this.sceneFramebuffer, this.surfaceFramebuffer]) {
      if (framebuffer) gl.deleteFramebuffer(framebuffer);
    }

    this.sceneTexture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.sceneTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);

    this.sceneFramebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.sceneFramebuffer);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, this.sceneTexture, 0);
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
      throw new Error("Liquid Glass scene framebuffer is incomplete");
    }

    this.surfaceTexture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.surfaceTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);

    this.surfaceFramebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.surfaceFramebuffer);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, this.surfaceTexture, 0);
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
      throw new Error("Liquid Glass surface framebuffer is incomplete");
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  resize(width, height) {
    const w = Math.max(2, Math.round(width));
    const h = Math.max(2, Math.round(height));
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
      this.canvas.style.width = `${width}px`;
      this.canvas.style.height = `${height}px`;
      this._targets(w, h);
    }
    this.cssW = width;
    this.cssH = height;
    this.dpr = 1;
  }

  setBackground(image) {
    const gl = this.gl;
    this.bgWidth = Math.max(1, image.naturalWidth || image.videoWidth || image.width || 1);
    this.bgHeight = Math.max(1, image.naturalHeight || image.videoHeight || image.height || 1);
    gl.bindTexture(gl.TEXTURE_2D, this.bgTexture);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
    gl.generateMipmap(gl.TEXTURE_2D);
  }

  setLayers(content, ui, top = []) {
    this.contentShapes = content.slice(0, MAX_SHAPES).map(normalizedShape);
    this.uiShapes = ui.slice(0, MAX_SHAPES).map(normalizedShape);
    this.topShapes = top.slice(0, MAX_SHAPES).map(normalizedShape);
    this._writeShapeBuffer(this.contentShapeBuffer, this.contentShapes);
    this._writeShapeBuffer(this.uiShapeBuffer, this.uiShapes);
    this._writeShapeBuffer(this.topShapeBuffer, this.topShapes);
    this.canvas.dataset.glassBuild = this.build;
    this.canvas.dataset.contentShapes = String(this.contentShapes.length);
    this.canvas.dataset.uiShapes = String(this.uiShapes.length);
    this.canvas.dataset.topShapes = String(this.topShapes.length);
    this.canvas.dataset.blueShapes = String(this.uiShapes.filter((shape) => shape.tint[2] === 1 && shape.tint[0] === 0.04).length);
  }

  setChromeShapes(shapes) {
    this.setLayers(shapes, []);
  }

  setIntensity(value) {
    this.glass = Math.max(0, Math.min(1, value));
  }

  setMaterial(partial = {}) {
    for (const key of ["frost", "bend", "merge"]) {
      if (Number.isFinite(partial[key])) this.material[key] = Math.max(0, Math.min(1, partial[key]));
    }
    if (Number.isFinite(partial.glass)) this.setIntensity(partial.glass);
  }

  setParams(partial = {}) {
    if (Number.isFinite(partial.opacity)) this.setIntensity(partial.opacity);
  }

  setGlobalTint() {
    // The source renderer has no global chromatic wash.
  }

  /**
   * Multiplies the ported material constants rather than replacing them, so
   * the defaults stay exactly as the original renderer computed them and 1.0
   * on every dial is the untouched look.
   */
  setOverrides(partial = {}) {
    this.overrides = { ...(this.overrides || {}), ...partial };
  }

  _applyOverrides(params) {
    const o = this.overrides;
    if (!o) return params;
    const scale = (key, factor) => {
      if (Number.isFinite(factor)) params[key] = params[key] * factor;
    };
    scale("bend", o.bend);
    scale("edge", o.edge);
    scale("spec", o.spec);
    scale("shine", o.shine);
    scale("rimLit", o.rim);
    scale("disp", o.disp);
    if (Number.isFinite(o.opacity)) params.opacity = params.opacity * o.opacity;
    return params;
  }

  setTouch(x, y, amount = 0) {
    this.touch = [x, y, Math.max(0, Math.min(1, amount))];
  }

  _writeShapeBuffer(buffer, shapes) {
    const gl = this.gl;
    this.shapeData.fill(0);
    const n = Math.min(shapes.length, MAX_SHAPES);
    const rowStride = MAX_SHAPES * 4;
    for (let i = 0; i < n; i++) {
      const shape = shapes[i];
      let offset = i * 4;
      this.shapeData.set([shape.x, shape.y, shape.hw, shape.hh], offset);
      offset = rowStride + i * 4;
      this.shapeData.set([shape.rad, shape.rot, shape.kind, shape.merge], offset);
      offset = rowStride * 2 + i * 4;
      this.shapeData.set(shape.tint, offset);
      offset = rowStride * 3 + i * 4;
      this.shapeData[offset] = shape.rim;
    }
    gl.bindBuffer(gl.UNIFORM_BUFFER, buffer);
    gl.bufferSubData(gl.UNIFORM_BUFFER, 0, this.shapeData);
  }

  _bindShapes(buffer, count) {
    const gl = this.gl;
    gl.bindBufferBase(gl.UNIFORM_BUFFER, 0, buffer);
    gl.uniform1i(this._loc("uCount"), Math.min(count, MAX_SHAPES));
  }

  _lightVectors() {
    const seconds = (performance.now() - this.startedAt) / 1000;
    const angle = (-125 * Math.PI) / 180 + 0.22 * Math.sin(seconds * 0.45);
    const x = Math.cos(angle);
    const y = Math.sin(angle);
    return {
      key: [x * 0.62, y * 0.62, 0.78],
      fill: [-x * 0.55, -y * 0.55, 0.62],
    };
  }

  _draw(backgroundTexture, shapeBuffer, shapeCount, params, framebuffer, uvA = [1, 1], uvB = [0, 0], lights = this._lightVectors()) {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.disable(gl.BLEND);
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(this.program);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
    const pos = gl.getAttribLocation(this.program, "aPos");
    gl.enableVertexAttribArray(pos);
    gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 0, 0);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, backgroundTexture);
    gl.uniform1i(this._loc("uBg"), 0);
    this._bindShapes(shapeBuffer, shapeCount);

    gl.uniform2f(this._loc("uRes"), this.cssW, this.cssH);
    gl.uniform2f(this._loc("uUvA"), uvA[0], uvA[1]);
    gl.uniform2f(this._loc("uUvB"), uvB[0], uvB[1]);
    gl.uniform1f(this._loc("uMergeK"), params.mergeK);
    gl.uniform1f(this._loc("uOpacity"), params.opacity);
    gl.uniform1f(this._loc("uFrost"), params.frost);
    gl.uniform1f(this._loc("uBend"), params.bend);
    gl.uniform1f(this._loc("uEdge"), params.edge);
    gl.uniform1f(this._loc("uAniso"), params.aniso);
    gl.uniform1f(this._loc("uDisp"), params.disp);
    gl.uniform1f(this._loc("uShadow"), params.shadow);
    gl.uniform1f(this._loc("uShadowR"), params.shadowR);
    gl.uniform1f(this._loc("uSpec"), params.spec);
    gl.uniform1f(this._loc("uShine"), params.shine);
    gl.uniform3f(this._loc("uKey"), ...lights.key);
    gl.uniform3f(this._loc("uFill"), ...lights.fill);
    gl.uniform1f(this._loc("uAdapt"), params.adapt);
    gl.uniform1f(this._loc("uSat"), params.sat);
    gl.uniform1f(this._loc("uRimLit"), params.rimLit);
    gl.uniform2f(this._loc("uTouch"), this.touch[0], this.touch[1]);
    gl.uniform1f(this._loc("uTouchA"), this.touch[2] * 0.16);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  render() {
    const gl = this.gl;
    const content = this._applyOverrides(sourceParams(this.glass, false));
    const ui = this._applyOverrides(sourceParams(this.glass, true));
    content.frost = this.material.frost * 22;
    content.bend = this.material.bend * 88;
    content.mergeK = 4 + this.material.merge * 86;

    // Literal engine.py cover_affine() for the source wallpaper pass.
    const ratio = (this.cssW / this.cssH) / (this.bgWidth / this.bgHeight);
    const uvA = ratio > 1 ? [1, 1 / ratio] : [ratio, 1];
    const uvB = [(1 - uvA[0]) / 2, (1 - uvA[1]) / 2];
    const lights = this._lightVectors();
    this._draw(this.bgTexture, this.contentShapeBuffer, this.contentShapes.length, content, this.sceneFramebuffer, uvA, uvB, lights);
    gl.bindTexture(gl.TEXTURE_2D, this.sceneTexture);
    gl.generateMipmap(gl.TEXTURE_2D);
    if (this.topShapes.length) {
      this._draw(this.sceneTexture, this.uiShapeBuffer, this.uiShapes.length, ui, this.surfaceFramebuffer, [1, 1], [0, 0], lights);
      gl.bindTexture(gl.TEXTURE_2D, this.surfaceTexture);
      gl.generateMipmap(gl.TEXTURE_2D);
      this._draw(this.surfaceTexture, this.topShapeBuffer, this.topShapes.length, ui, null, [1, 1], [0, 0], lights);
    } else {
      this._draw(this.sceneTexture, this.uiShapeBuffer, this.uiShapes.length, ui, null, [1, 1], [0, 0], lights);
    }
  }
}

export const WINLIQGLASS_SOURCE = {
  directory: "C:/Users/ediso/Downloads/liqglass/alcatraz-claude/winliqglass",
  renderer: "shaders.py + engine.py",
  passes: "2, with an exact third pass only for nested knobs",
  maxShapesPerPass: MAX_SHAPES,
  build: GLASS_BUILD,
  note: "Exact material equations and content/UI defaults; shape records use a std140 uniform block for cached ANGLE access.",
};
