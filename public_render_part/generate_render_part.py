#!/usr/bin/env python3
"""Generate standalone Liquid Glass render-part assets (HTML/CSS/JS)."""

import argparse
import json
import os
import re
import xml.etree.ElementTree as ET
from urllib.parse import quote


def _slug(text):
    s = re.sub(r'[^a-zA-Z0-9_-]+', '-', text.strip()).strip('-').lower()
    return s or 'render-part'


def _parse_num(v):
    if v is None:
        return None
    s = str(v).strip().lower()
    for suf in ('px', 'pt'):
        if s.endswith(suf):
            s = s[:-len(suf)].strip()
    return float(s)


def _read_svg_size(svg_path):
    tree = ET.parse(svg_path)
    root = tree.getroot()
    vb = root.get('viewBox') or root.get('viewbox')
    if vb:
        parts = vb.replace(',', ' ').split()
        if len(parts) == 4:
            return abs(float(parts[2])), abs(float(parts[3]))
    w = _parse_num(root.get('width'))
    h = _parse_num(root.get('height'))
    if w and h:
        return w, h
    raise ValueError('SVG must define viewBox or width/height, or pass --width/--height.')


def _parse_radii(raw):
    vals = [float(t) for t in str(raw).replace(',', ' ').split() if t.strip()]
    if not vals:
        raise ValueError('Provide at least one radius value.')
    if any(v < 0 for v in vals):
        raise ValueError('Radius values must be non-negative.')
    return vals


def _radii_css(values):
    return ' '.join(f'{round(v, 3):g}px' for v in values)


def _svg_data_uri(svg_path):
    with open(svg_path, 'r', encoding='utf-8') as f:
        src = f.read()
    return 'data:image/svg+xml;utf8,' + quote(src, safe='/():;,+-*=\'"._~!$&[]@')


def _build_assets(name, width, height, radii, mask_svg):
    cls = _slug(name)
    radius_css = _radii_css(radii or [20.0])
    mask_uri = _svg_data_uri(mask_svg) if mask_svg else None

    html = (
        '<!-- Liquid Glass render part -->\n'
        f'<div class="lgp {cls}">\n'
        '  <div class="lgp-glass"></div>\n'
        '</div>\n'
    )

    css = [
        '/* Standalone Liquid Glass render part */',
        '.lgp {',
        '  position: relative;',
        f'  width: {round(width, 3):g}px;',
        f'  height: {round(height, 3):g}px;',
        '}',
        '',
        '.lgp-glass {',
        '  position: absolute;',
        '  inset: 0;',
        '  background: rgba(255, 255, 255, 0.07);',
        '  -webkit-backdrop-filter: blur(2px) saturate(128%);',
        '  backdrop-filter: blur(2px) saturate(128%);',
        '  box-shadow: inset 0 1px 0 rgba(255,255,255,0.72),',
        '              inset 0 -1px 0 rgba(255,255,255,0.20),',
        '              inset 0 0 10px rgba(255,255,255,0.05);',
        '  isolation: isolate;',
    ]

    if mask_uri:
        css += [
            '  border-radius: 0;',
            f'  -webkit-mask-image: url("{mask_uri}");',
            '  -webkit-mask-size: 100% 100%;',
            '  -webkit-mask-repeat: no-repeat;',
            '  -webkit-mask-position: center;',
            f'  mask-image: url("{mask_uri}");',
            '  mask-size: 100% 100%;',
            '  mask-repeat: no-repeat;',
            '  mask-position: center;',
        ]
    else:
        css.append(f'  border-radius: {radius_css};')

    css += [
        '}',
        '',
        '.lgp-glass::before {',
        '  content: "";',
        '  position: absolute;',
        '  inset: 0;',
        '  border-radius: inherit;',
        '  background: linear-gradient(145deg,',
        '      rgba(255,255,255,0.30) 0%,',
        '      rgba(255,255,255,0.00) 18%,',
        '      rgba(255,255,255,0.00) 82%,',
        '      rgba(255,255,255,0.14) 100%);',
        '  mix-blend-mode: screen;',
        '  pointer-events: none;',
        '}',
        '',
        '.lgp-glass::after {',
        '  content: "";',
        '  position: absolute;',
        '  inset: 0;',
        '  border-radius: inherit;',
        '  padding: 1.5px;',
        '  background: conic-gradient(from 210deg,',
        '      rgba(255,138,76,0.85), rgba(150,255,190,0.65),',
        '      rgba(96,186,255,0.85), rgba(214,132,255,0.75),',
        '      rgba(255,138,76,0.85));',
        '  -webkit-mask: linear-gradient(#000 0 0) content-box, linear-gradient(#000 0 0);',
        '  -webkit-mask-composite: xor;',
        '  mask: linear-gradient(#000 0 0) content-box, linear-gradient(#000 0 0);',
        '  mask-composite: exclude;',
        '  mix-blend-mode: screen;',
        '  opacity: 0.28;',
        '  pointer-events: none;',
        '}',
    ]

    js = (
        '(function (global) {\n'
        '  function createLiquidRenderPart(target, opts) {\n'
        '    opts = opts || {};\n'
        '    var host = (typeof target === "string") ? document.querySelector(target) : target;\n'
        '    if (!host) throw new Error("Target not found");\n'
        f'    var root = document.createElement("div");\n'
        f'    root.className = "lgp " + (opts.className || "{cls}");\n'
        '    root.innerHTML = \'<div class="lgp-glass"></div>\';\n'
        '    host.appendChild(root);\n'
        '    return root;\n'
        '  }\n'
        '  global.createLiquidRenderPart = createLiquidRenderPart;\n'
        '})(window);\n'
    )

    return html, '\n'.join(css).rstrip() + '\n', js


def generate(name, out_dir, width=None, height=None, radii=None, mask_svg=None):
    if mask_svg and (width is None or height is None):
        width, height = _read_svg_size(mask_svg)
    if width is None or height is None:
        raise ValueError('Width and height are required unless SVG mask provides size.')
    if width <= 0 or height <= 0:
        raise ValueError('Width and height must be positive.')

    html, css, js = _build_assets(name, width, height, radii, mask_svg)
    os.makedirs(out_dir, exist_ok=True)
    stem = _slug(name)
    paths = {
        'html': os.path.join(out_dir, f'{stem}.html'),
        'css': os.path.join(out_dir, f'{stem}.css'),
        'js': os.path.join(out_dir, f'{stem}.js'),
    }
    with open(paths['html'], 'w', encoding='utf-8') as f:
        f.write(html)
    with open(paths['css'], 'w', encoding='utf-8') as f:
        f.write(css)
    with open(paths['js'], 'w', encoding='utf-8') as f:
        f.write(js)
    return paths


def main():
    p = argparse.ArgumentParser(description='Generate standalone Liquid Glass render-part files.')
    p.add_argument('--name', default='render-part')
    p.add_argument('--out-dir', default='public_render_part/output')
    p.add_argument('--width', type=float)
    p.add_argument('--height', type=float)
    p.add_argument('--radii', default='20', help='Space/comma-separated list of radii.')
    p.add_argument('--mask-svg', help='Use SVG mask path instead of border-radius geometry.')
    args = p.parse_args()

    radii = None if args.mask_svg else _parse_radii(args.radii)
    paths = generate(
        name=args.name,
        out_dir=args.out_dir,
        width=args.width,
        height=args.height,
        radii=radii,
        mask_svg=args.mask_svg,
    )
    print(json.dumps(paths, indent=2))


if __name__ == '__main__':
    main()
