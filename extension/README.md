# Liquid Glass for the web

A Chrome extension that finds every frosted `backdrop-filter` surface on a page,
switches the blur off, and draws the same box as refractive liquid glass.

## Load it

1. `chrome://extensions`
2. Turn on Developer mode
3. Load unpacked, and pick this folder
4. Reload any tab you want it on

`extension/test-page.html` has four frosted surfaces to try it against. Serve it
with `py demo/serve.py` and open `http://127.0.0.1:8765/extension/test-page.html`.

## How it works

The renderer needs a raster of whatever sits behind the glass. On a web page
that is the page itself, so the service worker calls `captureVisibleTab` while
the content script holds the glass elements invisible, and that capture becomes
the background texture. Between captures the texture is panned by the scroll
delta, which keeps the refraction attached to the content without asking for a
frame the quota will not give.

Each adopted element gets `backdrop-filter: none`, loses its translucent wash,
and is lifted above the canvas so its own text still paints on top of its own
glass. A third of the original background colour is carried into the tint, so a
site's colour survives without the pane going milky again.

## Dials

The toolbar popup writes to `chrome.storage.sync`, and the content script picks
up changes live.

| Dial | Default | What it does |
| --- | --- | --- |
| Replace frosted surfaces | on | Master switch. Off tears the renderer down and restores every element. |
| Body | `0.16` | Body opacity. |
| Refraction | `0.50` | How far the rim reaches for what it bends. |
| Light from | `0°` | Where the light comes from, clockwise from the top. |
| Opposite edge | `0.85` | How brightly the edge facing away answers. |

## Known limits

- Chrome's own pages and the Web Store cannot be captured, so the glass stays
  off there.
- A capture needs the glass elements hidden for a frame, so there is a brief
  flicker on load, on resize, and when a scroll settles.
- Adopted elements get a very high `z-index`, which a page with its own deep
  stacking tricks may not enjoy.
- `--load-extension` is restricted in current Google Chrome stable. Chrome for
  Testing or Chromium loads it without argument.
