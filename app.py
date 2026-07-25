"""Liquid Glass Studio — SDF/metaball liquid-glass playground.

Run:            py app.py [image]
Screenshot:     py app.py --shot out.png [--size 1280x800]

Controls
  V/C/R/P/T/S/L tools: select / circle / rect / pill / triangle / switch / slider
  drag          move shape        wheel        corner roundness
  Shift+wheel   resize            Q / E        rotate
  switches      tap knob to pop it, drag across, let go
  sliders       drag the glass knob; drag the track to move the slider
  menu          click ... to open, click an item to pick a tool, Shift+drag to move
  O             open image        drag&drop    image onto window
  Ctrl+S / L    save / load scene.json         Del / trash  delete
"""

import argparse
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw

import moderngl

Image.MAX_IMAGE_PIXELS = None   # the 6K desktop pictures trip Pillow's bomb guard

from engine import (GlassRenderer, Shape, hit_topmost,
                    KIND_CIRCLE, KIND_RRECT, KIND_TRI, KIND_RING, KIND_PENT, MAXS)
from hud import HUD
import htmlify

TOOLS = ['select', 'circle', 'rect', 'pill', 'tri', 'switch', 'slider']
UTILS = ['open', 'save', 'reset', 'trash']

# Frost defaults to zero. Liquid Glass is refractive, not frosted — the heavy
# backdrop blur is the iOS 15-18 look Apple moved away from, and it is what
# makes a render read as fogged plastic instead of glass. The slider is still
# there for when you want it.
DEFAULTS = {'frost': 0.0, 'bend': 0.42, 'merge': 0.34, 'glass': 0.10}

HINTS = {
    'select': 'Select · drag move · wheel round · Shift+wheel size · Q/E rotate',
    'circle': 'Circle · click canvas to place',
    'rect':   'Rounded rect · click canvas to place',
    'pill':   'Pill · click canvas to place',
    'tri':    'Triangle · click canvas to place',
    'pent':   'Pentagon · click canvas to place',
    'switch': 'Switch · click canvas to place · tap knob, drag, let go',
    'slider': 'Slider · click canvas to place · drag knob to slide, track to move',
}
GLOBAL_HINT = ('  |  right-click HTMLify · Shift+drag menu · O image'
               ' · Ctrl+S/L scene · Del delete')

CTX_ITEMS = ['Export element as HTML', 'Export element as CSS',
             'Export whole scene as HTML']

BLUE = (0.04, 0.48, 1.00, 0.95)     # systemBlue
GREY = (0.55, 0.56, 0.60, 0.75)
GREEN = (0.20, 0.78, 0.35)          # systemGreen, switch track when on

LIGHT_ANGLE = math.radians(-125.0)  # key light above and to the left

SCENE_FILE = 'scene.json'


# ------------------------------------------------------------ background

HERE = os.path.dirname(os.path.abspath(__file__))

# macOS 27 "Golden Gate" desktop picture, light and dark. Its broad smooth
# ribbons and hairline caustics are what a lens has to chew on, so it doubles
# as the reference surface for tuning refraction.
WALLPAPER = os.path.join(HERE, 'golden-gate.png')
WALLPAPER_DARK = os.path.join(HERE, 'golden-gate-dark.png')


def default_background(dark=False):
    """Golden Gate if it is on disk, otherwise the generated studio grid."""
    path = WALLPAPER_DARK if dark else WALLPAPER
    try:
        return Image.open(path)
    except Exception:
        return make_studio_background()


def make_studio_background(w=1920, h=1200):
    """Clean studio grid: soft neutral gradient + hairline grid lines."""
    ss = 2
    W, H = w * ss, h * ss
    gx, gy = np.meshgrid(np.linspace(0, 1, W, dtype='f4'),
                         np.linspace(0, 1, H, dtype='f4'))
    base = 0.965 - 0.070 * gy - 0.038 * gx
    base += 0.045 * np.exp(-(((gx - 0.22) ** 2) + (gy - 0.16) ** 2) * 2.4)
    v = np.clip(base * 255.0, 0, 255)
    arr = np.dstack([np.clip(v - 3, 0, 255),
                     np.clip(v - 1, 0, 255),
                     np.clip(v + 3, 0, 255)]).astype('uint8')
    img = Image.fromarray(arr)

    grid = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    dr = ImageDraw.Draw(grid)
    cell = 440 * ss
    x0 = (W % cell) // 2
    y0 = (H % cell) // 2
    for x in range(x0, W + 1, cell):
        dr.line([x, 0, x, H], fill=(70, 74, 82, 68), width=ss)
    for y in range(y0, H + 1, cell):
        dr.line([0, y, W, y], fill=(70, 74, 82, 68), width=ss)
    img = Image.alpha_composite(img.convert('RGBA'), grid).convert('RGB')
    return img.resize((w, h), Image.LANCZOS)


# ------------------------------------------------------------ UI widgets

