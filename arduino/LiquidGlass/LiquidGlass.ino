// Liquid Glass, on a microcontroller.
//
// The desktop studio in this repository is an OpenGL app: a fragment shader
// treats shapes as lenses, bending a wallpaper at the rim with spectral
// dispersion, specular highlights and an adaptive body tint. This sketch runs
// that shader. Not an approximation of it, not a redrawing of it in integers —
// `shaders.py`'s GLASS_FRAG translated line for line into float, with
// `engine.py`'s two passes and the two-lobe hairline from the WebGL build.
//
// The previous port here was Q16.16 fixed point and replaced the parts a
// microcontroller could not afford: the wallpaper became a function, the wide
// ambient mip became a 4x4 luma grid, and the eight spectral taps became
// traced rays. It was honest arithmetic and it did not look like the material.
// This one keeps the material and spends the FPU, because the boards that are
// interesting for this — ESP32, ESP32-P4, RP2040, Teensy — all have one.
//
// What that costs, plainly, so you can decide before you compile:
//   * the background must be a TEXTURE with a mip pyramid, so a panel-sized
//     RGB565 buffer plus a third again for the mips has to live somewhere;
//   * a frame is tens of milliseconds to seconds depending on the board and
//     the area of glass. This is a material, not an animation system. Render
//     a surface once, keep the pixels, redraw when something changes.
//
// Measured on an M5Stack Tab5 (ESP32-P4, 360 MHz): a 150x52 pill 145 ms, a
// 960x96 card 1 505 ms, a 460x420 frosted panel 1 731 ms. See arduino/README.md
// for the method and for the fidelity numbers against the real WebGL2 build.
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
// Anything Adafruit_GFX drives will work. Panels wider than 1280 px will not:
// the renderer's stripe buffer is sized for that and fg_render_region declines
// a wider window rather than overrun it.
//
// ---------------------------------------------------------------------------
// TUNING
// ---------------------------------------------------------------------------
// Build arduino/host/ and run ./preview to see exactly these pixels on your
// desktop — the host build compiles these same files with the same flags, so
// what the preview writes is what the panel gets, give or take one RGB565
// dither phase. Tune there, flash once.
//
// The dials that are worth your time are all -D flags, documented where they
// are defined: FG_SPECTRAL_TAPS and FG_TRILINEAR (speed against fidelity, with
// the measured price of each in fg_glass.h), FG_CORNERS_CONTINUOUS and
// FG_AMBIENT_HUE (which of the two upstream builds to match), and FG_SS
// (supersampling — the library's device-pixel-ratio, and the only thing it is).

// The IDE injects this for .ino files; PlatformIO and arduino-cli do not
// always, and it costs nothing to be explicit.
#include <Arduino.h>

#include "fg_glass.h"
#include "fg_tex.h"
#include "fg_demo.h"

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
// where the big buffers live
// ---------------------------------------------------------------------------
// The renderer never calls malloc itself: every allocation goes through this
// hook (fg_mem_set_alloc), because on an ESP32 the difference between internal
// DRAM and PSRAM is the difference between fitting and not. The wall and its
// mip pyramid are the large ones; the stripe buffer is 20 KB.
//
// fg_mem_set_alloc_small() is the second hook, taken for mip levels of 16 KB
// or less. Those are the levels the adaptive body tint fetches for EVERY glass
// pixel, so on a board with both kinds of memory they are worth the fast one.

static void *big_alloc(unsigned long size) {
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM)
    void *p = ps_malloc((size_t)size);
    if (p) return p;
#endif
    return malloc((size_t)size);
}

static void *small_alloc(unsigned long size) {
    return malloc((size_t)size);       // internal RAM on an ESP32
}

// ---------------------------------------------------------------------------

static FGScene  scene;
static fg_tex_t wall;
static uint16_t *wallPixels = 0;

static uint32_t lastReport = 0;
static uint32_t framesSince = 0;
static uint32_t msSince = 0;

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

    if (gW > 1280) {
        // fg_render_region's stripe buffer is 1280 px wide. Narrow the window
        // rather than let it decline the render and leave you with a blank
        // panel and no message.
        Serial.println(F("panel wider than 1280, clipping"));
        gW = 1280;
    }

    fg_mem_set_alloc(big_alloc);
    fg_mem_set_alloc_small(small_alloc);

    // Bake the wall once. This is the allocation that decides whether the
    // sketch fits: w*h*2 bytes, plus about a third again for the pyramid.
    wallPixels = (uint16_t *)big_alloc((unsigned long)gW * gH * 2);
    if (!wallPixels) {
        Serial.print(F("no room for a "));
        Serial.print(gW); Serial.print('x'); Serial.print(gH);
        Serial.println(F(" background — need w*h*2 bytes plus ~1/3 for mips"));
        return;
    }
    fg_demo_wall(wallPixels, gW, gH);
    if (!fg_tex_bind(&wall, wallPixels, gW, gH)) {
        Serial.println(F("mip pyramid did not fit"));
        return;
    }

    Serial.print(F("Liquid Glass  "));
    Serial.print(gW); Serial.print('x'); Serial.print(gH);
    Serial.print(F("  wall ")); Serial.print((unsigned long)gW * gH * 2 / 1024); Serial.print(F(" KB"));
    Serial.print(F("  levels ")); Serial.print(wall.levels);
    Serial.print(F("  taps ")); Serial.print(FG_SPECTRAL_TAPS);
    Serial.print(F("  ss ")); Serial.print(FG_SS);
    Serial.println();

    lastReport = millis();
}

void loop() {
    if (!wallPixels || wall.levels < 1) return;

    // One loop of the demo every ~7 s. The animation is a phase, not a delta,
    // so a slow board renders fewer frames of the same motion rather than
    // drifting out of step — which matters a great deal here, because this
    // renderer is slow and honest about it.
    float t = (float)(millis() % 7000UL) / 7000.0f;

    uint32_t t0 = millis();
    fg_demo_scene(&scene, gW, gH, t);
    fg_render_region(&scene, &wall, 0, 0, gW, gH, flush_stripe, 0);
    uint32_t dt = millis() - t0;

    framesSince++;
    msSince += dt;
    uint32_t now = millis();
    if (now - lastReport >= 2000) {
        // Both numbers, because at this cost per frame the fps figure alone
        // rounds away the thing you actually want to know.
        Serial.print(framesSince * 1000.0f / (now - lastReport));
        Serial.print(F(" fps  "));
        Serial.print(msSince / framesSince);
        Serial.println(F(" ms/frame"));
        framesSince = 0;
        msSince = 0;
        lastReport = now;
    }
}
