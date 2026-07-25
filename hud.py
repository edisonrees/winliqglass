"""Pillow-drawn HUD overlay: tabs, tool icons, labels, hints, menu panels
and the Lockscreen chrome (phone outline, clock, status bar). Redrawn only
when app state changes, then uploaded as a texture and alpha-blended over
the glass passes."""

import math

from PIL import Image, ImageDraw, ImageFont, ImageFilter

INK = (30, 32, 38, 235)
INK_DIM = (30, 32, 38, 170)
PAPER = (255, 255, 255, 235)
HALO = (255, 255, 255, 130)

# Studio chrome is drawn white into a single layer and given one soft dark
# shadow underneath. That is what lets a glyph stay legible whether the glass
# beneath it has gone light over a bright wallpaper or dark over a dim one —
# flipping the ink colour per element cannot survive a gradient crossing it.
MARK = (255, 255, 255, 245)
MARK_DIM = (255, 255, 255, 175)
SHADOW_RGB = (6, 8, 12)

_fonts = {}


def _font(size, light=False, bold=False):
    key = (size, light, bold)
    if key not in _fonts:
        if light:
            names = ['segoeuil.ttf', 'segoeui.ttf', 'arial.ttf']
        elif bold:
            names = ['seguisb.ttf', 'arialbd.ttf']
        else:
            names = ['segoeui.ttf', 'arial.ttf']
        f = ImageFont.load_default()
        for n in names:
            try:
                f = ImageFont.truetype(n, size)
                break
            except OSError:
                continue
        _fonts[key] = f
    return _fonts[key]


def _text(dr, xy, s, font, fill=INK, anchor='la'):
    x, y = xy
    dr.text((x, y + 1), s, font=font, fill=HALO, anchor=anchor)
    dr.text((x, y), s, font=font, fill=fill, anchor=anchor)


# ---------------------------------------------------------------- icons

def _i_select(dr, x, y, s, c):
    pts = [(x - s * 0.40, y - s * 0.62), (x - s * 0.40, y + 0.52 * s),
           (x - s * 0.10, y + 0.24 * s), (x + 0.18 * s, y + 0.66 * s),
           (x + 0.38 * s, y + 0.52 * s), (x + 0.10 * s, y + 0.12 * s),
           (x + 0.46 * s, y + 0.04 * s)]
    dr.polygon(pts, fill=c)


def _i_circle(dr, x, y, s, c):
    dr.ellipse([x - s * 0.6, y - s * 0.6, x + s * 0.6, y + s * 0.6], outline=c, width=3)


def _i_rect(dr, x, y, s, c):
    dr.rounded_rectangle([x - s * 0.62, y - s * 0.52, x + s * 0.62, y + s * 0.52],
                         radius=s * 0.22, outline=c, width=3)


def _i_pill(dr, x, y, s, c):
    dr.rounded_rectangle([x - s * 0.68, y - s * 0.34, x + s * 0.68, y + s * 0.34],
                         radius=s * 0.34, outline=c, width=3)


def _i_tri(dr, x, y, s, c):
    dr.polygon([(x, y - s * 0.60), (x - s * 0.62, y + s * 0.48), (x + s * 0.62, y + s * 0.48)],
               outline=c, width=3)


def _pent_pts(x, y, r):
    return [(x + r * math.cos(math.radians(-90 + i * 72)),
             y + r * math.sin(math.radians(-90 + i * 72))) for i in range(5)]


def _i_pent(dr, x, y, s, c):
    dr.polygon(_pent_pts(x, y, s * 0.62), outline=c, width=3)


def _i_switch(dr, x, y, s, c):
    dr.rounded_rectangle([x - s * 0.72, y - s * 0.40, x + s * 0.72, y + s * 0.40],
                         radius=s * 0.40, outline=c, width=3)
    dr.ellipse([x + s * 0.04, y - s * 0.26, x + s * 0.56, y + s * 0.26], fill=c)


def _i_slider(dr, x, y, s, c):
    dr.line([x - s * 0.72, y, x + s * 0.72, y], fill=c, width=3)
    dr.rounded_rectangle([x - s * 0.26, y - s * 0.30, x + s * 0.34, y + s * 0.30],
                         radius=s * 0.30, fill=c)


def _i_cursor(dr, x, y, s, c):
    """Pointer arrow. Drawn into the ink layer so it picks up the same drop
    shadow as everything else and stays readable over any wallpaper."""
    pts = [(x, y), (x, y + 1.62 * s), (x + 0.40 * s, y + 1.22 * s),
           (x + 0.66 * s, y + 1.80 * s), (x + 0.94 * s, y + 1.66 * s),
           (x + 0.68 * s, y + 1.10 * s), (x + 1.16 * s, y + 1.06 * s)]
    dr.polygon(pts, fill=c, outline=(0, 0, 0, 120))


