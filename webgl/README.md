# winliqglass, WebGL2

An exact WebGL2 browser port of the renderer at the root of this repository
(`shaders.py` + `engine.py`). It keeps the original two pass model: content
glass composited into an offscreen mipmapped scene texture first, then UI glass
sampling that composed scene. DOM text and controls sit above the canvas, the
way the Python HUD does.

Refractive, not frosted. Blur stays low and the rim does the work: a hairline
highlight, a narrow bevel, a faint spectral fringe, and visible displacement
where something behind crosses the boundary.

## Files

- `winliqglass.js` a single ES module exporting the `WinLiqGlass` class. No
  dependencies.
- `winliqglass.d.ts` TypeScript types for the same.
- `demo.html` a self contained page: a slow animated background for the glass
  to bend, three glass panes, and cursor tracking.

## Run

Serve the folder over HTTP (ES modules do not load from `file://`) and open the
demo:

```
python -m http.server 8000
```

Then open `http://localhost:8000/demo.html`.

## API

```js
import { WinLiqGlass } from "./winliqglass.js";

const glass = new WinLiqGlass(canvas);   // throws if WebGL2 is unavailable
glass.resize(width, height);             // CSS pixels
glass.setBackground(image);              // any image, video frame or canvas
glass.setLayers(content, ui, top);       // arrays of shape records
glass.setTouch(x, y, amount);            // a ripple at the cursor
glass.render();                          // draw one frame
```

A shape record is a rounded rectangle in CSS pixels:

```js
{ x, y, w, h, rad, rot, kind, merge, rim, tint: [r, g, b, a] }
```

The content layer defaults that read as glass are `rim: -0.12` and
`tint: [1, 1, 1, 0.13]`. Intensity defaults to `0.10`; push it with
`setIntensity(value)`. The material dials (`frost`, `bend`, `merge`, `glass`)
are set with `setMaterial`, and `setOverrides` multiplies the ported constants
rather than replacing them, so `1.0` on every dial is the untouched look.

The renderer bends what is behind it, so it needs a moving background to read.
Over a still, quiet backdrop clear glass shows almost nothing, which is correct.
