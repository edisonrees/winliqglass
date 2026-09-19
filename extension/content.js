/**
 * Finds every frosted surface on the page, switches its backdrop-filter off,
 * and draws the same box as refractive liquid glass instead.
 *
 * The renderer needs a raster of what sits behind the glass. On a web page
 * that is the page itself, so the service worker captures the visible tab with
 * the glass elements held invisible, and that capture becomes the background
 * texture. Between captures the texture is panned by the scroll delta, which
 * keeps the refraction tracking without asking for a frame the quota will not
 * give.
 */

const CANVAS_Z = 2147483000;
const RESCAN_MS = 900;
const SCROLL_SETTLE_MS = 220;

const state = {
  settings: { enabled: true, intensity: 0.16, bend: 0.5, angle: 0, bounce: 0.85 },
  glass: null,
  canvas: null,
  targets: new Map(),
  capture: null,
  captureScroll: [0, 0],
  compose: null,
  composeCtx: null,
  bgDirty: false,
  pending: false,
  running: false,
  raf: 0,
  scrollTimer: 0,
  pageColor: "#111318",
};

function parseColor(value) {
  const m = /rgba?\(([^)]+)\)/.exec(value || "");
  if (!m) return null;
  const parts = m[1].split(/[\s,\/]+/).map(parseFloat).filter((n) => Number.isFinite(n));
  if (parts.length < 3) return null;
  return [parts[0] / 255, parts[1] / 255, parts[2] / 255, parts.length > 3 ? parts[3] : 1];
}

/** A frosted surface is any element the page asked to blur its backdrop. */
function isGlass(el) {
  const s = getComputedStyle(el);
  const filter = s.backdropFilter || s.webkitBackdropFilter || "none";
  if (filter === "none" || !filter.includes("blur")) return false;
  const rect = el.getBoundingClientRect();
  return rect.width >= 24 && rect.height >= 16;
}

function radiusOf(style, rect) {
  const r = parseFloat(style.borderTopLeftRadius) || 0;
  return Math.min(r, Math.min(rect.width, rect.height) / 2);
}

function adopt(el) {
  if (state.targets.has(el)) return;
  const style = getComputedStyle(el);
  const background = parseColor(style.backgroundColor);
  state.targets.set(el, {
    background,
    prevPosition: el.style.position,
    prevZ: el.style.zIndex,
  });
  // The frost goes, the element's own wash goes with it, and the box is lifted
  // over the canvas so its text still paints on top of its own glass.
  el.style.setProperty("backdrop-filter", "none", "important");
  el.style.setProperty("-webkit-backdrop-filter", "none", "important");
  if (background && background[3] < 0.985) {
    el.style.setProperty("background-color", "transparent", "important");
  }
  if (style.position === "static") el.style.setProperty("position", "relative");
  el.style.setProperty("z-index", String(CANVAS_Z + 1));
  el.dataset.liquidGlass = "";
}

function release(el) {
  const saved = state.targets.get(el);
  if (!saved) return;
  el.style.removeProperty("backdrop-filter");
  el.style.removeProperty("-webkit-backdrop-filter");
  el.style.removeProperty("background-color");
  el.style.position = saved.prevPosition;
  el.style.zIndex = saved.prevZ;
  delete el.dataset.liquidGlass;
  state.targets.delete(el);
}

function scan() {
  const seen = new Set();
  for (const el of document.body.querySelectorAll("*")) {
    if (state.targets.has(el) || isGlass(el)) {
      seen.add(el);
      adopt(el);
    }
  }
  for (const el of [...state.targets.keys()]) {
    if (!el.isConnected) release(el);
  }
  return seen.size;
}

function shapes() {
  const out = [];
  for (const [el, saved] of state.targets) {
    const rect = el.getBoundingClientRect();
    if (rect.width < 8 || rect.height < 8) continue;
    if (rect.bottom < 0 || rect.top > innerHeight) continue;
    const style = getComputedStyle(el);
    if (style.display === "none" || style.visibility === "hidden") continue;
    const c = saved.background;
    // Keep a trace of the page's own wash so a site's colour survives, at a
    // third of its weight: any more and the pane goes milky again, which is
    // the look this replaces.
    const tint = c && c[3] > 0.02 ? [c[0], c[1], c[2], Math.min(c[3] * 0.34, 0.22)] : [1, 1, 1, 0.1];
    out.push({
      x: rect.left,
      y: rect.top,
      w: rect.width,
      h: rect.height,
      rad: radiusOf(style, rect),
      rot: 0,
      kind: 1,
      merge: 0,
      rim: -0.12,
      tint,
    });
  }
  return out;
}

// ---- background capture ---------------------------------------------------

function hideTargets(hidden) {
  for (const el of state.targets.keys()) {
    if (hidden) el.style.setProperty("visibility", "hidden", "important");
    else el.style.removeProperty("visibility");
  }
  if (state.canvas) state.canvas.style.visibility = hidden ? "hidden" : "";
}

const twoFrames = () =>
  new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));