def _i_dots(dr, x, y, s, c):
    for dx in (-0.42, 0.0, 0.42):
        dr.ellipse([x + dx * s - 2.6, y - 2.6, x + dx * s + 2.6, y + 2.6], fill=c)


def _i_open(dr, x, y, s, c):
    dr.rounded_rectangle([x - s * 0.62, y - s * 0.40, x + s * 0.62, y + s * 0.46],
                         radius=4, outline=c, width=3)
    dr.line([x - s * 0.62, y - s * 0.40, x - s * 0.30, y - s * 0.40], fill=c, width=8)


def _i_save(dr, x, y, s, c):
    dr.line([x, y - s * 0.55, x, y + s * 0.25], fill=c, width=3)
    dr.polygon([(x - s * 0.28, y + 0.02 * s), (x + s * 0.28, y + 0.02 * s), (x, y + 0.34 * s)], fill=c)
    dr.line([x - s * 0.55, y + s * 0.55, x + s * 0.55, y + s * 0.55], fill=c, width=3)


def _i_reset(dr, x, y, s, c):
    dr.arc([x - s * 0.55, y - s * 0.55, x + s * 0.55, y + s * 0.55],
           start=-50, end=230, fill=c, width=3)
    ax, ay = x + s * 0.36, y - s * 0.42
    dr.polygon([(ax - 5, ay - 4), (ax + 5, ay + 1), (ax - 3, ay + 6)], fill=c)


def _i_trash(dr, x, y, s, c):
    dr.rounded_rectangle([x - s * 0.42, y - s * 0.30, x + s * 0.42, y + s * 0.58],
                         radius=4, outline=c, width=3)
    dr.line([x - s * 0.58, y - s * 0.42, x + s * 0.58, y - s * 0.42], fill=c, width=3)
    dr.line([x - s * 0.18, y - s * 0.58, x + s * 0.18, y - s * 0.58], fill=c, width=3)
    for dx in (-0.16, 0.16):
        dr.line([x + dx * s, y - s * 0.10, x + dx * s, y + s * 0.38], fill=c, width=2)


def _i_flash(dr, x, y, s, c):
    dr.polygon([(x - s * 0.34, y - s * 0.60), (x + s * 0.34, y - s * 0.60),
                (x + s * 0.20, y - s * 0.10), (x - s * 0.20, y - s * 0.10)], fill=c)
    dr.rounded_rectangle([x - s * 0.20, y - s * 0.10, x + s * 0.20, y + s * 0.60],
                         radius=3, fill=c)
    dr.line([x, y + s * 0.05, x, y + s * 0.22], fill=(0, 0, 0, 120), width=2)


def _i_cam(dr, x, y, s, c):
    dr.rounded_rectangle([x - s * 0.60, y - s * 0.36, x + s * 0.60, y + s * 0.44],
                         radius=5, fill=c)
    dr.polygon([(x - s * 0.22, y - s * 0.36), (x - s * 0.14, y - s * 0.52),
                (x + s * 0.14, y - s * 0.52), (x + s * 0.22, y - s * 0.36)], fill=c)
    dr.ellipse([x - s * 0.20, y - s * 0.16, x + s * 0.20, y + s * 0.24],
               fill=(0, 0, 0, 130))


ICONS = {'select': _i_select, 'circle': _i_circle, 'rect': _i_rect, 'pill': _i_pill,
         'tri': _i_tri, 'pent': _i_pent, 'switch': _i_switch, 'slider': _i_slider,
         'dots': _i_dots, 'open': _i_open, 'save': _i_save, 'reset': _i_reset,
         'trash': _i_trash, 'flash': _i_flash, 'cam': _i_cam}


# ---------------------------------------------------------------- lock chrome

