// Liquid Glass, on a microcontroller.
//
// The desktop studio in this repository is an OpenGL app: a fragment shader
// treats shapes as lenses, bending a wallpaper at the rim with spectral
// dispersion, specular highlights and an adaptive body tint. This sketch is
// that material rebuilt for a board with no GPU, no float unit worth relying
// on, and less RAM than a single scanline of the original.
//
// What survives intact: the smooth-min SDF field, the circular bevel profile,
// the two-light specular, the adaptive tint, the rim hairline.
// What changed: the background is a function rather than a texture, so
// refraction costs one evaluation instead of a render pass; and the spectral
// fringe is traced as light rays running around the fillet rather than
// integrated per pixel. See lg_rays.h for why that is both cheaper and closer
// to what a real glass edge does.
//
// ---------------------------------------------------------------------------
// WIRING
// ---------------------------------------------------------------------------
// This sketch does not own your pins. Whichever driver you pick below, the
// display is configured in that library's own way:
//
//   TFT_eSPI   - edit its User_Setup.h (or select a User_Setup_Select profile)
//                for your panel and pin-out. Nothing here needs changing.
//   Adafruit   - fill in the CS/DC/RST pins in the constructor below.
//
// Tested shapes of setup: ESP32 + ST7789 240x240, ESP32 + ILI9341 320x240,
// RP2040 + ST7789. Anything Adafruit_GFX drives will work; anything with an
// FPU will be pleasant.
//
// ---------------------------------------------------------------------------
// TUNING
// ---------------------------------------------------------------------------
// Build arduino/host/ and run ./preview to see exactly these pixels on your
// desktop, with --gain/--disp/--decay/--bounce to try edge-ray settings
// without a reflash. The host build compiles these same files.

// The IDE injects this for .ino files; PlatformIO and arduino-cli do not
// always, and it costs nothing to be explicit.
#include <Arduino.h>

#include "lg_config.h"
#include "lg_scene.h"
#include "lg_render.h"
#include "lg_demo.h"

// ---------------------------------------------------------------------------
// display driver
// ---------------------------------------------------------------------------

#define LG_DRIVER_TFT_ESPI 1
#define LG_DRIVER_ADAFRUIT 2
#define LG_DRIVER_NONE     3    // no panel: render and report timings only

#ifndef LG_DRIVER
  #define LG_DRIVER LG_DRIVER_TFT_ESPI
#endif

#if LG_DRIVER == LG_DRIVER_TFT_ESPI
  #include <TFT_eSPI.h>
  static TFT_eSPI tft = TFT_eSPI();

#elif LG_DRIVER == LG_DRIVER_ADAFRUIT
  #include <Adafruit_GFX.h>
  #include <Adafruit_ST7789.h>
  #define TFT_CS  5
  #define TFT_DC  16
  #define TFT_RST 23
  static Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
#endif

static int gW = 240, gH = 240;

static void flush_stripe(int x, int y, int w, int h,
                         const uint16_t *pixels, void *user) {
    (void)user;
#if LG_DRIVER == LG_DRIVER_TFT_ESPI
    tft.pushImage(x, y, w, h, (uint16_t *)pixels);
#elif LG_DRIVER == LG_DRIVER_ADAFRUIT
    tft.drawRGBBitmap(x, y, (uint16_t *)pixels, w, h);
#else
    (void)x; (void)y; (void)w; (void)h; (void)pixels;
#endif
}

// ---------------------------------------------------------------------------

static LGScene scene;
static uint32_t frame = 0;
static uint32_t lastReport = 0;
static uint32_t framesSince = 0;

void setup() {
    Serial.begin(115200);

#if LG_DRIVER == LG_DRIVER_TFT_ESPI
    tft.init();
    tft.setRotation(0);
    // The renderer emits native-order 565 and hands it over as a block; this
    // is the one line that has to agree with it.
    tft.setSwapBytes(true);
    gW = tft.width();
    gH = tft.height();
#elif LG_DRIVER == LG_DRIVER_ADAFRUIT
    tft.init(240, 240);
    tft.setRotation(0);
    gW = tft.width();
    gH = tft.height();
#endif

    if (gW > LG_MAX_WIDTH) {
        // Raise LG_MAX_WIDTH in lg_config.h if you have the RAM for it; the
        // stripe buffer is the only thing that grows.
        Serial.print(F("panel wider than LG_MAX_WIDTH, clipping to "));
        Serial.println(LG_MAX_WIDTH);
        gW = LG_MAX_WIDTH;
    }

    Serial.print(F("Liquid Glass  "));
    Serial.print(gW); Serial.print('x'); Serial.print(gH);
    Serial.print(F("  tier ")); Serial.print(LG_TIER);
    Serial.print(F("  stripe ")); Serial.print(LG_STRIPE_H);
    Serial.print(F("  rays ")); Serial.print(LG_RAY_COUNT);
    Serial.print(F("x")); Serial.print(LG_RAY_BANDS);
    Serial.println();

    lastReport = millis();
}

void loop() {
    // One loop of the demo every ~7 s. The animation is a phase, not a delta,
    // so a slow board renders fewer frames of the same motion rather than
    // drifting out of step.
    fx t = (fx)((millis() % 7000UL) * FX_ONE / 7000UL);

    lg_demo_scene(&scene, gW, gH, t);
    lg_render(&scene, gW, gH, flush_stripe, 0);

    frame++;
    framesSince++;
    uint32_t now = millis();
    if (now - lastReport >= 2000) {
        Serial.print(framesSince * 1000.0f / (now - lastReport));
        Serial.println(F(" fps"));
        framesSince = 0;
        lastReport = now;
    }
}
