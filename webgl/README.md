# winliqglass, WebGL2

The renderer at the root of this repository (`shaders.py` + `engine.py`), running
in a browser. Same two passes, same material equations, same constants: content
glass composited into an offscreen mipmapped scene texture first, then UI glass
sampling that composed scene. Your text and controls sit above the canvas in the
DOM, the way the Python HUD does.

Refractive, not frosted. Blur stays at zero and the rim does the work: a
hairline on the edge facing the light and on the edge facing away, a narrow
bevel, a faint spectral fringe, and visible displacement where something behind
crosses the boundary.

One file, no dependencies.

## Install

```
npm install winliqglass
```

or copy `winliqglass.js` next to your page. It is an ES module, so it needs to
be served over HTTP; it will not load from `file://`.

## Quick start

```html
<canvas id="glass"></canvas>
<div class="card" data-glass>Anything you like</div>

<script type="module">
  import { createGlass } from "winliqglass";
  createGlass({ background: "/wallpaper.jpg" });
</script>
```

`createGlass` inserts the canvas, follows the device pixel ratio, watches for
resizes, runs the frame loop, tracks the pointer, uploads the background, and
each frame reads every element you marked as a shape. Mark an element
`data-glass` for the content layer or `data-glass-ui` for the layer above it.

The surfaces themselves must be transparent. The glass is drawn on the canvas
behind them, at exactly the box the browser laid out, radius included, so an
element that paints its own background will cover its own glass.

**The renderer bends what is behind it, so it needs something worth bending.**
Over a flat colour, clear glass shows almost nothing, and that is correct. Give
it a photograph, a gradient with structure, or video.

## createGlass(options)

Returns `{ glass, canvas, start, stop, setBackground, refresh, destroy }`.

| Option | Default | What it does |
| --- | --- | --- |
| `background` | none | Image URL, or any drawable source: `HTMLImageElement`, `HTMLVideoElement`, `HTMLCanvasElement`, `ImageBitmap`, `OffscreenCanvas`. A video sets `animated` for you. |
| `animated` | `false` | Re-upload the background every frame. Needed for video or a canvas you are drawing into. A still image is uploaded once. |
| `canvas` | created | Use your own canvas instead of inserting one. |
| `mount` | `document.body` | Host for a created canvas, and the element whose size the renderer follows. |
| `select` | `"[data-glass]"` | Content layer selector. |
| `selectUi` | `"[data-glass-ui]"` | UI layer selector, drawn over the composed content. |
| `selectTop` | none | Optional third layer over the UI layer. Costs one more pass. |
| `intensity` | `0.10` | Body opacity, 0 to 1. |
| `material` | see below | `{ frost, bend, merge, glass }`. |
| `highlight` | see below | `{ angle, bounce, sharpness, base, strength, specular, sharpen, sway }`. |
| `direction` | `1` | `1` refracts outward, `-1` inward. |
| `overrides` | none | Multiplies the ported constants. |
| `dpr` | `min(devicePixelRatio, 2.5)` | Backing store scale. |
| `pointer` | `true` | Track the pointer for the touch light. |
| `crossOrigin` | `"anonymous"` | Used when `background` is a URL. |
| `rim` | `-0.12` | Default rim for collected elements. |
| `tint` | `[1, 1, 1, 0.13]` | Default tint for collected elements. |
| `onFrame` | none | `(dt, glass) => {}`, called every frame. |

Per element, in markup:

| Attribute | What it does |
| --- | --- |
| `data-glass` | Collect into the content layer. |
| `data-glass-ui` | Collect into the UI layer. |
| `data-glass-rim="-0.12"` | Hairline override. Above 0 lights the edge, below 0 darkens it. |
| `data-glass-tint="1 1 1 0.13"` | Body tint, three or four numbers, 0 to 1. |
| `data-glass-merge` | Join this shape to its neighbours through the smooth minimum. |

## The dials

### setMaterial({ frost, bend, merge, glass })

| Dial | Range | Default | What it does |
| --- | --- | --- | --- |
| `frost` | 0 to 1 | `0` | Backdrop blur radius. Zero is the point of this material. Above about 0.2 it stops being glass and becomes the frosted look it exists to replace. |
| `bend` | 0 to 1 | `0.42` | Refraction depth. How far the rim reaches for what it bends. |
| `merge` | 0 to 1 | `0.34` | Smooth minimum radius between shapes marked `merge`, so two panes gel into one blob as they approach. |
| `glass` | 0 to 1 | `0.10` | Body opacity, same as `setIntensity`. |

### setHighlight({ ... })

The light, and how the rim answers it.