def _status_bar(dr, right, y, k, a):
    c = (255, 255, 255, int(235 * a))
    # battery
    bw, bh = 25 * k, 12 * k
    bx = right - bw
    dr.rounded_rectangle([bx, y - bh / 2, bx + bw, y + bh / 2],
                         radius=3.5 * k, outline=c, width=max(1, int(1.5 * k)))
    dr.rounded_rectangle([bx + 2 * k, y - bh / 2 + 2 * k, bx + bw - 2 * k, y + bh / 2 - 2 * k],
                         radius=2 * k, fill=c)
    dr.rounded_rectangle([bx + bw + 1.5 * k, y - 2.5 * k, bx + bw + 3.5 * k, y + 2.5 * k],
                         radius=1.5 * k, fill=c)
    # wifi: three nested arcs opening upward around (wcx, wcy)
    wcx, wcy = bx - 16 * k, y + 4 * k
    for r in (9.5 * k, 6.2 * k, 2.9 * k):
        dr.arc([wcx - r, wcy - r, wcx + r, wcy + r],
               start=225, end=315, fill=c, width=max(1, int(2.2 * k)))
    dr.ellipse([wcx - 1.6 * k, wcy - 3.4 * k, wcx + 1.6 * k, wcy - 0.2 * k], fill=c)
    # signal bars
    sx = wcx - 12 * k - 24 * k
    for i in range(4):
        h = (4 + i * 2.6) * k
        x0 = sx + i * 5.6 * k
        dr.rounded_rectangle([x0, y + 5 * k - h, x0 + 3.6 * k, y + 5 * k],
                             radius=1.2 * k, fill=c)


