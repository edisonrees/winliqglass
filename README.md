# winliqglass

Liquid Glass (iOS 26 / macOS 26-style) on Windows. OpenGL app that treats shapes
as lenses: SDFs join with a smooth-min field, then bend the background at the rim
with spectral dispersion, specular highlights and an adaptive body tint. Two
render passes, so knobs and toolbar glass sample the scene underneath them
rather than sitting on a flat overlay.

Ships the macOS 27 **Golden Gate** desktop picture as the default wallpaper —
its broad ribbons and hairline caustics are what the lens has to chew on, so it
doubles as the reference surface for tuning refraction.

Needs a GPU that can run ModernGL + GLFW.

## Run

```
py -m pip install -r requirements.txt
py app.py                       # Golden Gate wallpaper
py app.py photo.jpg             # your own background
py app.py --shot out.png
py record.py demo.mp4           # scripted demo, straight to MP4
py record.py demo.mp4 --dark    # dark Golden Gate
```

## The material

What the fragment shader does per pixel, and why:

| Effect | How |
| --- | --- |
| Lensing | Circular thickness profile over the SDF band; the rim bends light outward hard enough that distant background folds into visible compression rings. |
| Spectral edge | Eight wavelength taps recombined through a narrow RGB response. The chromatic part of the residual is amplified on its own, so the fringe saturates without putting luminance ringing on high-contrast edges. |
| Specular | Key light above-left, fill below-right, gated to the bevel — a flat top facing the viewer would otherwise wash the whole interior with constant sheen. |
| Adaptive tint | A wide mip of the wall behind sets the body polarity: near-black over dark content, near-white over light, so overlaid glyphs keep contrast on any wallpaper. |
| Adaptive shadow | Wide, soft, offset down, and scaled by background luminance — clear over light content, nearly gone over dark. |
| Fresnel + hairline | Edge reflectivity rises at grazing angles; a bright hairline traces the silhouette, brightest where it faces the key light. |
| Touch light | Pressing illuminates the glass from the pointer outward. |

Chrome is drawn white into one layer with a single soft drop shadow beneath —
flipping ink colour per element cannot survive a gradient crossing it.

## Tools

| Key | Tool |
| --- | --- |
| `V` | select |
| `C` | circle |
| `R` | rounded rect |
| `P` | pill |
| `T` | triangle |
| `S` | switch |
| `L` | slider |

Draw tools place on click. Wheel changes corner roundness, Shift+wheel resizes,
`Q`/`E` rotate, drag moves, `Del` deletes.

Switches use iOS's 51x31 track ratio; press the knob and it grows into glass,
then drag or tap to toggle. Sliders keep a thin blue/grey track and a tall glass
knob that refracts the track through itself. The `...` menu morphs open for
shape picks; Shift+drag moves the menu.

Bottom-right sliders: Glass, Frost, Bend, Merge. The circular-arrow button resets
settings and the scene.

## HTMLify

Right-click any element for **Export element as HTML**, **Export element as CSS**,
or **Export whole scene as HTML**. Files land in `export/`.

The translation is deliberate rather than mechanical, because a browser cannot
run the fragment shader:

* body — `backdrop-filter: blur() saturate()`, the one part browsers implement
  natively;
* lensing — an SVG `feDisplacementMap` fed by a displacement map baked from the
  same SDF the shader uses. It is emitted per element and wired up, but
  `backdrop-filter: url(#id)` is Chromium-only today, so the export degrades to
  the blur alone elsewhere;
* fringe — a masked conic-gradient ring standing in for spectral separation;
* specular — a 145° gradient sweep in `screen`, matching the shader's key/fill
  pair;
* shadow — a plain drop `box-shadow`.

Clip-path shapes (triangle, pentagon) export as flat translucent fills:
`backdrop-filter` does not clip reliably to `clip-path`.

`scene.html` references the wallpaper next to the sources, so open it from
`export/` and the picture resolves.

## Other input

| Input | Action |
| --- | --- |
| right-click | HTMLify menu |
| `O` or drop a file | background image |
| `Ctrl+S` / `Ctrl+L` | save / load `scene.json` |
| trash icon | delete selection |
| `Esc` | deselect |

## Code

| File | What it does |
| --- | --- |
| `app.py` | window, widgets, input, headless shot |
| `engine.py` | shape model and two-pass renderer |
| `shaders.py` | GLSL for fields and lens shading |
| `hud.py` | Pillow icons, labels, cursor |
| `htmlify.py` | HTML/CSS export |
| `record.py` | scripted demo recorder to MP4 |
| `scene.json` | last saved layout |

## Wallpapers

`golden-gate.png` and `golden-gate-dark.png` are Apple's macOS 27 desktop
pictures, used here as test backgrounds. `wallpaper2.png` through
`wallpaper4.jpg` are alternates.