class Slider:
    """macOS-style: thin opaque track (blue fill / grey rest) on the content
    plane, big oblong glass knob on the UI plane."""

    def __init__(self, length=176, v=0.5, key=None, label='', knob=(21, 14)):
        self.length = length
        self.v = v
        self.key, self.label = key, label
        self.knob_hw, self.knob_hh = knob
        self.cx = self.cy = 0.0
        self.pop = 0.0
        self.active = False

    @property
    def x0(self):
        return self.cx - self.length / 2

    @property
    def x1(self):
        return self.cx + self.length / 2

    def place(self, cx, cy):
        self.cx, self.cy = cx, cy

    def knob_x(self):
        m = self.knob_hw + 4
        return self.x0 + m + self.v * (self.length - 2 * m)

    def hit_knob(self, mx, my):
        sc = 1.0 + 0.14 * self.pop
        return (abs(mx - self.knob_x()) <= self.knob_hw * sc + 6 and
                abs(my - self.cy) <= self.knob_hh * sc + 8)

    def hit_track(self, mx, my):
        return self.x0 - 6 <= mx <= self.x1 + 6 and abs(my - self.cy) <= 18

    def set_value(self, mx):
        m = self.knob_hw + 4
        self.v = min(max((mx - self.x0 - m) / (self.length - 2 * m), 0.0), 1.0)

    def track_shapes(self):
        th = 4.5
        kx = self.knob_x()
        out = []
        lw = max((kx + th - self.x0) / 2, th)
        out.append(Shape(KIND_RRECT, self.x0 + lw, self.cy, lw, th, rad=th,
                         tint=BLUE, merge=False, rim=0.0))
        rw = max((self.x1 - kx + th) / 2, th)
        out.append(Shape(KIND_RRECT, self.x1 - rw, self.cy, rw, th, rad=th,
                         tint=GREY, merge=False, rim=0.0))
        return out

    def knob_shape(self):
        sc = 1.0 + 0.14 * self.pop
        hw, hh = self.knob_hw * sc, self.knob_hh * sc
        return Shape(KIND_RRECT, self.knob_x(), self.cy, hw, hh, rad=hh,
                     tint=(1, 1, 1, 0.22 - 0.08 * self.pop),
                     merge=False, rim=-0.40)

    def step(self, dt):
        ease = 1.0 - math.exp(-dt * 16.0)
        self.pop += ((1.0 if self.active else 0.0) - self.pop) * ease


class Switch:
    """iOS toggle at the system 51x31 ratio, scaled up 1.22x for the studio.
    Idle the knob is a near-opaque disc; pressed it grows into an oblong glass
    lens that pops past the track and refracts it."""
    W, H = 62, 38

    def __init__(self, x, y, on=False):
        self.x, self.y = x, y
        self.on = on
        self.t = 1.0 if on else 0.0
        self.pop = 0.0
        self.active = False
        self.moved = False
        self.drag_t = self.t

    @property
    def r(self):
        return self.H / 2 - 2.4

    def knob_xy(self):
        m = self.H / 2
        return self.x - self.W / 2 + m + self.t * (self.W - 2 * m), self.y

    def hit_knob(self, mx, my):
        kx, ky = self.knob_xy()
        sc = 1.0 + 0.45 * self.pop
        return abs(mx - kx) <= self.r * sc + 6 and abs(my - ky) <= self.r * sc + 6

    def hit_body(self, mx, my):
        return (abs(mx - self.x) <= self.W / 2 + 4 and
                abs(my - self.y) <= self.H / 2 + 4)

    def track_shape(self):
        g = self.t
        off = (0.78, 0.78, 0.80)
        col = tuple(off[i] + (GREEN[i] - off[i]) * g for i in range(3))
        return Shape(KIND_RRECT, self.x, self.y, self.W / 2, self.H / 2,
                     rad=self.H / 2, tint=(*col, 0.88 - 0.50 * self.pop),
                     merge=False, rim=-0.15)

    def knob_shape(self):
        kx, ky = self.knob_xy()
        hw = self.r * (1.0 + 0.45 * self.pop)
        hh = self.r * (1.0 + 0.15 * self.pop)
        return Shape(KIND_RRECT, kx, ky, hw, hh, rad=hh,
                     tint=(1, 1, 1, 0.90 - 0.68 * self.pop),
                     merge=False, rim=-0.25)

    def step(self, dt):
        ease = 1.0 - math.exp(-dt * 16.0)
        self.pop += ((1.0 if self.active else 0.0) - self.pop) * ease
        target = self.drag_t if self.active else (1.0 if self.on else 0.0)
        self.t += (target - self.t) * ease


