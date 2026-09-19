# Presets

Every setting the three demos actually use, and how they were served. Dial
meanings are in `webgl/README.md`; this file is the values.

Anything not listed is the library default: `intensity 0.10`, `frost 0`,
`bend 0.42`, `merge 0.34`, `direction 1`, and the highlight at `angle 0`,
`bounce 0.85`, `sharpness 1.0`, `base 0.20`, `strength 1`, `specular 1`,
`sharpen 1`, `sway 0`.

---

## demo/drag.html — one wallpaper, one draggable pane

Hand driven loop, no `createGlass`. Everything is the library default except the
refraction direction.

```js
glass.setDirection(-1);
glass.resize(innerWidth, innerHeight, Math.min(devicePixelRatio || 1, 2.5));
```

| Setting | Value |
| --- | --- |
| background | `demo/bg-gg.jpg`, uploaded once |
| direction | `-1`, inward |
| intensity | `0.10` |
| pane | `rim -0.12`, `tint [1, 1, 1, 0.13]` |
| pane size | `min(340, viewport - 44)` wide, `0.59` of that tall |
| pane radius | `0.17 × min(w, h)` |

Motion, all of it curved:

| Term | Value | What it is |
| --- | --- | --- |
| rubber band | `0.42` | fraction of the finger the pane keeps past the edge |
| follow | `1 - e^(-dt × 34)` | tracking the pointer, so a coarse pointer stream does not stutter the refraction |
| spring | `K = 210`, `C = 2√K × 1.04` | critically damped return at the bounds |
| friction | `0.0018^dt` | exponential glide after a flick |
| pickup | target `1.035`, `K = 280`, damping `0.9` | springs larger on grab, overshoots, settles |
| touch light | rise `13`, fall `5` | exponential, per second |

---

## demo/site.html — a page of surfaces, with live light dials

Uses `createGlass`, so the canvas, DPR, resize, frame loop and pointer are all
handled by the helper, and the shapes are read off the marked elements.

```js
createGlass({
  background: "./bg-gg.jpg",
  intensity: 0.13,
  material: { bend: 0.5 },
  highlight: { angle: 0, bounce: 0.85 },
});
```

| Surface | Layer | Settings |
| --- | --- | --- |
| nav bar | `data-glass-ui` | `data-glass-rim="-0.10"`, radius 24px |
| cards | `data-glass` | defaults: `rim -0.12`, `tint [1, 1, 1, 0.13]`, radius 30px |
| control pill | `data-glass-ui` | `data-glass-rim="-0.10"`, radius 26px |

The two sliders write straight into `setHighlight({ angle, bounce })`. Setting
**Opposite edge** to 0 collapses the pair of hairlines back to a single lobe,
which is the clearest way to see what `bounce` does.

---

## demo/video.html — glass over moving video

```js
glass.setDirection(-1);
glass.setIntensity(0.14);
glass.setMaterial({ bend: 0.52 });
glass.setHighlight({
  angle: 0,
  bounce: 0.85,
  strength: 2.3,
  specular: 2.6,
  sharpen: 0.42,
  base: 0.14,
});
```

The highlight is pushed hard here on purpose. The ported constants are tuned
against a calm wallpaper: the specular exponent is 22, which puts the whole
reflection inside about two pixels, and the shader pulls highlights down over
bright content by design. Over a lit, moving frame that leaves nothing to see.
`sharpen 0.42` widens the lobe and `specular 2.6` gives it something to show.

### Shapes

| Shape | Layer | kind | Settings |
| --- | --- | --- | --- |
| disc | content | 0 | `d = clamp(0.19 × min(vw, vh), 104, 168)`, `rim -0.12`, `tint [1, 1, 1, 0.12]` |
| pause bars | ui | 1 | `w = 0.15d`, `h = 0.48d`, gap `0.13d`, radius `w/2`, `rim -0.10`, `tint [1, 1, 1, 1]` |
| dock plate | content | 1 | radius `0.42 × height`, `rim -0.12`, `tint [1, 1, 1, 0.10]` |
| dock tiles | ui | 1 | `tile = clamp(0.11 × vw, 38, 54)`, gap `0.46 tile`, pad `0.38 tile`, radius `0.28 tile`, `rim -0.10`, opaque tints |

Bars on the UI layer and the disc on the content layer is the whole trick: the
bars refract the disc's own refraction instead of sitting flat on it.

### Shimmer, from device orientation

| Term | Value |
| --- | --- |
| angle from roll | `-gamma × 1.7` |
| tilt window | `clamp(beta - 45, -55, 55)` |
| hairline from tilt | `1 + |tilt| / 55 × 0.85`, then `× 2.3` |
| specular from tilt | `1 + |tilt| / 55 × 0.90`, then `× 2.6` |
| smoothing | `1 - e^(-dt × 6.5)` |

Roll turns the highlight the opposite way, so the light stays put in the room
while the phone moves under it. Tilt sets how glancing that light is. The
orientation stream arrives in noisy steps, so both are chased exponentially or
it stutters instead of sweeping.

iOS only hands out motion after `DeviceOrientationEvent.requestPermission()`,
which must be called from a gesture, so the first tap raises the prompt. Without
it, or on desktop, the pointer drives the same light:
`angle = atan2(dx, -dy)` in degrees from the middle of the screen.

### Background upload

The texture goes up only when the video produces a frame:

```js
let fresh = false;
const tick = () => { fresh = true; video.requestVideoFrameCallback(tick); };
video.requestVideoFrameCallback(tick);
```

At display rate a 24fps clip would be pushed through `texImage2D` and a full
mipmap chain two or three times per frame for nothing.

### The clip

`demo/action.mp4` is 32 seconds from **Tears of Steel**, starting at 07:04, the
robot battle. Cut from the 720p master with:

```
ffmpeg -ss 424 -i tears_of_steel_720p.mov -t 32 -an \
       -vf "scale=1280:-2" -c:v libx264 -preset slow -crf 23 \
       -profile:v high -level 4.0 -pix_fmt yuv420p -movflags +faststart action.mp4
```

Audio is dropped because autoplay requires muted anyway, and `+faststart` puts
the moov atom first so it begins playing before the whole file arrives.

(CC) Blender Foundation, mango.blender.org, CC BY 3.0.

---

## The server

`demo/serve.py`, serving the repository root so `/demo/*.html` can reach
`/webgl/winliqglass.js`.

```
py demo/serve.py 8765
```

| Setting | Value | Why |
| --- | --- | --- |
| bind | `127.0.0.1` | local only; the tunnel is what exposes it |
| port | `8765` | |
| root | repository root | the demos import one directory up |
| server | `ThreadingHTTPServer` | a phone opens the page, the module and the video at once |
| `Accept-Ranges: bytes` | always | |
| range replies | `206` with `Content-Range` | **Safari will not start or seek a video without this.** Python's stock `http.server` answers 200 with the whole file, and `video.html` stays black on an iPhone |
| `Cache-Control: no-cache` | always | the demos get edited while they are being watched |

Public URL, for testing on a phone:

```
cloudflared tunnel --url http://127.0.0.1:8765
```

Quick tunnels get one edge connection and expire on their own, so expect to
restart it and hand out a new hostname. Do not put a restart watchdog on it:
tunnel creation rate limits after about nine, and the error it returns on the
way is misreported as a response parsing failure.

---

## Wallpaper

`demo/bg-gg.jpg` is `golden-gate.png` from the repository root, resized to 2048
on the long edge at JPEG quality 82, progressive. The full size original is
6016 × 4147 and 23 MB, which is not something to send down a tunnel to a phone.
