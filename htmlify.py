"""HTMLify — export a Liquid Glass scene, or a single element, as HTML/CSS.

What the GPU pass does per pixel, a browser has to fake with layers, so the
translation is deliberate rather than mechanical:

* body      -> `backdrop-filter: blur() saturate()`, the one part of the
                material browsers implement natively;
* lensing   -> an SVG `feDisplacementMap` fed by a displacement map baked from
                the same SDF the shader uses. Emitted per element and wired up,
                but `backdrop-filter: url(#id)` is Chromium-only today, so the
                export degrades to the blur alone elsewhere;
* fringe    -> a masked conic-gradient ring, the CSS stand-in for the spectral
                edge separation;
* specular  -> a 145deg linear-gradient sweep in `screen`, matching the key /
                fill pair used by the shader;
* shadow    -> a plain drop `box-shadow`.

`element_css` returns just the rule, `element_html` a self-contained snippet,
`scene_html` a whole page.
"""

import base64
import io
import json
import math
import os

from PIL import Image

from engine import (Shape, KIND_CIRCLE, KIND_RRECT, KIND_TRI, KIND_RING,
                    KIND_PENT)

EXPORT_DIR = 'export'

# Tuned against the shader's own defaults so the two renderings agree.
GLASS_VARS = """  --lg-blur: 7px;
  --lg-saturate: 165%;
  --lg-tint: rgba(255, 255, 255, 0.14);
  --lg-rim: rgba(255, 255, 255, 0.60);
  --lg-rim-fill: rgba(255, 255, 255, 0.26);
  --lg-glow: rgba(255, 255, 255, 0.13);
  --lg-shadow: 0 10px 28px rgba(0, 0, 0, 0.20);
  --lg-fringe: 0.55;"""

BASE_CSS = """.lg {
  position: absolute;
  box-sizing: border-box;
  background: var(--lg-tint);
  -webkit-backdrop-filter: blur(var(--lg-blur)) saturate(var(--lg-saturate));
  backdrop-filter: blur(var(--lg-blur)) saturate(var(--lg-saturate));
  box-shadow: var(--lg-shadow),
              inset 0 1px 0 var(--lg-rim),
              inset 0 -1px 0 var(--lg-rim-fill),
              inset 0 0 20px var(--lg-glow);
  isolation: isolate;
}

/* specular: key above-left, fill below-right */
.lg::before {
  content: "";
  position: absolute;
  inset: 0;
  border-radius: inherit;
  background: linear-gradient(145deg,
      rgba(255, 255, 255, 0.50) 0%,
      rgba(255, 255, 255, 0.00) 30%,
      rgba(255, 255, 255, 0.00) 70%,
      rgba(255, 255, 255, 0.24) 100%);
  mix-blend-mode: screen;
  pointer-events: none;
}

/* spectral edge: the CSS stand-in for wavelength separation at the rim */
.lg::after {
  content: "";
  position: absolute;
  inset: 0;
  border-radius: inherit;
  padding: 1.5px;
  background: conic-gradient(from 210deg,
      rgba(255, 138, 76, 0.85), rgba(150, 255, 190, 0.65),
      rgba(96, 186, 255, 0.85), rgba(214, 132, 255, 0.75),
      rgba(255, 138, 76, 0.85));
  -webkit-mask: linear-gradient(#000 0 0) content-box, linear-gradient(#000 0 0);
  -webkit-mask-composite: xor;
  mask: linear-gradient(#000 0 0) content-box, linear-gradient(#000 0 0);
  mask-composite: exclude;
  mix-blend-mode: screen;
  opacity: var(--lg-fringe);
  pointer-events: none;
}

.lg-clip::before, .lg-clip::after { display: none; }
.lg-solid {
  position: absolute;
  box-sizing: border-box;
  pointer-events: none;
}"""


# ------------------------------------------------------------ displacement