class HUD:
    def __init__(self, size):
        self.f12 = _font(12)
        self.f13 = _font(13)
        self.f14 = _font(14)
        self.size = size

    def resize(self, size):
        self.size = size

    # ---- lockscreen chrome, drawn in a screen-local layer so lifted
    # content clips at the phone screen bounds
    def _draw_lock(self, img, dr, lk):
        sx, sy, sw, sh = lk['screen']
        k = sh / 956.0
        a = lk['alpha']
        lift = lk['lift']
        layer = Image.new('RGBA', (sw, sh), (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)

        # Dynamic Island
        iw, ih = 122 * k, 36 * k
        d.rounded_rectangle([sw / 2 - iw / 2, 12 * k, sw / 2 + iw / 2, 12 * k + ih],
                            radius=ih / 2, fill=(5, 5, 8, 255))
        _status_bar(d, sw - 30 * k, 28 * k, k, 1.0)

        if a > 0.02:
            aa = int(255 * a)
            d.text((sw / 2, 120 * k - lift), lk['date'],
                   font=_font(int(30 * k)), fill=(255, 255, 255, int(225 * a)),
                   anchor='mm')
            # glassy time: soft glow + translucent body + faint shade
            tlayer = Image.new('RGBA', (sw, sh), (0, 0, 0, 0))
            d2 = ImageDraw.Draw(tlayer)
            tf = _font(int(168 * k), light=True)
            ty = 212 * k - lift
            d2.text((sw / 2, ty), lk['time'], font=tf,
                    fill=(255, 255, 255, 255), anchor='mm')
            glow = tlayer.filter(ImageFilter.GaussianBlur(7 * k))
            glow.putalpha(glow.getchannel('A').point(lambda v: int(v * 0.40 * a)))
            layer.alpha_composite(glow)
            d.text((sw / 2, ty + 2 * k), lk['time'], font=tf,
                   fill=(40, 60, 80, int(45 * a)), anchor='mm')
            d.text((sw / 2, ty), lk['time'], font=tf,
                   fill=(255, 255, 255, int(150 * a)), anchor='mm')
            for cx, cy, icon in lk['buttons']:
                ICONS[icon](d, cx, cy, 17 * k / 0.8, (255, 255, 255, aa))
        # home indicator
        hw = sw * 0.335
        d.rounded_rectangle([sw / 2 - hw / 2, sh - 12 * k,
                             sw / 2 + hw / 2, sh - 12 * k + 5 * k],
                            radius=2.5 * k, fill=(255, 255, 255, 235))
        img.alpha_composite(layer, dest=(sx, sy))

    def _draw_studio(self, dr, st):
        """All studio chrome, in white, into the shadowed ink layer."""
        for gid, slots in (('tool', st['tool_slots']), ('util', st['util_slots'])):
            for sid, cx, cy in slots:
                hovered = st['hover'] == (gid, sid)
                active = gid == 'tool' and st['tool'] == sid
                if active:
                    dr.ellipse([cx - 18, cy - 18, cx + 18, cy + 18],
                               fill=(255, 255, 255, 62))
                elif hovered:
                    dr.ellipse([cx - 18, cy - 18, cx + 18, cy + 18],
                               fill=(255, 255, 255, 30))
                ICONS[sid](dr, cx, cy, 15,
                           MARK if (active or hovered) else MARK_DIM)
        for label, x0, x1, cy, v in st['sliders']:
            dr.text((x0 - 13, cy), label, font=self.f13, fill=MARK_DIM, anchor='rm')

        for m in st['menus']:
            _i_dots(dr, m['x'], m['y'], 13, MARK)
            pop = m['pop']
            # content reveals after the panel has scaled most of the way in
            if pop > 0.55:
                fade = min((pop - 0.55) / 0.30, 1.0)
                ia = int(245 * fade)
                da = int(60 * fade)
                px, py, pw, ph = m['panel']
                pad, items, ih = m['pad'], m['items'], m['ih']
                for i, (label, icon) in enumerate(items):
                    iy = py + pad + ih * (i + 0.5)
                    if i == m.get('hover'):
                        dr.rounded_rectangle([px + 6, iy - ih / 2 + 3,
                                              px + pw - 6, iy + ih / 2 - 3],
                                             radius=13,
                                             fill=(255, 255, 255, int(70 * fade)))
                    dr.text((px + 20, iy), label, font=self.f14,
                            fill=(255, 255, 255, ia), anchor='lm')
                    ICONS[icon](dr, px + pw - 26, iy, 11, (255, 255, 255, ia))
                    if i < len(items) - 1:
                        y = py + pad + ih * (i + 1)
                        dr.line([px + 16, y, px + pw - 16, y],
                                fill=(255, 255, 255, da), width=1)

        cm = st.get('ctx')
        if cm and cm['pop'] > 0.55:
            fade = min((cm['pop'] - 0.55) / 0.30, 1.0)
            ia = int(245 * fade)
            px, py, pw, ph = cm['panel']
            pad, items, ih = cm['pad'], cm['items'], cm['ih']
            dr.text((px + 18, py + pad * 0.5 + 1), cm['title'], font=self.f12,
                    fill=(255, 255, 255, int(150 * fade)), anchor='lm')
            for i, label in enumerate(items):
                iy = py + pad + ih * (i + 0.5)
                if i == cm.get('hover'):
                    dr.rounded_rectangle([px + 6, iy - ih / 2 + 3,
                                          px + pw - 6, iy + ih / 2 - 3],
                                         radius=11,
                                         fill=(255, 255, 255, int(70 * fade)))
                dr.text((px + 18, iy), label, font=self.f13,
                        fill=(255, 255, 255, ia), anchor='lm')

        for label, (x, y, w, h), active in st['tabs']:
            dr.rounded_rectangle([x, y, x + w, y + h], radius=h / 2,
                                 fill=(255, 255, 255, 70 if active else 26))
            dr.text((x + w / 2, y + h / 2), label, font=self.f13,
                    fill=MARK if active else MARK_DIM, anchor='mm')

        dr.text(st['hint_pos'], st['hint'], font=self.f13, fill=MARK_DIM)
        if st.get('toast'):
            dr.text((st['hint_pos'][0], st['hint_pos'][1] + 22), st['toast'],
                    font=self.f13, fill=MARK)

        cur = st.get('cursor')
        if cur:
            cx, cy, down = cur
            if down:
                dr.ellipse([cx - 17, cy - 17, cx + 17, cy + 17],
                           fill=(255, 255, 255, 46))
            _i_cursor(dr, cx, cy, 11, MARK)

    def render(self, st) -> Image.Image:
        img = Image.new('RGBA', self.size, (0, 0, 0, 0))

        if st['mode'] == 'studio':
            ink = Image.new('RGBA', self.size, (0, 0, 0, 0))
            self._draw_studio(ImageDraw.Draw(ink), st)
            a = ink.getchannel('A').filter(ImageFilter.GaussianBlur(2.4))
            shadow = Image.new('RGBA', self.size, SHADOW_RGB + (0,))
            shadow.putalpha(a.point(lambda v: (v * 150) // 255))
            img.alpha_composite(shadow, dest=(0, 1))
            img.alpha_composite(ink)
            return img

        dr = ImageDraw.Draw(img)
        lk = st['lock']
        self._draw_lock(img, dr, lk)
        img.alpha_composite(lk['outline'], dest=lk['phone_pos'])
        dr = ImageDraw.Draw(img)
        for label, (x, y, w, h), active in st['tabs']:
            dr.rounded_rectangle([x, y, x + w, y + h], radius=h / 2,
                                 fill=(255, 255, 255, 215 if active else 85))
            dr.text((x + w / 2, y + h / 2), label, font=self.f13,
                    fill=INK if active else INK_DIM, anchor='mm')
        _text(dr, st['hint_pos'], st['hint'], self.f13, fill=INK_DIM)
        if st.get('toast'):
            _text(dr, (st['hint_pos'][0], st['hint_pos'][1] + 22),
                  st['toast'], self.f13, fill=INK)
        return img
