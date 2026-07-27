# Public render-part generator

This folder provides a standalone path to generate reusable Liquid Glass render
parts without changing or removing the main app.

## Generate from width/height + radii

```bash
py public_render_part/generate_render_part.py \
  --name card-glass \
  --width 320 \
  --height 140 \
  --radii "24 24 36 36"
```

## Generate from SVG mask

```bash
py public_render_part/generate_render_part.py \
  --name hero-glass \
  --mask-svg shape.svg
```

Outputs are written to `public_render_part/output/` by default:

- `<name>.html`
- `<name>.css`
- `<name>.js`