def displacement_uri(shape, pad=24, edge=17.0, cap=1.0):
    """Bake the shape's rim normals into a PNG the browser can use as an
    feDisplacementMap input. R/G carry the outward normal scaled by the same
    circular lens profile the shader uses; 0.5 is no displacement."""
    w = int(shape.hw * 2 + pad * 2)
    h = int(shape.hh * 2 + pad * 2)
    w, h = max(w, 8), max(h, 8)
    probe = Shape(shape.kind, w / 2.0, h / 2.0, shape.hw, shape.hh,
                  rad=shape.rad, tint=shape.tint, merge=False)
    img = Image.new('RGBA', (w, h), (128, 128, 0, 255))
    px = img.load()
    e = 1.0
    for y in range(h):
        for x in range(w):
            d = probe.sdf(x + 0.5, y + 0.5)
            if d > 1.0 or d < -edge:
                continue
            gx = probe.sdf(x + 0.5 + e, y + 0.5) - probe.sdf(x + 0.5 - e, y + 0.5)
            gy = probe.sdf(x + 0.5, y + 0.5 + e) - probe.sdf(x + 0.5, y + 0.5 - e)
            n = math.hypot(gx, gy) or 1.0
            gx, gy = gx / n, gy / n
            rim = 1.0 - min(max(-d / edge, 0.0), 1.0)
            prof = 1.0 - math.sqrt(max(1.0 - rim * rim, 0.0))
            px[x, y] = (int(128 + 127 * gx * prof * cap),
                        int(128 + 127 * gy * prof * cap), 0, 255)
    buf = io.BytesIO()
    img.save(buf, format='PNG')
    return 'data:image/png;base64,' + base64.b64encode(buf.getvalue()).decode()


def lens_filter(fid, shape, scale=42.0):
    """SVG filter that refracts the backdrop through the baked map."""
    uri = displacement_uri(shape)
    return (f'<filter id="{fid}" x="-25%" y="-25%" width="150%" height="150%"\n'
            f'        color-interpolation-filters="sRGB">\n'
            f'  <feImage href="{uri}" result="map"'
            f' preserveAspectRatio="none"/>\n'
            f'  <feDisplacementMap in="SourceGraphic" in2="map"'
            f' scale="{scale:g}"\n'
            f'                     xChannelSelector="R" yChannelSelector="G"/>\n'
            f'</filter>')


# ------------------------------------------------------------ geometry

def _pent_clip():
    pts = []
    for i in range(5):
        a = math.radians(-90 + i * 72)
        pts.append('%.1f%% %.1f%%' % (50 + 50 * math.cos(a),
                                      50 + 50 * math.sin(a)))
    return 'polygon(%s)' % ', '.join(pts)


def shape_geometry(s):
    """CSS box + radius/clip for one Shape."""
    w, h = s.hw * 2.0, s.hh * 2.0
    g = {'left': s.x - s.hw, 'top': s.y - s.hh, 'width': w, 'height': h,
         'radius': None, 'clip': None, 'rotate': s.rot}
    if s.kind == KIND_CIRCLE:
        g['radius'] = '50%'
    elif s.kind == KIND_RRECT:
        g['radius'] = '%gpx' % min(s.rad, s.hw, s.hh)
    elif s.kind == KIND_TRI:
        g['clip'] = 'polygon(50% 4%, 96% 92%, 4% 92%)'
    elif s.kind == KIND_PENT:
        g['clip'] = _pent_clip()
    elif s.kind == KIND_RING:
        g['radius'] = '50%'
    return g


def _rgba(t):
    r, g, b = (max(0.0, min(1.0, c)) for c in t[:3])
    a = t[3] if len(t) > 3 else 1.0
    return 'rgba(%d, %d, %d, %.3f)' % (round(r * 255), round(g * 255),
                                       round(b * 255), a)


def _decl(g, extra=()):
    out = ['left: %gpx;' % round(g['left'], 1),
           'top: %gpx;' % round(g['top'], 1),
           'width: %gpx;' % round(g['width'], 1),
           'height: %gpx;' % round(g['height'], 1)]
    if g['radius']:
        out.append('border-radius: %s;' % g['radius'])
    if g['clip']:
        out.append('clip-path: %s;' % g['clip'])
    if abs(g['rotate']) > 1e-4:
        out.append('transform: rotate(%.3frad);' % g['rotate'])
    out.extend(extra)
    return out


# ------------------------------------------------------------ per element

def describe(obj):
    """(css class stem, human title) for any scene object."""
    from app import Menu, Slider, Switch
    if isinstance(obj, Switch):
        return 'switch', 'Switch'
    if isinstance(obj, Slider):
        return 'slider', 'Slider'
    if isinstance(obj, Menu):
        return 'menu', 'Menu'
    if isinstance(obj, Shape):
        return ({KIND_CIRCLE: ('circle', 'Circle'),
                 KIND_RRECT: ('rrect', 'Rounded rect'),
                 KIND_TRI: ('triangle', 'Triangle'),
                 KIND_PENT: ('pentagon', 'Pentagon'),
                 KIND_RING: ('ring', 'Ring')}
                .get(obj.kind, ('shape', 'Shape')))
    return 'element', 'Element'