| Dial | Range | Default | What it does |
| --- | --- | --- | --- |
| `angle` | degrees | `0` | Where the light comes from. 0 is the top and it runs clockwise: 90 from the right, 180 from below, 270 from the left. |
| `bounce` | 0 to 1 | `0.85` | How brightly the edge facing away answers. A pane in a lit room is lit by a source and again by what that source bounces off, so both the near and far edges carry a line while the two sides between them go dark. `0` leaves a single lobe, which reads as a sticker with one bright corner. |
| `sharpness` | > 0 | `1.0` | Exponent on each lobe. Above 1 the lines shorten and the dark sides grow; below 1 the light wraps further round. |
| `base` | 0 to 1 | `0.20` | Floor under both lobes, so the silhouette never fully breaks. |
| `strength` | ≥ 0 | `1` | Multiplies the hairline. |
| `specular` | ≥ 0 | `1` | Multiplies the reflection off the bevel. |
| `sharpen` | > 0 | `1` | Scales the specular exponent. Below 1 widens the lobe. Raise `specular` and lower `sharpen` together to make the edge read over bright or busy content. |
| `sway` | radians | `0` | Slow drift around the angle. `0` holds it still. |

### setDirection(value)

`1` samples outward along the surface normal, the convex bevel: the rim pulls in
what sits just outside the shape and compresses it. `-1` runs the same profile
inward, so content crossing the edge folds rather than easing through and the
corners close into caustic lobes. Values between scale the depth with it.

### setOverrides({ opacity, bend, edge, spec, shine, rim, disp })

Multiplies the ported constants rather than replacing them, so `1.0` on every
dial is the untouched look and you can nudge one term without re-deriving the
material. `edge` is the width of the lens band in pixels, `disp` the chromatic
dispersion, `shine` the specular exponent.

### setTouch(x, y, amount)

Lights the glass from a point, in CSS pixels. `createGlass` wires this to the
pointer; drive it yourself for a press.

## Driving it by hand

Skip `createGlass` when you own the loop. `demo/drag.html` and `demo/video.html`
both do this.

```js
import { WinLiqGlass } from "winliqglass";

const glass = new WinLiqGlass(canvas);   // throws if WebGL2 is unavailable
glass.resize(innerWidth, innerHeight, devicePixelRatio);
glass.setBackground(image);
glass.setLayers(content, ui, top);
glass.render();
```

`resize` takes CSS pixels, which is also the space shape records are given in.
The backing store is that size times `dpr` and `uRes` stays in CSS pixels, so
every pixel denominated dial keeps its size on screen while the field is sampled
at device resolution.

A shape record:

```js
{
  x, y, w, h,     // CSS pixels, top left origin
  rad,            // corner radius, default 22% of the short side
  rot,            // radians
  kind,           // 0 circle, 1 rounded rect, 2 triangle, 3 ring, 4 pentagon
  merge,          // 1 joins this shape to its neighbours
  rim,            // hairline override
  tint,           // [r, g, b, a], a = 1 is a solid painted glyph
}
```

`shapeFromElement(el)` builds one from a laid out element if you want the DOM
reading without the rest of the helper.

Up to 48 shapes per layer.

## Layers

Three at most, and each one costs a pass.

- **content** is drawn over the background into the scene texture.
- **ui** samples that composed scene, so a UI pane refracts the content glass
  underneath it rather than sitting flat on top of it.
- **top** samples the UI result the same way.

This is what makes a glyph inside a glass button read as glass inside glass: put
the disc on the content layer and the bars on the UI layer, and the bars refract
the disc's own refraction. `demo/video.html` is that exactly.

## Performance

- A still background is uploaded once. `animated` re-uploads every frame, which
  means a `texImage2D` plus a full mipmap chain per frame and is the most
  expensive thing in the loop.
- For video, upload only when the video produces a frame. Pushing a 24fps clip
  at display rate does the same frame two or three times for nothing:

  ```js
  let fresh = false;
  const tick = () => { fresh = true; video.requestVideoFrameCallback(tick); };
  video.requestVideoFrameCallback(tick);
  ```

- `dpr` is capped at 2.5 by default. A 3x phone rendering a full screen field is
  the one thing likely to cost you frames.
- `glass.debug = true` publishes shape counts onto the canvas dataset. It is off
  by default because `setLayers` runs every frame and a dataset write is a style
  invalidation each time.
- `dispose()` releases every GL object. A component that mounts more than once
  needs it, or each mount leaks two screen sized textures and their framebuffers.
  `createGlass().destroy()` does this plus the listeners.

## Support

WebGL2, which is everywhere current. `isSupported()` answers before you build
anything. The constructor throws if the context cannot be created, so a plain
try/catch around it is a fine fallback path.

## Demos

Serve the repository root and open them. `demo/serve.py` answers Range requests,
which Safari needs before it will play or seek the video demo.

```
py demo/serve.py
```

- `demo/drag.html` one wallpaper, one draggable pane, hand driven loop
- `demo/site.html` a page of glass surfaces with live light dials, via `createGlass`
- `demo/video.html` glass over moving video, a transport button and a dock,
  with the highlight driven by device roll and tilt

`presets.md` at the repository root lists the exact settings each demo uses.