class Menu:
    """Toolbar menu (SwiftUI Menu reference): a glass ellipsis button that
    scales open a glass panel of items from its bottom-right corner."""
    R = 22
    W = 204
    ITEM_H = 46
    PAD = 6
    ITEMS = [('Circle', 'circle', 'circle'),
             ('Triangle', 'tri', 'tri'),
             ('Square', 'rect', 'rect'),
             ('Pentagon', 'pent', 'pent')]

    def __init__(self, x, y):
        self.x, self.y = x, y
        self.open = False
        self.pop = 0.0
        self.hover = None

    def panel_rect(self):
        h = self.ITEM_H * len(self.ITEMS) + self.PAD * 2
        return (self.x + self.R - self.W, self.y + self.R + 14, self.W, h)

    def hit_button(self, mx, my):
        return math.hypot(mx - self.x, my - self.y) <= self.R + 4

    def item_at(self, mx, my):
        if self.pop < 0.55:
            return None
        px, py, pw, ph = self.panel_rect()
        iy0 = py + self.PAD
        iyN = iy0 + self.ITEM_H * len(self.ITEMS)
        if px <= mx <= px + pw and iy0 <= my <= iyN:
            return int((my - iy0) / self.ITEM_H)
        return None

    def shapes(self):
        out = [Shape(KIND_CIRCLE, self.x, self.y, self.R, self.R,
                     tint=(1, 1, 1, 0.18), merge=False, rim=-0.22)]
        pop = self.pop
        if pop > 0.005:
            px, py, pw, ph = self.panel_rect()
            e = 1.0 - (1.0 - pop) ** 3
            ax, ay = self.x + self.R, self.y + self.R + 4
            pcx, pcy = px + pw / 2, py + ph / 2
            cx = ax + (pcx - ax) * e
            cy = ay + (pcy - ay) * e
            hw = max(pw / 2 * e, 4.0)
            hh = max(ph / 2 * e, 4.0)
            out.append(Shape(KIND_RRECT, cx, cy, hw, hh,
                             rad=min(20.0, hh),
                             tint=(1, 1, 1, min(0.30 * pop * 1.4, 0.30)),
                             merge=False, rim=-0.16))
        return out

    def step(self, dt):
        ease = 1.0 - math.exp(-dt * 17.0)
        self.pop += ((1.0 if self.open else 0.0) - self.pop) * ease


class ContextMenu:
    """Right-click menu over a single element: the HTMLify export targets.
    Scales open from the click point like the toolbar Menu does."""
    W = 208
    ITEM_H = 38
    PAD = 22

    def __init__(self, x, y, target, title, items):
        self.x, self.y = x, y
        self.target = target
        self.title = title
        self.items = items
        self.open = True
        self.pop = 0.0
        self.hover = None

    def panel_rect(self):
        h = self.ITEM_H * len(self.items) + self.PAD + 8
        return (self.x, self.y, self.W, h)

    def item_at(self, mx, my):
        if self.pop < 0.55:
            return None
        px, py, pw, ph = self.panel_rect()
        iy0 = py + self.PAD
        iyN = iy0 + self.ITEM_H * len(self.items)
        if px <= mx <= px + pw and iy0 <= my <= iyN:
            return int((my - iy0) / self.ITEM_H)
        return None

    def shapes(self):
        if self.pop <= 0.005:
            return []
        px, py, pw, ph = self.panel_rect()
        e = 1.0 - (1.0 - self.pop) ** 3
        cx = self.x + (px + pw / 2 - self.x) * e
        cy = self.y + (py + ph / 2 - self.y) * e
        return [Shape(KIND_RRECT, cx, cy, max(pw / 2 * e, 4.0),
                      max(ph / 2 * e, 4.0), rad=min(18.0, ph / 2 * e),
                      tint=(1, 1, 1, min(0.30 * self.pop * 1.4, 0.30)),
                      merge=False, rim=-0.16)]

    def step(self, dt):
        ease = 1.0 - math.exp(-dt * 17.0)
        self.pop += ((1.0 if self.open else 0.0) - self.pop) * ease


# ------------------------------------------------------------ state