def _parts(obj):
    """Flatten a scene object into (class-suffix, geometry, extra-decls,
    is_glass) pieces. Controls are more than one box."""
    from app import BLUE, GREY, Menu, Slider, Switch
    stem, _ = describe(obj)

    if isinstance(obj, Shape):
        g = shape_geometry(obj)
        extra = []
        if obj.tint[3] > 0.001:
            extra.append('background: %s;' % _rgba(obj.tint))
        return [(stem, g, extra, True)]

    if isinstance(obj, Switch):
        kx, ky = obj.knob_xy()
        track = {'left': obj.x - obj.W / 2, 'top': obj.y - obj.H / 2,
                 'width': obj.W, 'height': obj.H,
                 'radius': '%gpx' % (obj.H / 2), 'clip': None, 'rotate': 0.0}
        knob = {'left': kx - obj.r, 'top': ky - obj.r,
                'width': obj.r * 2, 'height': obj.r * 2,
                'radius': '50%', 'clip': None, 'rotate': 0.0}
        tr = obj.track_shape()
        return [('switch-track', track,
                 ['background: %s;' % _rgba(tr.tint)], False),
                ('switch-knob', knob,
                 ['background: rgba(255, 255, 255, 0.88);'], True)]

    if isinstance(obj, Slider):
        kx = obj.knob_x()
        th = 4.5
        # the two halves overlap by `th` under the knob, exactly as the
        # renderer lays them out, so no seam shows through the glass
        fill = {'left': obj.x0, 'top': obj.cy - th,
                'width': max(kx + th - obj.x0, th * 2),
                'height': th * 2, 'radius': '%gpx' % th, 'clip': None,
                'rotate': 0.0}
        rest = {'left': min(kx - th, obj.x1 - th * 2), 'top': obj.cy - th,
                'width': max(obj.x1 - kx + th, th * 2),
                'height': th * 2, 'radius': '%gpx' % th, 'clip': None,
                'rotate': 0.0}
        knob = {'left': kx - obj.knob_hw, 'top': obj.cy - obj.knob_hh,
                'width': obj.knob_hw * 2, 'height': obj.knob_hh * 2,
                'radius': '%gpx' % obj.knob_hh, 'clip': None, 'rotate': 0.0}
        return [('slider-fill', fill, ['background: %s;' % _rgba(BLUE)], False),
                ('slider-rest', rest, ['background: %s;' % _rgba(GREY)], False),
                ('slider-knob', knob,
                 ['background: rgba(255, 255, 255, 0.20);'], True)]

    if isinstance(obj, Menu):
        px, py, pw, ph = obj.panel_rect()
        btn = {'left': obj.x - obj.R, 'top': obj.y - obj.R,
               'width': obj.R * 2, 'height': obj.R * 2,
               'radius': '50%', 'clip': None, 'rotate': 0.0}
        panel = {'left': px, 'top': py, 'width': pw, 'height': ph,
                 'radius': '20px', 'clip': None, 'rotate': 0.0}
        return [('menu-button', btn,
                 ['background: rgba(255, 255, 255, 0.16);'], True),
                ('menu-panel', panel,
                 ['background: rgba(255, 255, 255, 0.22);'], True)]

    return []


def element_css(obj, name=None):
    """Just the CSS rule(s) for one element."""
    stem, title = describe(obj)
    name = name or stem
    lines = ['/* %s — Liquid Glass, exported by HTMLify */' % title,
             ':root {', GLASS_VARS, '}', '']
    for i, (suffix, g, extra, glass) in enumerate(_parts(obj)):
        sel = '.%s' % name if len(_parts(obj)) == 1 else '.%s .%s' % (name, suffix)
        body = '\n'.join('  ' + d for d in _decl(g, extra))
        lines.append('%s {\n%s\n}' % (sel, body))
        lines.append('')
    lines.append(BASE_CSS)
    return '\n'.join(lines).rstrip() + '\n'


def element_html(obj, name=None):
    """A self-contained snippet: filter defs, style, markup."""
    stem, title = describe(obj)
    name = name or stem
    parts = _parts(obj)
    defs, nodes, rules = [], [], []
    for i, (suffix, g, extra, glass) in enumerate(parts):
        cls = name if len(parts) == 1 else '%s-%s' % (name, suffix)
        sel = '.' + cls
        decls = list(_decl(g, extra))
        if glass:
            fid = 'lens-%s-%d' % (name, i)
            defs.append(lens_filter(fid, _probe_shape(g)))
            decls.append('/* Chromium refracts the backdrop; elsewhere the')
            decls.append('   blur above stands in */')
            decls.append('-webkit-backdrop-filter: blur(var(--lg-blur))'
                         ' saturate(var(--lg-saturate)) url(#%s);' % fid)
            decls.append('backdrop-filter: blur(var(--lg-blur))'
                         ' saturate(var(--lg-saturate)) url(#%s);' % fid)
        rules.append('%s {\n%s\n}' % (sel, '\n'.join('  ' + d for d in decls)))
        nodes.append('<div class="%s%s"></div>'
                     % ('lg ' if glass else 'lg-solid ', cls))
    return (
        '<!-- %s — Liquid Glass, exported by HTMLify -->\n' % title
        + '<svg width="0" height="0" style="position:absolute">\n<defs>\n'
        + '\n'.join(defs) + '\n</defs>\n</svg>\n\n'
        + '<style>\n:root {\n' + GLASS_VARS + '\n}\n\n'
        + BASE_CSS + '\n\n' + '\n\n'.join(rules) + '\n</style>\n\n'
        + '\n'.join(nodes) + '\n')


