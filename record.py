"""Headless demo recorder — drives the studio through a scripted session and
encodes it straight to MP4.

    py record.py out.mp4 [--size 1440x900] [--fps 30] [--dark]

Nothing here fakes the picture: it moves the same State the interactive app
moves, renders through the same two-pass GlassRenderer, and pipes raw frames
to ffmpeg. So the video is the renderer's actual output, cursor included.
"""

import argparse
import math
import os
import subprocess
import sys

import moderngl
from PIL import Image

import htmlify
from app import (CTX_ITEMS, ContextMenu, HERE, State, default_background,
                 demo_scene)
from engine import GlassRenderer
from hud import HUD

Image.MAX_IMAGE_PIXELS = None


# ------------------------------------------------------------ easing

def ease(t):
    """Smooth in/out, the shape SwiftUI springs settle into."""
    t = min(max(t, 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def seg(now, t0, t1):
    """Normalised, eased progress through [t0, t1]; None outside it."""
    if now < t0 or now > t1:
        return None
    return ease((now - t0) / max(t1 - t0, 1e-6))


def lerp(a, b, u):
    return a + (b - a) * u


# ------------------------------------------------------------ timeline

class Demo:
    """One scripted pass: shapes dragged, switches toggled, sliders moved,
    menus opened. Times are seconds."""

    DURATION = 14.6

    def __init__(self, state):
        self.st = state
        s = state
        self.circle = s.content[0]
        self.pill = s.content[1]
        self.tri = s.content[3]
        self.sw_a, self.sw_b = s.switches[0], s.switches[1]
        self.play = s.play_sliders[0]
        self.menu = s.menus[0]
        # Glass and Bend, not Frost: pushing the frost slider fogs the whole
        # scene, which is the look this material is meant to be the opposite of
        self.glass = next(x for x in s.sliders if x.key == 'glass')
        self.bend = next(x for x in s.sliders if x.key == 'bend')
        self.W, self.H = state.size
        self.home = {
            'circle': (self.circle.x, self.circle.y),
            'tri': (self.tri.x, self.tri.y),
        }
        self.cursor = (self.circle.x, self.circle.y)
        self.down = False
        self.ctx_shown = False
        self.exported = False

    # -- helpers ---------------------------------------------------------

    def _grab(self, obj, x, y, down=True):
        obj.x, obj.y = x, y
        self.cursor = (x, y)
        self.down = down

    def _press_switch(self, sw, u, target):
        """Tap the knob, let it swell into glass, drag across, release."""
        sw.active = u < 0.86
        sw.drag_t = min(max(u * 1.45 - 0.12, 0.0), 1.0) if target else \
            min(max(1.0 - (u * 1.45 - 0.12), 0.0), 1.0)
        if u >= 0.86:
            sw.on = target
        kx, ky = sw.knob_xy()
        self.cursor = (kx, ky)
        self.down = u < 0.86

    # -- the script ------------------------------------------------------

    def update(self, t):
        st = self.st
        st.cursor = None
        self.down = False

        W, H = self.W, self.H

        # 1. walk the circle into the pill so the metaball field gels, then
        #    pull it back out to clear ground — parked over the small capsule
        #    it would read as one permanently conjoined blob
        x0, y0 = self.home['circle']
        u = seg(t, 0.3, 2.0)
        if u is not None:
            self._grab(self.circle, lerp(x0, x0 + 0.13 * W, u),
                       lerp(y0, y0 + 0.04 * H, u))
        u = seg(t, 2.0, 3.2)
        if u is not None:
            self._grab(self.circle, lerp(x0 + 0.13 * W, x0 - 0.015 * W, u),
                       lerp(y0 + 0.04 * H, y0 + 0.11 * H, u))

        # 2. carry the triangle across a caustic so the lensing is obvious
        u = seg(t, 3.3, 4.9)
        if u is not None:
            tx, ty = self.home['tri']
            self._grab(self.tri, lerp(tx, tx + 0.17 * W, u),
                       lerp(ty, ty - 0.13 * H,
                            math.sin(u * math.pi) * 0.5 + u * 0.5))

        # 3. toggle both switches
        u = seg(t, 5.0, 5.9)
        if u is not None:
            self._press_switch(self.sw_a, u, False)
        u = seg(t, 6.0, 6.9)
        if u is not None:
            self._press_switch(self.sw_b, u, True)

        # 4. drive the big canvas slider
        u = seg(t, 7.0, 9.0)
        if u is not None:
            sw = math.sin(u * math.pi * 1.5)
            self.play.active = True
            self.play.v = min(max(0.55 + sw * 0.42, 0.02), 0.98)
            self.cursor = (self.play.knob_x(), self.play.cy)
            self.down = True
        elif t > 9.0:
            self.play.active = False

        # 5. push the material sliders — the whole scene answers
        u = seg(t, 9.1, 10.2)
        if u is not None:
            self.glass.active = True
            self.glass.v = lerp(0.10, 0.34, u)
            self.cursor = (self.glass.knob_x(), self.glass.cy)
            self.down = True
        u = seg(t, 10.2, 11.0)
        if u is not None:
            self.glass.active = True
            self.glass.v = lerp(0.34, 0.10, u)
            self.cursor = (self.glass.knob_x(), self.glass.cy)
            self.down = True
        elif t > 11.0:
            self.glass.active = False
        u = seg(t, 11.0, 11.9)
        if u is not None:
            self.bend.active = True
            self.bend.v = lerp(0.42, 0.82, u)
            self.cursor = (self.bend.knob_x(), self.bend.cy)
            self.down = True
        elif t > 11.9:
            self.bend.active = False

        # 6. morph the toolbar menu open, then the HTMLify right-click menu
        self.menu.open = 12.0 < t < 12.8
        if 12.0 < t < 12.8:
            self.cursor = (self.menu.x, self.menu.y)
        cx = min(self.pill.x + 0.03 * W, W - ContextMenu.W - 16)
        cy = min(self.pill.y + 0.06 * H, H - 220)
        if t >= 12.9 and not self.ctx_shown:
            self.ctx_shown = True
            st.ctx_menu = ContextMenu(cx, cy, self.pill,
                                      'HTMLify · Rounded rect', list(CTX_ITEMS))
        if st.ctx_menu is not None and t >= 12.9:
            self.cursor = (cx, cy)
            if t > 13.5:
                st.ctx_menu.hover = 0
                self.cursor = (cx + 34, cy + ContextMenu.PAD
                               + ContextMenu.ITEM_H * 0.5)
            if t > 13.9 and not self.exported:
                # actually run the export the menu is pointing at, so the
                # toast in the last shot reports a file that really exists
                self.exported = True
                try:
                    p = htmlify.write_element(self.pill, base=HERE)
                    st.say('Wrote %s' % os.path.relpath(p, HERE), secs=4.0)
                except Exception as e:
                    st.say('Export failed: %s' % e, secs=4.0)
                st.ctx_menu.open = False

        st.pressing = self.down
        if self.down:
            st.touch = self.cursor
        st.cursor = (self.cursor[0], self.cursor[1], self.down)


# ------------------------------------------------------------ encoding

def ffmpeg_exe():
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        return 'ffmpeg'


def open_encoder(path, size, fps):
    w, h = size
    cmd = [ffmpeg_exe(), '-y', '-loglevel', 'error',
           '-f', 'rawvideo', '-pix_fmt', 'rgb24',
           '-s', '%dx%d' % (w, h), '-r', str(fps), '-i', '-',
           '-an',
           # frames arrive bottom-up from the GL framebuffer
           '-vf', 'vflip',
           '-c:v', 'libx264', '-preset', 'slow', '-crf', '17',
           '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
           path]
    return subprocess.Popen(cmd, stdin=subprocess.PIPE)


# ------------------------------------------------------------ main

def record(out, size=(1440, 900), fps=30, dark=False, seconds=None):
    ctx = moderngl.create_context(standalone=True)
    tex = ctx.texture(size, 3)
    fbo = ctx.framebuffer(color_attachments=[tex])
    renderer = GlassRenderer(ctx, size)
    renderer.set_background(default_background(dark=dark))

    st = State(size)
    demo_scene(st)
    hud = HUD(size)
    demo = Demo(st)

    total = seconds or Demo.DURATION
    n = int(total * fps)
    dt = 1.0 / fps
    enc = open_encoder(out, size, fps)

    try:
        for i in range(n):
            t = i * dt
            demo.update(t)
            st.step(dt)
            if st.toast and st.time > st.toast_until:
                st.toast = None
            renderer.update_hud(hud.render(st.hud_state()))
            renderer.render(fbo, st.content_shapes(), st.ui_shapes(),
                            st.content_params(), st.ui_params())
            enc.stdin.write(fbo.read(components=3))
            if i % 30 == 0:
                pct = 100.0 * i / n
                sys.stdout.write('\r  %5.1f%%  frame %d/%d' % (pct, i, n))
                sys.stdout.flush()
    finally:
        enc.stdin.close()
        enc.wait()
    sys.stdout.write('\r  100.0%%  %d frames\n' % n)
    print('wrote', out)
    return out


def main():
    ap = argparse.ArgumentParser(description='Record a Liquid Glass demo')
    ap.add_argument('out', nargs='?', default='liquid-glass-demo.mp4')
    ap.add_argument('--size', default='1440x900')
    ap.add_argument('--fps', type=int, default=30)
    ap.add_argument('--seconds', type=float, default=None)
    ap.add_argument('--dark', action='store_true',
                    help='use the dark Golden Gate desktop picture')
    a = ap.parse_args()
    size = tuple(int(v) for v in a.size.lower().split('x'))
    record(a.out, size=size, fps=a.fps, dark=a.dark, seconds=a.seconds)


if __name__ == '__main__':
    main()
