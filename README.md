# winliqglass

Liquid Glass (iOS 26-style) on Windows. OpenGL app that treats shapes as lenses: SDFs join with a smooth-min field, then bend the background at the rim with frost and a bit of chromatic spread. Two render passes so knobs and toolbar glass sample the scene underneath them, not a flat overlay.

Needs a GPU that can run ModernGL + GLFW.

## Run

```
py -m pip install -r requirements.txt
py app.py
py app.py photo.jpg
py app.py --shot out.png
```

`wallpaper1.png` through `wallpaper4.jpg` are fine as test backgrounds.

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

Draw tools place on click. Wheel changes corner roundness, Shift+wheel resizes, `Q`/`E` rotate, drag moves, `Del` deletes.

Switches use a short track; press the knob and it grows into glass, then drag or tap to toggle. Sliders keep a thin blue/grey track and a tall glass knob that refracts the track through itself. The `...` menu morphs open for shape picks; Shift+drag moves the menu.

Bottom-right sliders: Glass, Frost, Bend, Merge. The circular-arrow button resets settings and the scene.

## Other input

| Input | Action |
| --- | --- |
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
| `hud.py` | Pillow icons and labels |
| `scene.json` | last saved layout |