def _probe_shape(g):
    """A Shape standing in for a CSS box, so the lens map can be baked."""
    hw, hh = g['width'] / 2.0, g['height'] / 2.0
    if g['radius'] == '50%':
        return Shape(KIND_CIRCLE, 0, 0, hw, hh)
    rad = 0.0
    if g['radius']:
        try:
            rad = float(str(g['radius']).replace('px', ''))
        except ValueError:
            rad = 0.0
    return Shape(KIND_RRECT, 0, 0, hw, hh, rad=rad)


# ------------------------------------------------------------ whole scene

def scene_html(state, bg='golden-gate.png', title='Liquid Glass scene'):
    W, H = state.size
    defs, rules, nodes = [], [], []
    seq = 0

    def emit(obj, group):
        nonlocal seq
        for i, (suffix, g, extra, glass) in enumerate(_parts(obj)):
            seq += 1
            cls = 'el%d-%s' % (seq, suffix)
            decls = list(_decl(g, extra))
            if glass:
                fid = 'lens%d' % seq
                defs.append(lens_filter(fid, _probe_shape(g)))
                decls.append('-webkit-backdrop-filter: blur(var(--lg-blur))'
                             ' saturate(var(--lg-saturate)) url(#%s);' % fid)
                decls.append('backdrop-filter: blur(var(--lg-blur))'
                             ' saturate(var(--lg-saturate)) url(#%s);' % fid)
            rules.append('.%s {\n%s\n}' % (cls, '\n'.join('  ' + d
                                                          for d in decls)))
            nodes.append('  <div class="%s%s" data-group="%s"></div>'
                         % ('lg ' if glass else 'lg-solid ', cls, group))

    for s in state.content:
        emit(s, 'shape')
    for sl in state.all_sliders():
        emit(sl, 'slider')
    for sw in state.switches:
        emit(sw, 'switch')
    for m in state.menus:
        emit(m, 'menu')

    return f"""<!DOCTYPE html>
<meta charset="utf-8">
<title>{title}</title>
<!-- Exported by HTMLify from winliqglass. Self-contained apart from the
     wallpaper next to it. Open it straight from disk. -->
<svg width="0" height="0" style="position:absolute" aria-hidden="true">
<defs>
{chr(10).join(defs)}
</defs>
</svg>
<style>
:root {{
{GLASS_VARS}
}}
html, body {{ margin: 0; height: 100%; }}
body {{
  background: #1b1b20 url("{bg}") center / cover no-repeat;
  font: 13px/1.4 -apple-system, "SF Pro Text", "Segoe UI", system-ui, sans-serif;
  color: #fff;
}}
.stage {{
  position: relative;
  width: {W}px;
  height: {H}px;
  margin: 0 auto;
  overflow: hidden;
}}

{BASE_CSS}

{chr(10).join(rules)}
</style>
<div class="stage">
{chr(10).join(nodes)}
</div>
"""


# ------------------------------------------------------------ file writing

def _outdir(base):
    d = os.path.join(base, EXPORT_DIR)
    os.makedirs(d, exist_ok=True)
    return d


def write_scene(state, base='.', bg='golden-gate.png'):
    d = _outdir(base)
    p = os.path.join(d, 'scene.html')
    # the wallpaper lives beside the sources, one level up from export/
    href = os.path.relpath(os.path.join(base, bg), d).replace(os.sep, '/')
    with open(p, 'w', encoding='utf-8') as f:
        f.write(scene_html(state, bg=href))
    return p


def write_element(obj, base='.', as_css=False):
    d = _outdir(base)
    stem, _ = describe(obj)
    ext = 'css' if as_css else 'html'
    n = 1
    while os.path.exists(os.path.join(d, '%s-%d.%s' % (stem, n, ext))):
        n += 1
    p = os.path.join(d, '%s-%d.%s' % (stem, n, ext))
    with open(p, 'w', encoding='utf-8') as f:
        f.write(element_css(obj) if as_css else element_html(obj))
    return p
