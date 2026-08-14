"""Looping material clips — glass over a wallpaper, no chrome, no cursor.

    py shots.py out.mp4
    py shots.py out.mp4 --wall wallpaper4.jpg --seconds 6
    py shots.py out.mp4 --detail          # corner close-up, light laps it once
    py shots.py --all shots/              # the whole set, one file per wallpaper

`record.py` drives the studio through a scripted session, widgets and HUD and
all. This is the other thing you want to look at: the material on its own, a
superellipse card and a button and a slider, moving over a desktop picture so
the lens has something changing to bend.

Everything is a function of loop phase rather than elapsed time, so the clips
cut back to their first frame exactly and can run on repeat.

The slider is a genuine two-pass composite — track on the content plane, knob
on the UI plane — so the knob refracts the track beneath it rather than
sitting on a picture of it. That is the part a mockup fakes.
"""

import argparse
import math
import os

from PIL import Image

from app import _standalone_context, default_background, LIGHT_ANGLE, BLUE, GREY
from engine import GlassRenderer, Shape, KIND_RRECT
from record import open_encoder

HERE = os.path.dirname(os.path.abspath(__file__))

Image.MAX_IMAGE_PIXELS = None

# wallpaper, crop fraction, crop centre, clip name
SET = [
    ('golden-gate.png',      0.42, (0.34, 0.60), 'golden-gate'),
    ('golden-gate-dark.png', 0.42, (0.34, 0.60), 'golden-gate-dark'),
    ('wallpaper2.png',       0.50, (0.50, 0.58), 'blue-wave'),
    ('wallpaper3.jpg',       0.50, (0.50, 0.50), 'colour-ridges'),
    ('wallpaper4.jpg',       0.46, (0.50, 0.45), 'rays'),
]


def load_wall(name, zoom=1.0, off=(0.5, 0.5), width=2400):
    """A desktop picture, cropped in so its detail fills the frame.

    Fitted whole, a 6K wallpaper puts mostly flat gradient behind a 960px
    frame, and a lens over flat content has nothing to show. Cropping to
    roughly half keeps a caustic or a colour boundary under the glass the
    whole way through. Downsampling to a few times the render width keeps the
    mip chain cheap without starving the rim.
    """
    try:
        img = Image.open(os.path.join(HERE, name)).convert('RGB')
    except Exception:
        img = default_background().convert('RGB')
    if zoom < 0.999:
        w, h = img.size
        cw, ch = int(w * zoom), int(h * zoom)
        img = img.crop((int((w - cw) * off[0]), int((h - ch) * off[1]),
                        int((w - cw) * off[0]) + cw,
                        int((h - ch) * off[1]) + ch))
    return img.resize((width, max(1, round(width * img.height / img.width))),
                      Image.LANCZOS)


def _bump(u):
    """0 -> 1 -> 0 across u in [0,1], flat outside it."""
    return 0.0 if u <= 0.0 or u >= 1.0 else math.sin(u * math.pi) ** 2


def _press(ph, centre, width):
    """A press pulse centred on a loop phase, wrapping across the seam."""
    return _bump(((ph - centre + 0.5) % 1.0 - 0.5) / width + 0.5)


def _lights(a, touch=(0.0, 0.0), touch_a=0.0):
    kx, ky = math.cos(a), math.sin(a)
    return dict(uKey=(kx * 0.62, ky * 0.62, 0.78),
                uFill=(-kx * 0.55, -ky * 0.55, 0.62),
                uTouch=touch, uTouchA=touch_a)


def _params(lights, *, opacity, bend, edge, disp, spec, shine,
            aniso=1.0, adapt=0.50, sat=0.07, rimlit=0.30):
    """The keyword defaults are app.py's *content* preset; the UI plane
    overrides the four that differ there. Collapsing the two loses the
    distinction — the UI plane damps its bend horizontally and carries a
    brighter hairline, which is what keeps a small knob from reading as a
    smear."""
    return dict(uOpacity=opacity, uFrost=0.0, uBend=bend, uMergeK=26.0,
                uEdge=edge, uAniso=aniso, uDisp=disp, uTravel=0.85,
                uShadow=0.0, uShadowR=9.0, uSpec=spec, uShine=shine,
                uAdapt=adapt, uSat=sat, uRimLit=rimlit, **lights)


