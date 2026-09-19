// Captures the visible tab on request. The content script cannot do this
// itself, and this is the only way to get a raster of the page that sits
// behind the glass without re-implementing the browser's own painter.
//
// captureVisibleTab is quota limited to roughly two calls a second. Exceeding
// it throws rather than queueing, so requests are spaced here and a rejected
// capture leaves the previous frame in place instead of tearing.

const MIN_GAP_MS = 420;
let lastCapture = 0;
let inFlight = null;

async function capture(windowId) {
  const wait = Math.max(0, MIN_GAP_MS - (Date.now() - lastCapture));
  if (wait > 0) await new Promise((r) => setTimeout(r, wait));
  lastCapture = Date.now();
  return chrome.tabs.captureVisibleTab(windowId, { format: "jpeg", quality: 72 });
}

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message?.type !== "glass:capture") return false;
  const windowId = sender.tab?.windowId;
  if (windowId === undefined) {
    sendResponse({ ok: false, error: "no window" });
    return false;
  }
  // One capture at a time: parallel calls burn the quota and return the same
  // frame anyway.
  inFlight = (inFlight ?? Promise.resolve())
    .catch(() => {})
    .then(() => capture(windowId))
    .then((dataUrl) => sendResponse({ ok: true, dataUrl }))
    .catch((error) => sendResponse({ ok: false, error: String(error?.message ?? error) }));
  return true;
});

chrome.runtime.onInstalled.addListener(() => {
  chrome.storage.sync.get(null, (current) => {
    chrome.storage.sync.set({
      enabled: current.enabled ?? true,
      intensity: current.intensity ?? 0.16,
      bend: current.bend ?? 0.5,
      angle: current.angle ?? 0,
      bounce: current.bounce ?? 0.85,
    });
  });
});