async function requestCapture() {
  if (state.pending || !state.targets.size) return;
  state.pending = true;
  hideTargets(true);
  // Hiding is a style change, and the capture reads the compositor. Without
  // waiting for the frame that applies it, the shot can still contain the
  // glass and the text, and both then get refracted into the next one.
  await twoFrames();
  try {
    const reply = await chrome.runtime.sendMessage({ type: "glass:capture" });
    if (reply?.ok && reply.dataUrl) {
      const image = new Image();
      await new Promise((resolve, reject) => {
        image.onload = resolve;
        image.onerror = reject;
        image.src = reply.dataUrl;
      });
      state.capture = image;
      state.captureScroll = [scrollX, scrollY];
      state.bgDirty = true;
    }
  } catch {
    // A refused capture just leaves the previous texture up.
  } finally {
    hideTargets(false);
    state.pending = false;
  }
}

/**
 * The capture is one viewport of the page at the scroll position it was taken
 * from. Panning it by the delta keeps the refraction attached to the content
 * while the page moves; the strip that scrolls in is unknown until the next
 * capture, so it is filled with the page's own background colour.
 */
function composeBackground() {
  const image = state.capture;
  if (!image) return null;
  const w = innerWidth;
  const h = innerHeight;
  if (!state.compose) {
    state.compose = document.createElement("canvas");
    state.composeCtx = state.compose.getContext("2d", { alpha: false });
  }
  if (state.compose.width !== w || state.compose.height !== h) {
    state.compose.width = w;
    state.compose.height = h;
  }
  const ctx = state.composeCtx;
  const dx = state.captureScroll[0] - scrollX;
  const dy = state.captureScroll[1] - scrollY;
  ctx.fillStyle = state.pageColor;
  ctx.fillRect(0, 0, w, h);
  ctx.drawImage(image, 0, 0, image.width, image.height, dx, dy, w, h);
  return state.compose;
}

// ---- renderer -------------------------------------------------------------

async function boot() {
  const module = await import(chrome.runtime.getURL("winliqglass.js"));
  const canvas = document.createElement("canvas");
  canvas.id = "liquid-glass-surface";
  canvas.style.cssText = [
    "position:fixed",
    "left:0",
    "top:0",
    "width:100%",
    "height:100%",
    "pointer-events:none",
    "border:0",
    "margin:0",
    "padding:0",
    `z-index:${CANVAS_Z}`,
  ].join(";");
  document.documentElement.appendChild(canvas);

  let glass;
  try {
    glass = new module.WinLiqGlass(canvas);
  } catch {
    canvas.remove();
    return;
  }
  state.canvas = canvas;
  state.glass = glass;

  const bodyColor = parseColor(getComputedStyle(document.body).backgroundColor);
  if (bodyColor && bodyColor[3] > 0.5) {
    state.pageColor = `rgb(${bodyColor.slice(0, 3).map((v) => Math.round(v * 255)).join(",")})`;
  }

  applySettings();
  resize();
  addEventListener("resize", () => {
    resize();
    requestCapture();
  }, { passive: true });
  addEventListener("scroll", onScroll, { passive: true });
  addEventListener("pointermove", (e) => glass.setTouch(e.clientX, e.clientY, 0.35), { passive: true });

  const observer = new MutationObserver(() => {
    clearTimeout(state.scrollTimer);
    state.scrollTimer = setTimeout(() => {
      if (scan()) requestCapture();
    }, RESCAN_MS);
  });
  observer.observe(document.body, { childList: true, subtree: true, attributes: true,
                                    attributeFilter: ["class", "style"] });

  scan();
  await requestCapture();
  start();
}

function resize() {
  if (!state.glass) return;
  state.glass.resize(innerWidth, innerHeight, Math.min(devicePixelRatio || 1, 2));
  state.bgDirty = true;
}

function onScroll() {
  state.bgDirty = true;
  clearTimeout(state.scrollTimer);
  state.scrollTimer = setTimeout(requestCapture, SCROLL_SETTLE_MS);
}

function applySettings() {
  const s = state.settings;
  if (!state.glass) return;
  state.glass.setIntensity(s.intensity);
  state.glass.setMaterial({ bend: s.bend });
  state.glass.setHighlight({ angle: s.angle, bounce: s.bounce });
}

function frame() {
  const glass = state.glass;
  if (glass && state.capture) {
    if (state.bgDirty) {
      const bg = composeBackground();
      if (bg) glass.setBackground(bg);
      state.bgDirty = false;
    }
    glass.setLayers(shapes(), []);
    glass.render();
  }
  state.raf = requestAnimationFrame(frame);
}

function start() {
  if (state.running) return;
  state.running = true;
  state.raf = requestAnimationFrame(frame);
}

function stop() {
  state.running = false;
  cancelAnimationFrame(state.raf);
}

function teardown() {
  stop();
  for (const el of [...state.targets.keys()]) release(el);
  if (state.glass) state.glass.dispose();
  if (state.canvas) state.canvas.remove();
  state.glass = null;
  state.canvas = null;
  state.capture = null;
}

chrome.storage.sync.get(null, (stored) => {
  Object.assign(state.settings, stored || {});
  if (state.settings.enabled !== false) boot();
});

chrome.storage.onChanged.addListener((changes) => {
  for (const [key, change] of Object.entries(changes)) {
    state.settings[key] = change.newValue;
  }
  if (state.settings.enabled === false) {
    teardown();
    return;
  }
  if (!state.glass) {
    boot();
    return;
  }
  applySettings();
});
