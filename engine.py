"""Shape model + two-pass GPU liquid-glass renderer.

Pass 1 renders the background image with the *content* glass layer into an
offscreen framebuffer; pass 2 renders the *UI* glass layer on top, sampling
pass 1 — so knobs and panels genuinely refract the scene beneath them.
A Pillow-drawn HUD is alpha-blended last.

The renderer can draw into a sub-viewport (used by the Lockscreen phone
screen): shape coordinates and `uRes` become viewport-local, and pass 2
samples the matching sub-rectangle of the pass-1 texture via an affine
UV transform (uUvA/uUvB).
"""

import math
from dataclasses import dataclass

import moderngl
import numpy as np

from shaders import VERT, GLASS_FRAG, HUD_FRAG

MAXS = 48
KIND_CIRCLE, KIND_RRECT, KIND_TRI, KIND_RING, KIND_PENT = 0, 1, 2, 3, 4

_PK = (0.809016994, 0.587785252, 0.726542528)   # pentagon constants (iq)


def _smoothstep(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def squircle_amount(hw, hh, rad):
    """How continuous-curvature a box's corners are: 1 = superellipse, 0 = arc.

    Mirrors `squircleAmt` in shaders.py. Only partial-radius corners qualify —
    once the radius reaches the short half-axis the shape is a capsule or a
    circle, whose caps really are semicircular.
    """
    r = min(rad, hw, hh)
    return 1.0 - _smoothstep(0.72, 0.99, r / max(min(hw, hh), 1e-4))


@dataclass
class Shape:
    kind: int = KIND_CIRCLE
    x: float = 0.0
    y: float = 0.0
    hw: float = 40.0          # half width (circle/ring: radius)
    hh: float = 40.0          # half height
    rad: float = 0.0          # corner rounding (ring: half thickness)
    rot: float = 0.0
    tint: tuple = (1.0, 1.0, 1.0, 0.0)
    merge: bool = True        # participates in metaball smooth-min
    scale: float = 1.0        # transient pop/hover scale
    rim: float = 0.0          # hairline edge: >0 light, <0 dark contour

    def sdf(self, px: float, py: float) -> float:
        dx, dy = px - self.x, py - self.y
        c, s = math.cos(self.rot), math.sin(self.rot)
        px, py = c * dx - s * dy, s * dx + c * dy
        hw, hh, rad = self.hw * self.scale, self.hh * self.scale, self.rad * self.scale
        if self.kind == KIND_CIRCLE:
            return math.hypot(px, py) - hw
        if self.kind == KIND_RRECT:
            # Mirrors sdRRect in shaders.py, continuous corner included, so a
            # click on a card's corner agrees with the pixels drawn there.
            r = min(rad, hw, hh)
            qx, qy = abs(px) - hw + r, abs(py) - hh + r
            mx, my = max(qx, 0.0), max(qy, 0.0)
            d = math.hypot(mx, my) + min(max(qx, qy), 0.0) - r
            sq = squircle_amount(hw, hh, rad)
            if sq > 0.001 and mx > 0.0 and my > 0.0:
                mx2, my2 = mx * mx, my * my
                s = mx2 * mx2 + my2 * my2
                l4 = math.sqrt(math.sqrt(s))
                g = math.sqrt(mx2 ** 3 + my2 ** 3)
                d += sq * ((l4 - r) * s / max(l4 * g, 1e-6) - d)
            return d
        if self.kind == KIND_TRI:
            k = 1.7320508
            sz = hw * 0.82
            px, py = abs(px) - sz, -py + sz / k
            if px + k * py > 0.0:
                px, py = (px - k * py) * 0.5, (-k * px - py) * 0.5
            px -= min(max(px, -2.0 * sz), 0.0)
            return (-math.hypot(px, py) * (1 if py > 0 else -1)) - rad
        if self.kind == KIND_PENT:
            kx, ky, kz = _PK
            r = hw * 0.85
            px, py = abs(px), -py
            for sx in (-kx, kx):
                dp = min(sx * px + ky * py, 0.0)
                px, py = px - 2.0 * dp * sx, py - 2.0 * dp * ky
            px -= min(max(px, -r * kz), r * kz)
            py -= r
            return math.hypot(px, py) * (1 if py > 0 else -1) - rad
        return abs(math.hypot(px, py) - hw) - rad

    def bound_radius(self) -> float:
        return math.hypot(self.hw, self.hh) * self.scale + self.rad


def hit_topmost(shapes, x, y):
    """Topmost shape containing the point (last drawn wins)."""
    for s in reversed(shapes):
        if s.kind != KIND_RING and s.sdf(x, y) <= 0.0:
            return s
    return None


def cover_affine(img_size, vp_size):
    """Affine (A, B) so uv*A+B cover-fits the image into the viewport."""
    iw, ih = img_size
    vw, vh = vp_size
    r = (vw / vh) / (iw / ih)
    sx, sy = (1.0, 1.0 / r) if r > 1.0 else (r, 1.0)
    return (sx, sy), ((1.0 - sx) / 2.0, (1.0 - sy) / 2.0)


class GlassRenderer:
    def __init__(self, ctx: moderngl.Context, size):
        self.ctx = ctx
        self.prog = ctx.program(vertex_shader=VERT, fragment_shader=GLASS_FRAG)
        self.hud_prog = ctx.program(vertex_shader=VERT, fragment_shader=HUD_FRAG)
        vbo = ctx.buffer(np.array([-1, -1, 3, -1, -1, 3], dtype='f4'))
        self.vao = ctx.vertex_array(self.prog, [(vbo, '2f', 'in_pos')])
        self.hud_vao = ctx.vertex_array(self.hud_prog, [(vbo, '2f', 'in_pos')])

        self.bg = None            # default background handle
        self.scene_tex = None
        self.fbo = None
        self.hud_tex = None
        self.size = None
        self.resize(size)

        self._kind = np.zeros(MAXS, dtype='i4')
        self._pos = np.zeros((MAXS, 2), dtype='f4')
        self._sizes = np.zeros((MAXS, 2), dtype='f4')
        self._rad = np.zeros(MAXS, dtype='f4')
        self._rot = np.zeros(MAXS, dtype='f4')
        self._tint = np.zeros((MAXS, 4), dtype='f4')
        self._merge = np.zeros(MAXS, dtype='f4')
        self._rimb = np.zeros(MAXS, dtype='f4')

    def resize(self, size):
        size = (max(size[0], 8), max(size[1], 8))
        if size == self.size:
            return
        self.size = size
        for obj in (self.scene_tex, self.fbo, self.hud_tex):
            if obj is not None:
                obj.release()
        self.scene_tex = self.ctx.texture(size, 3)
        self.scene_tex.filter = (moderngl.LINEAR_MIPMAP_LINEAR, moderngl.LINEAR)
        self.scene_tex.repeat_x = self.scene_tex.repeat_y = False
        self.fbo = self.ctx.framebuffer(color_attachments=[self.scene_tex])
        self.hud_tex = self.ctx.texture(size, 4)
        self.hud_tex.filter = (moderngl.NEAREST, moderngl.NEAREST)

    # ---- backgrounds

    def create_bg(self, pil_img):
        img = pil_img.convert('RGB')
        tex = self.ctx.texture(img.size, 3, img.transpose(1).tobytes())
        tex.build_mipmaps()
        tex.filter = (moderngl.LINEAR_MIPMAP_LINEAR, moderngl.LINEAR)
        tex.repeat_x = tex.repeat_y = False
        return {'tex': tex, 'size': img.size}

    def release_bg(self, handle):
        if handle is not None:
            handle['tex'].release()

    def set_background(self, pil_img):
        self.release_bg(self.bg)
        self.bg = self.create_bg(pil_img)

    def update_hud(self, pil_rgba):
        self.hud_tex.write(pil_rgba.transpose(1).tobytes())

    # ---- drawing

    def _set(self, name, value):
        try:
            self.prog[name].value = value
        except KeyError:
            pass

    def _upload(self, shapes, params):
        n = min(len(shapes), MAXS)
        for i in range(n):
            s = shapes[i]
            self._kind[i] = s.kind
            self._pos[i] = (s.x, s.y)
            self._sizes[i] = (s.hw * s.scale, s.hh * s.scale)
            self._rad[i] = s.rad * s.scale
            self._rot[i] = s.rot
            self._tint[i] = s.tint
            self._merge[i] = 1.0 if s.merge else 0.0
            self._rimb[i] = s.rim
        self.prog['uCount'].value = n
        self.prog['uKind'].write(self._kind)
        self.prog['uPos'].write(self._pos)
        self.prog['uSize'].write(self._sizes)
        self.prog['uRad'].write(self._rad)
        self.prog['uRot'].write(self._rot)
        self.prog['uTint'].write(self._tint)
        self.prog['uMerge'].write(self._merge)
        self.prog['uRimB'].write(self._rimb)
        for k, v in params.items():
            self._set(k, v)

    def render(self, screen_fbo, content, ui, content_params, ui_params,
               bg=None, viewport=None, clear=None):
        bg = bg or self.bg
        W, H = self.size
        vx, vy, vw, vh = viewport or (0, 0, W, H)
        vy_gl = H - vy - vh

        # --- pass 1: wallpaper + content glass -> offscreen
        self.fbo.use()
        self.fbo.clear(0.93, 0.935, 0.945)
        self.ctx.viewport = (vx, vy_gl, vw, vh)
        bg['tex'].use(0)
        self.prog['uBg'].value = 0
        self._set('uRes', (float(vw), float(vh)))
        a, b = cover_affine(bg['size'], (vw, vh))
        self._set('uUvA', a)
        self._set('uUvB', b)
        self._upload(content, content_params)
        self.vao.render()
        self.scene_tex.build_mipmaps()

        # --- pass 2: UI glass lensing the composed scene -> screen
        screen_fbo.use()
        if clear is not None:
            screen_fbo.clear(*clear)
        self.ctx.viewport = (vx, vy_gl, vw, vh)
        self.scene_tex.use(0)
        self._set('uUvA', (vw / W, vh / H))
        self._set('uUvB', (vx / W, vy_gl / H))
        self._upload(ui, ui_params)
        self.vao.render()

        # --- HUD overlay (full window)
        self.ctx.viewport = (0, 0, W, H)
        self.ctx.enable(moderngl.BLEND)
        self.ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE_MINUS_SRC_ALPHA)
        self.hud_tex.use(0)
        self.hud_prog['uTex'].value = 0
        self.hud_vao.render()
        self.ctx.disable(moderngl.BLEND)