class State:
    def __init__(self, size):
        self.size = size
        self.content = []
        self.switches = []
        self.play_sliders = []
        self.menus = []
        self.selected = None
        self.tool = 'select'
        self.sliders = [Slider(key='frost', label='Frost', v=DEFAULTS['frost']),
                        Slider(key='bend', label='Bend', v=DEFAULTS['bend']),
                        Slider(key='merge', label='Merge', v=DEFAULTS['merge']),
                        Slider(key='glass', label='Glass', v=DEFAULTS['glass'])]
        self.hover_icon = None
        self.toast = None
        self.toast_until = 0.0
        self.time = 0.0
        self.touch = (-1e4, -1e4)   # interactive illumination centre
        self.touch_a = 0.0
        self.pressing = False
        self.ctx_menu = None        # right-click HTMLify menu
        self.cursor = None          # (x, y, down) drawn by the recorder only
        self.layout(size)

    def layout(self, size):
        self.size = size
        W, H = size
        slot, pad, h = 46, 13, 56
        y = H - 24 - h / 2
        x0 = 24.0
        tw = pad * 2 + slot * len(TOOLS)
        self.tool_pill = (x0, y, tw, h)
        self.tool_slots = [(TOOLS[i], x0 + pad + slot * i + slot / 2, y)
                           for i in range(len(TOOLS))]
        ux = x0 + tw + 16
        uw = pad * 2 + slot * len(UTILS)
        self.util_pill = (ux, y, uw, h)
        self.util_slots = [(UTILS[i], ux + pad + slot * i + slot / 2, y)
                           for i in range(len(UTILS))]
        right = W - 26.0
        for i, sl in enumerate(self.sliders):
            sl.place(right - sl.length / 2,
                     H - 26 - 21 - (len(self.sliders) - 1 - i) * 56)

    def val(self, key):
        return next(s.v for s in self.sliders if s.key == key)

    def all_sliders(self):
        return self.sliders + self.play_sliders

    def content_shapes(self):
        out = list(self.content)
        out.extend(sw.track_shape() for sw in self.switches)
        for sl in self.all_sliders():
            out.extend(sl.track_shapes())
        s = self.selected
        if isinstance(s, Shape) and s in self.content:
            r = s.bound_radius() + 11
            out.append(Shape(KIND_RING, s.x, s.y, r, r, rad=2.2,
                             tint=(1, 1, 1, 0.55), merge=False, rim=0.8))
        return out[:MAXS]

    def ui_shapes(self):
        out = []
        for (x, y, w, h) in (self.tool_pill, self.util_pill):
            out.append(Shape(KIND_RRECT, x + w / 2, y, w / 2, h / 2, rad=h / 2,
                             tint=(1, 1, 1, 0.16), merge=False, rim=-0.20))
        out.extend(sl.knob_shape() for sl in self.all_sliders())
        out.extend(sw.knob_shape() for sw in self.switches)
        for m in self.menus:
            out.extend(m.shapes())
        if self.ctx_menu is not None:
            out.extend(self.ctx_menu.shapes())
        return out[:MAXS]

    def light_dirs(self):
        """Key above-left, fill below-right. The pair is what gives Liquid
        Glass its double rim arc; drifting the angle makes the specular travel
        the way it does when a phone is tilted."""
        a = LIGHT_ANGLE + 0.22 * math.sin(self.time * 0.45)
        kx, ky = math.cos(a), math.sin(a)
        return (kx * 0.62, ky * 0.62, 0.78), (-kx * 0.55, -ky * 0.55, 0.62)

    def _light_params(self):
        key, fill = self.light_dirs()
        return dict(uKey=key, uFill=fill,
                    uTouch=self.touch, uTouchA=self.touch_a * 0.16)

    def content_params(self):
        p = dict(uOpacity=self.val('glass'),
                 uFrost=self.val('frost') * 22.0,
                 uBend=self.val('bend') * 88.0,
                 uMergeK=4.0 + self.val('merge') * 86.0,
                 uEdge=12.0, uAniso=1.0, uDisp=0.70,
                 # no drop shadow: the rim hairline carries the separation
                 uShadow=0.0, uShadowR=14.0,
                 uSpec=0.30, uShine=22.0,
                 uAdapt=0.50, uSat=0.07, uRimLit=0.30)
        p.update(self._light_params())
        return p

    def ui_params(self):
        p = dict(uOpacity=0.10 + 0.18 * self.val('glass'),
                 uFrost=0.0, uBend=21.0, uMergeK=26.0,
                 uEdge=7.0, uAniso=0.80, uDisp=0.85,
                 uShadow=0.0, uShadowR=9.0,
                 uSpec=0.34, uShine=26.0,
                 uAdapt=0.48, uSat=0.06, uRimLit=0.34)
        p.update(self._light_params())
        return p

    def hud_state(self):
        return dict(
            mode='studio',
            tabs=[],
            hint=HINTS[self.tool] + GLOBAL_HINT,
            hint_pos=(24, 20),
            toast=self.toast,
            tool=self.tool,
            hover=self.hover_icon,
            cursor=self.cursor,
            tool_slots=list(self.tool_slots),
            util_slots=list(self.util_slots),
            sliders=[(s.label, s.x0, s.x1, s.cy, s.v) for s in self.sliders],
            menus=[dict(x=m.x, y=m.y, pop=m.pop, panel=m.panel_rect(),
                        pad=m.PAD, ih=m.ITEM_H,
                        items=[(lbl, ic) for lbl, ic, _ in m.ITEMS],
                        hover=m.hover)
                   for m in self.menus],
            ctx=None if self.ctx_menu is None else
                dict(pop=self.ctx_menu.pop, panel=self.ctx_menu.panel_rect(),
                     pad=self.ctx_menu.PAD, ih=self.ctx_menu.ITEM_H,
                     title=self.ctx_menu.title, items=self.ctx_menu.items,
                     hover=self.ctx_menu.hover))

    def say(self, msg, secs=2.5):
        self.toast = msg
        self.toast_until = self.time + secs

    def step(self, dt):
        self.time += dt
        ease = 1.0 - math.exp(-dt * 14.0)
        self.touch_a += ((1.0 if self.pressing else 0.0) - self.touch_a) * ease
        for sl in self.all_sliders():
            sl.step(dt)
        for sw in self.switches:
            sw.step(dt)
        for m in self.menus:
            m.step(dt)
        if self.ctx_menu is not None:
            self.ctx_menu.step(dt)
            if not self.ctx_menu.open and self.ctx_menu.pop < 0.01:
                self.ctx_menu = None