def composition(ph, W, H):
    """Card, button and slider, all three moving. Returns (content, ui, cp, up)."""
    r = min(W / 1000.0, H / 620.0)
    tau = 2.0 * math.pi * ph

    # the light drifts, so the specular travels the corner rather than sitting
    lights_a = LIGHT_ANGLE + 0.34 * math.sin(tau)

    # card: a Lissajous, so it crosses the wallpaper's ribbons twice a loop
    card = Shape(KIND_RRECT,
                 0.31 * W + 0.085 * W * math.sin(tau),
                 0.37 * H + 0.045 * H * math.sin(2.0 * tau),
                 152 * r, 112 * r, rad=54 * r,     # partial radius -> squircle
                 merge=False)

    # button: two presses a loop, each a pop and a touch light
    p = max(_press(ph, 0.22, 0.16), _press(ph, 0.68, 0.16))
    btn = Shape(KIND_RRECT, 0.72 * W, 0.30 * H, 74 * r, 30 * r, rad=24 * r,
                tint=(1, 1, 1, 0.10 + 0.06 * p), merge=False)
    btn.scale = 1.0 + 0.055 * p

    # slider: track on the content plane, glass knob on the UI plane
    length, cx, cy = 0.46 * W, 0.50 * W, 0.74 * H
    khw, khh = 26 * r, 16 * r
    x0, x1 = cx - length / 2, cx + length / 2
    m = khw + 4
    kx = x0 + m + (0.5 - 0.44 * math.cos(tau)) * (length - 2 * m)
    th = 5.0 * r
    lw, rw = max((kx + th - x0) / 2, th), max((x1 - kx + th) / 2, th)
    track = [Shape(KIND_RRECT, x0 + lw, cy, lw, th, rad=th, tint=BLUE, merge=False),
             Shape(KIND_RRECT, x1 - rw, cy, rw, th, rad=th, tint=GREY, merge=False)]
    knob = Shape(KIND_RRECT, kx, cy, khw, khh, rad=khh,   # capsule: stays round
                 tint=(1, 1, 1, 0.20), merge=False, rim=-0.35)

    lit = _lights(lights_a, (0.72 * W, 0.30 * H), 0.30 * p)
    return (track, [card, btn, knob],
            _params(lit, opacity=0.10, bend=40.0, edge=12.0, disp=0.70,
                    spec=0.30, shine=22.0),
            _params(lit, opacity=0.13, bend=24.0 * r, edge=8.0, disp=0.90,
                    spec=0.36, shine=26.0,
                    aniso=0.85, adapt=0.48, sat=0.06, rimlit=0.36))


def detail(ph, W, H):
    """One corner, big enough to read, with the key light lapping it once."""
    tau = 2.0 * math.pi * ph
    lit = _lights(LIGHT_ANGLE + tau)
    s = min(W / 960.0, H / 600.0)
    card = Shape(KIND_RRECT,
                 W * 0.5 + 18 * s * math.sin(tau),
                 H * 0.5 + 12 * s * math.sin(2.0 * tau),
                 300 * s, 208 * s, rad=104 * s, merge=False)
    p = _params(lit, opacity=0.12, bend=46.0 * s, edge=11.0 * s, disp=0.95,
                spec=0.40, shine=24.0,
                aniso=1.0, adapt=0.48, sat=0.06, rimlit=0.38)
    return [], [card], p, p


def render(out, wall='golden-gate.png', size=(960, 600), fps=30, seconds=8.0,
           zoom=None, off=(0.5, 0.5), scene=composition):
    W, H = size
    if zoom is None:
        zoom = 0.30 if scene is detail else 0.42
    ctx = _standalone_context()
    tex = ctx.texture(size, 3)
    fbo = ctx.framebuffer(color_attachments=[tex])
    rend = GlassRenderer(ctx, size)
    rend.set_background(load_wall(wall, zoom, off))
    rend.update_hud(Image.new('RGBA', size, (0, 0, 0, 0)))

    n = max(1, int(round(fps * seconds)))
    enc = open_encoder(out, size, fps)
    try:
        for i in range(n):
            content, ui, cp, up = scene(i / n, W, H)
            rend.render(fbo, content, ui, cp, up)
            enc.stdin.write(fbo.read(components=3))
            if i % 20 == 0:
                print('  %d/%d' % (i, n), end='\r', flush=True)
    finally:
        enc.stdin.close()
        enc.wait()
        ctx.release()
    print('wrote %s  %dx%d  %d frames @%d' % (out, W, H, n, fps))


def main():
    ap = argparse.ArgumentParser(description='Liquid Glass material clips')
    ap.add_argument('out', nargs='?', default='shot.mp4',
                    help='output .mp4, or the output directory with --all')
    ap.add_argument('--all', action='store_true',
                    help='render one clip per wallpaper into the output dir')
    ap.add_argument('--wall', default='golden-gate.png')
    ap.add_argument('--detail', action='store_true',
                    help='corner close-up instead of the full composition')
    ap.add_argument('--size', default='960x600')
    ap.add_argument('--fps', type=int, default=30)
    ap.add_argument('--seconds', type=float, default=8.0)
    ap.add_argument('--zoom', type=float, default=None,
                    help='wallpaper crop fraction, 1 = whole picture')
    args = ap.parse_args()

    size = tuple(int(v) for v in args.size.lower().split('x'))
    scene = detail if args.detail else composition

    if args.all:
        os.makedirs(args.out, exist_ok=True)
        for wall, zoom, off, tag in SET:
            render(os.path.join(args.out, tag + '.mp4'), wall, size,
                   args.fps, args.seconds, args.zoom or zoom, off, scene)
        return
    render(args.out, args.wall, size, args.fps, args.seconds,
           args.zoom, (0.5, 0.5) if args.zoom else (0.34, 0.60), scene)


if __name__ == '__main__':
    main()
