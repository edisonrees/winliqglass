const DEFAULTS = { enabled: true, intensity: 0.16, bend: 0.5, angle: 0, bounce: 0.85 };
const SLIDERS = ["intensity", "bend", "angle", "bounce"];

const toggle = document.getElementById("toggle");
const enabled = document.getElementById("enabled");

function label(key, value) {
  return key === "angle" ? `${Math.round(value)}°` : Number(value).toFixed(2);
}

function paint(settings) {
  enabled.checked = settings.enabled !== false;
  toggle.dataset.on = String(enabled.checked);
  for (const key of SLIDERS) {
    const input = document.getElementById(key);
    input.value = settings[key];
    document.getElementById(`${key}Out`).textContent = label(key, settings[key]);
  }
}

chrome.storage.sync.get(null, (stored) => paint({ ...DEFAULTS, ...(stored || {}) }));

toggle.addEventListener("click", () => {
  const next = !(toggle.dataset.on === "true");
  toggle.dataset.on = String(next);
  enabled.checked = next;
  chrome.storage.sync.set({ enabled: next });
});

for (const key of SLIDERS) {
  const input = document.getElementById(key);
  input.addEventListener("input", () => {
    const value = parseFloat(input.value);
    document.getElementById(`${key}Out`).textContent = label(key, value);
    chrome.storage.sync.set({ [key]: value });
  });
}