def demo_scene(state):
    """Laid out as fractions of the window so the scene stays composed at any
    size, and clear of the bottom-left toolbars and the right-hand sliders."""
    W, H = state.size
    r = min(W / 1280.0, H / 800.0)

    def P(fx, fy):
        return fx * W, fy * H

    # Control-scale and discrete, the way Apple's glass actually appears: a
    # button, a capsule, a card, a mark. Oversized blobs at a low merge
    # threshold gel into one amoeba and stop reading as an interface.
    state.content = [
        Shape(KIND_CIRCLE, *P(0.24, 0.31), 46 * r, 46 * r),
        Shape(KIND_RRECT, *P(0.42, 0.31), 74 * r, 34 * r, rad=34 * r),
        Shape(KIND_RRECT, *P(0.60, 0.31), 58 * r, 58 * r, rad=20 * r),
        Shape(KIND_TRI, *P(0.30, 0.56), 46 * r, 46 * r, rad=10 * r),
    ]
    sx = 0.72 * W
    state.switches = [Switch(sx, H * 0.27, on=True),
                      Switch(sx, H * 0.37, on=False)]
    demo = Slider(length=0.24 * W, v=0.55, knob=(24 * r, 15 * r))
    demo.place(*P(0.34, 0.75))
    state.play_sliders = [demo]
    state.menus = [Menu(*P(0.89, 0.13))]
    state.selected = None


# ------------------------------------------------------------ scene io

def save_scene(state, path=SCENE_FILE):
    data = dict(
        sliders={s.key: s.v for s in state.sliders},
        shapes=[dict(kind=s.kind, x=s.x, y=s.y, hw=s.hw, hh=s.hh, rad=s.rad,
                     rot=s.rot, tint=list(s.tint), merge=s.merge)
                for s in state.content],
        switches=[dict(x=sw.x, y=sw.y, on=sw.on) for sw in state.switches],
        play_sliders=[dict(x=sl.cx, y=sl.cy, v=sl.v, length=sl.length)
                      for sl in state.play_sliders],
        menus=[dict(x=m.x, y=m.y) for m in state.menus])
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=1)


def load_scene(state, path=SCENE_FILE):
    with open(path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    state.content = [Shape(kind=d['kind'], x=d['x'], y=d['y'], hw=d['hw'],
                           hh=d['hh'], rad=d['rad'], rot=d['rot'],
                           tint=tuple(d['tint']), merge=d['merge'])
                     for d in data['shapes']]
    state.switches = [Switch(d['x'], d['y'], d['on'])
                      for d in data.get('switches', [])]
    state.play_sliders = []
    for d in data.get('play_sliders', []):
        sl = Slider(length=d['length'], v=d['v'], knob=(26, 17))
        sl.place(d['x'], d['y'])
        state.play_sliders.append(sl)
    state.menus = [Menu(d['x'], d['y']) for d in data.get('menus', [])]
    for s in state.sliders:
        s.v = data['sliders'].get(s.key, s.v)
    state.selected = None


def ask_open_image():
    try:
        import tkinter as tk
        from tkinter import filedialog
        root = tk.Tk()
        root.withdraw()
        root.attributes('-topmost', True)
        p = filedialog.askopenfilename(title='Choose background image', filetypes=[
            ('Images', '*.png;*.jpg;*.jpeg;*.bmp;*.webp'), ('All files', '*.*')])
        root.destroy()
        return p or None
    except Exception:
        return None


# ------------------------------------------------------------ app

class App:
    def __init__(self, image_path=None):
        import glfw
        self.glfw = glfw
        if not glfw.init():
            raise RuntimeError('glfw init failed')
        glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
        glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
        glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
        self.win = glfw.create_window(1280, 800, 'Liquid Glass Studio', None, None)
        if not self.win:
            glfw.terminate()
            raise RuntimeError('window creation failed')
        glfw.make_context_current(self.win)
        glfw.swap_interval(1)

        self.ctx = moderngl.create_context()
        self.screen = self.ctx.detect_framebuffer()
        size = glfw.get_framebuffer_size(self.win)
        self.renderer = GlassRenderer(self.ctx, size)
        bg = None
        if image_path:
            try:
                bg = Image.open(image_path)
            except Exception as e:
                print('could not open image:', e)
        self.renderer.set_background(bg or default_background())

        self.state = State(size)
        demo_scene(self.state)
        self.hud = HUD(size)
        self.hud_dirty = True

        self.drag_shape = None
        self.drag_switch = None
        self.drag_slider = None
        self.drag_menu = None
        self.knob_switch = None
        self.active_slider = None
        self.mouse = (0, 0)

        glfw.set_mouse_button_callback(self.win, self.on_mouse_button)
        glfw.set_cursor_pos_callback(self.win, self.on_cursor)
        glfw.set_scroll_callback(self.win, self.on_scroll)
        glfw.set_key_callback(self.win, self.on_key)
        glfw.set_drop_callback(self.win, self.on_drop)
        glfw.set_framebuffer_size_callback(self.win, self.on_resize)

    # ---------------- helpers

    def _mouse_pos(self):
        wx, wy = self.glfw.get_cursor_pos(self.win)
        ww, wh = self.glfw.get_window_size(self.win)
        fw, fh = self.state.size
        if ww and wh:
            return wx * fw / ww, wy * fh / wh
        return wx, wy

    def _icon_hit(self, mx, my):
        for sid, cx, cy in self.state.tool_slots:
            if math.hypot(mx - cx, my - cy) <= 21:
                return ('tool', sid)
        for sid, cx, cy in self.state.util_slots:
            if math.hypot(mx - cx, my - cy) <= 21:
                return ('util', sid)
        return None

    def _load_bg(self, path):
        try:
            self.renderer.set_background(Image.open(path))
            self.state.say(f'Background: {os.path.basename(path)}')
        except Exception as e:
            self.state.say(f'Could not load image: {e}')
        self.hud_dirty = True

    # ---------------- actions

    def _reset_all(self):
        st = self.state
        for sl in st.sliders:
            sl.v = DEFAULTS[sl.key]
        demo_scene(st)
        st.say('Everything reset to defaults')

    def _delete_selected(self):
        st = self.state
        s = st.selected
        if isinstance(s, Switch) and s in st.switches:
            st.switches.remove(s)
        elif isinstance(s, Slider) and s in st.play_sliders:
            st.play_sliders.remove(s)
        elif isinstance(s, Menu) and s in st.menus:
            st.menus.remove(s)
        elif s in st.content:
            st.content.remove(s)
        st.selected = None

    def _pick(self, mx, my):
        """Topmost interactive object under the cursor, controls before art."""
        st = self.state
        for m in reversed(st.menus):
            if m.hit_button(mx, my):
                return m
        for sl in reversed(st.play_sliders):
            if sl.hit_knob(mx, my) or sl.hit_track(mx, my):
                return sl
        for sw in reversed(st.switches):
            if sw.hit_knob(mx, my) or sw.hit_body(mx, my):
                return sw
        return hit_topmost(st.content, mx, my)

    def _open_ctx(self, mx, my):
        st = self.state
        target = self._pick(mx, my)
        if target is None:
            st.ctx_menu = None
            st.say('Right-click an element to HTMLify it')
            return
        st.selected = target
        _, title = htmlify.describe(target)
        x = min(mx, st.size[0] - ContextMenu.W - 12)
        h = ContextMenu.ITEM_H * len(CTX_ITEMS) + ContextMenu.PAD + 8
        y = min(my, st.size[1] - h - 12)
        st.ctx_menu = ContextMenu(x, y, target, 'HTMLify · ' + title,
                                  list(CTX_ITEMS))
        self.hud_dirty = True

    def _run_ctx(self, idx):
        st = self.state
        cm = st.ctx_menu
        if cm is None:
            return
        try:
            if idx == 2:
                p = htmlify.write_scene(st, base=HERE,
                                        bg=os.path.basename(WALLPAPER))
            else:
                p = htmlify.write_element(cm.target, base=HERE, as_css=idx == 1)
            st.say('Wrote %s' % os.path.relpath(p, HERE))
        except Exception as e:
            st.say('Export failed: %s' % e)
        cm.open = False
        st.ctx_menu = cm if cm.pop > 0.01 else None
        self.hud_dirty = True

    def _add(self, mx, my):
        st = self.state
        kind = st.tool
        if kind == 'circle':
            s = Shape(KIND_CIRCLE, mx, my, 52, 52)
        elif kind == 'rect':
            s = Shape(KIND_RRECT, mx, my, 88, 62, rad=26)
        elif kind == 'pill':
            s = Shape(KIND_RRECT, mx, my, 95, 30, rad=30)
        elif kind == 'tri':
            s = Shape(KIND_TRI, mx, my, 58, 58, rad=12)
        elif kind == 'pent':
            s = Shape(KIND_PENT, mx, my, 58, 58, rad=10)
        elif kind == 'switch':
            st.switches.append(Switch(mx, my))
            st.selected = st.switches[-1]
            return
        elif kind == 'slider':
            sl = Slider(length=300, v=0.5, knob=(26, 17))
            sl.place(mx, my)
            st.play_sliders.append(sl)
            st.selected = sl
            return
        else:
            return
        if len(st.content) >= MAXS - len(st.switches) - 12:
            st.say('Shape limit reached')
            return
        st.content.append(s)
        st.selected = s

    # ---------------- events

    def on_resize(self, win, w, h):
        if w < 8 or h < 8:
            return
        self.renderer.resize((w, h))
        self.state.layout((w, h))
        self.hud.resize((w, h))
        self.hud_dirty = True

    def on_drop(self, win, paths):
        if paths:
            self._load_bg(paths[0])

    def on_cursor(self, win, x, y):
        mx, my = self._mouse_pos()
        pmx, pmy = self.mouse
        self.mouse = (mx, my)
        st = self.state

        hov = self._icon_hit(mx, my)
        if hov != st.hover_icon:
            st.hover_icon = hov
            self.hud_dirty = True

        for m in st.menus:
            h = m.item_at(mx, my)
            if h != m.hover:
                m.hover = h
                self.hud_dirty = True

        if st.pressing:
            st.touch = (mx, my)
        cm = st.ctx_menu
        if cm is not None:
            h = cm.item_at(mx, my)
            if h != cm.hover:
                cm.hover = h
                self.hud_dirty = True

        if self.active_slider is not None:
            self.active_slider.set_value(mx)
            return
        sw = self.knob_switch
        if sw is not None:
            span = sw.W - sw.H
            sw.drag_t = min(max(sw.drag_t + (mx - pmx) / span, 0.0), 1.0)
            sw.moved = sw.moved or abs(sw.drag_t - (1.0 if sw.on else 0.0)) > 0.15
            return
        if self.drag_menu is not None:
            self.drag_menu.x += mx - pmx
            self.drag_menu.y += my - pmy
            return
        if self.drag_switch is not None:
            self.drag_switch.x += mx - pmx
            self.drag_switch.y += my - pmy
            return
        if self.drag_slider is not None:
            self.drag_slider.cx += mx - pmx
            self.drag_slider.cy += my - pmy
            return
        if self.drag_shape is not None:
            self.drag_shape.x += mx - pmx
            self.drag_shape.y += my - pmy

    def on_mouse_button(self, win, button, action, mods):
        import glfw
        mx, my = self._mouse_pos()
        st = self.state

        if action == glfw.RELEASE:
            st.pressing = False
            if self.active_slider is not None:
                self.active_slider.active = False
                self.active_slider = None
            sw = self.knob_switch
            if sw is not None:
                sw.on = sw.drag_t > 0.5 if sw.moved else not sw.on
                sw.active = False
                self.knob_switch = None
            self.drag_shape = None
            self.drag_switch = None
            self.drag_slider = None
            self.drag_menu = None
            return

        if button == glfw.MOUSE_BUTTON_RIGHT and action == glfw.PRESS:
            self._open_ctx(mx, my)
            return

        if button != glfw.MOUSE_BUTTON_LEFT or action != glfw.PRESS:
            return

        st.pressing = True
        st.touch = (mx, my)

        # an open HTMLify menu eats the next click
        if st.ctx_menu is not None and st.ctx_menu.open:
            idx = st.ctx_menu.item_at(mx, my)
            if idx is not None:
                self._run_ctx(idx)
            else:
                st.ctx_menu.open = False
                self.hud_dirty = True
            return

        shift = mods & glfw.MOD_SHIFT

        # menus first
        for m in reversed(st.menus):
            if m.hit_button(mx, my):
                if shift:
                    self.drag_menu = m
                    st.selected = m
                else:
                    was = m.open
                    for o in st.menus:
                        o.open = False
                    m.open = not was
                self.hud_dirty = True
                return
        for m in reversed(st.menus):
            if m.open:
                idx = m.item_at(mx, my)
                if idx is not None:
                    label, _, tool = m.ITEMS[idx]
                    st.tool = tool
                    st.say(f'{label} tool')
                    m.open = False
                    m.hover = None
                    self.hud_dirty = True
                    return
                m.open = False
                self.hud_dirty = True
                return

        # control sliders
        for sl in st.sliders:
            if sl.hit_knob(mx, my) or sl.hit_track(mx, my):
                sl.active = True
                sl.set_value(mx)
                self.active_slider = sl
                return

        # toolbar
        hit = self._icon_hit(mx, my)
        if hit:
            group, sid = hit
            if group == 'tool':
                st.tool = sid
            elif sid == 'open':
                p = ask_open_image()
                if p:
                    self._load_bg(p)
            elif sid == 'save':
                save_scene(st)
                st.say(f'Saved {SCENE_FILE}')
            elif sid == 'reset':
                self._reset_all()
            elif sid == 'trash':
                self._delete_selected()
            self.hud_dirty = True
            return

        # playground sliders
        for sl in reversed(st.play_sliders):
            if sl.hit_knob(mx, my):
                sl.active = True
                self.active_slider = sl
                st.selected = sl
                return
        for sl in reversed(st.play_sliders):
            if sl.hit_track(mx, my):
                st.selected = sl
                self.drag_slider = sl
                return

        # switches
        for sw in reversed(st.switches):
            if sw.hit_knob(mx, my):
                sw.active = True
                sw.moved = False
                sw.drag_t = sw.t
                self.knob_switch = sw
                st.selected = sw
                return
        for sw in reversed(st.switches):
            if sw.hit_body(mx, my):
                st.selected = sw
                self.drag_switch = sw
                return

        # canvas
        if st.tool == 'select':
            s = hit_topmost(st.content, mx, my)
            if s is not None:
                st.selected = s
                self.drag_shape = s
            else:
                st.selected = None
        else:
            self._add(mx, my)

    def on_scroll(self, win, xoff, yoff):
        import glfw
        s = self.state.selected
        if not isinstance(s, Shape):
            return
        shift = (glfw.get_key(self.win, glfw.KEY_LEFT_SHIFT) == glfw.PRESS or
                 glfw.get_key(self.win, glfw.KEY_RIGHT_SHIFT) == glfw.PRESS)
        if shift:
            f = 1.0 + 0.07 * yoff
            s.hw = min(max(s.hw * f, 8), 600)
            s.hh = min(max(s.hh * f, 8), 600)
        else:
            s.rad = min(max(s.rad + 3.0 * yoff, 0.0), min(s.hw, s.hh))

    def on_key(self, win, key, scancode, action, mods):
        import glfw
        if action not in (glfw.PRESS, glfw.REPEAT):
            return
        st = self.state
        ctrl = mods & glfw.MOD_CONTROL
        tool_keys = {glfw.KEY_V: 'select', glfw.KEY_C: 'circle', glfw.KEY_R: 'rect',
                     glfw.KEY_P: 'pill', glfw.KEY_T: 'tri', glfw.KEY_S: 'switch',
                     glfw.KEY_L: 'slider'}
        if ctrl and key == glfw.KEY_S:
            save_scene(st)
            st.say(f'Saved {SCENE_FILE}')
        elif ctrl and key == glfw.KEY_L:
            if os.path.exists(SCENE_FILE):
                load_scene(st)
                st.say(f'Loaded {SCENE_FILE}')
            else:
                st.say('No scene.json yet')
        elif key in tool_keys and not ctrl:
            st.tool = tool_keys[key]
        elif key == glfw.KEY_O and not ctrl:
            p = ask_open_image()
            if p:
                self._load_bg(p)
        elif key in (glfw.KEY_DELETE, glfw.KEY_BACKSPACE):
            self._delete_selected()
        elif key == glfw.KEY_ESCAPE:
            st.selected = None
            if st.ctx_menu is not None:
                st.ctx_menu.open = False
        elif key == glfw.KEY_Q and isinstance(st.selected, Shape):
            st.selected.rot -= 0.06
        elif key == glfw.KEY_E and isinstance(st.selected, Shape):
            st.selected.rot += 0.06
        else:
            return
        self.hud_dirty = True

    # ---------------- frame

    def run(self, max_seconds=None):
        import glfw
        st = self.state
        last = glfw.get_time()
        start = last
        while not glfw.window_should_close(self.win):
            now = glfw.get_time()
            dt = min(now - last, 0.05)
            last = now
            if max_seconds is not None and now - start > max_seconds:
                break

            if st.toast and st.time > st.toast_until:
                st.toast = None
                self.hud_dirty = True
            st.step(dt)

            popping = [m.pop for m in st.menus]
            if st.ctx_menu is not None:
                popping.append(st.ctx_menu.pop)
            if any(0.001 < p < 0.999 for p in popping):
                self.hud_dirty = True
            if self.hud_dirty:
                self.renderer.update_hud(self.hud.render(st.hud_state()))
                self.hud_dirty = False

            self.renderer.render(self.screen, st.content_shapes(),
                                 st.ui_shapes(), st.content_params(),
                                 st.ui_params())
            glfw.swap_buffers(self.win)
            glfw.poll_events()
        glfw.terminate()


# ------------------------------------------------------------ headless shot

def shot(out_path, size=(1280, 800), image_path=None):
    ctx = moderngl.create_context(standalone=True)
    tex = ctx.texture(size, 3)
    fbo = ctx.framebuffer(color_attachments=[tex])
    renderer = GlassRenderer(ctx, size)
    renderer.set_background(Image.open(image_path) if image_path
                            else default_background())
    st = State(size)
    demo_scene(st)
    sw = st.switches[1]
    sw.active = True
    sw.pop = 1.0
    sw.t = 0.5
    st.menus[0].open = True
    st.menus[0].pop = 1.0
    st.step(0.016)
    hud = HUD(size)
    renderer.update_hud(hud.render(st.hud_state()))
    renderer.render(fbo, st.content_shapes(), st.ui_shapes(),
                    st.content_params(), st.ui_params())
    img = Image.frombytes('RGB', size, fbo.read(components=3)).transpose(1)
    img.save(out_path)
    print('wrote', out_path)


def main():
    ap = argparse.ArgumentParser(description='Liquid Glass Studio')
    ap.add_argument('image', nargs='?', help='background image')
    ap.add_argument('--shot', metavar='OUT.png', help='render one frame headless and exit')
    ap.add_argument('--size', default='1280x800')
    ap.add_argument('--selftest', action='store_true', help='open window ~2s then exit')
    args = ap.parse_args()
    size = tuple(int(v) for v in args.size.lower().split('x'))

    if args.shot:
        shot(args.shot, size=size, image_path=args.image)
        return
    app = App(image_path=args.image)
    app.run(max_seconds=2.0 if args.selftest else None)


if __name__ == '__main__':
    main()
