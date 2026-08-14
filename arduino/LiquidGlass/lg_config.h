// Build-time budget for the Liquid Glass renderer.
//
// Everything that costs RAM or cycles is a knob here, and the defaults are
// picked per MCU class so the sketch compiles and runs on whatever you have
// plugged in. Override any of these before including, or with -D flags.

#ifndef LG_CONFIG_H
#define LG_CONFIG_H

// ---------------------------------------------------------------------------
// board class detection
// ---------------------------------------------------------------------------

#if !defined(LG_TIER)
  #if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_ARCH_RP2040) || \
      defined(__IMXRT1062__) || defined(TEENSYDUINO) || defined(ARDUINO_ARCH_SAMD) || \
      !defined(ARDUINO)
    #define LG_TIER 2          // 32-bit, tens to hundreds of KB of RAM
  #elif defined(__AVR_ATmega2560__)
    #define LG_TIER 1          // Mega: 8KB SRAM
  #else
    #define LG_TIER 0          // Uno class: 2KB SRAM. It runs. Slowly.
  #endif
#endif

// ---------------------------------------------------------------------------
// framebuffer
// ---------------------------------------------------------------------------
//
// The renderer never holds a full frame. It fills LG_STRIPE_H rows at a time
// and hands them to the display, so peak RAM is width * LG_STRIPE_H * 2 bytes
// regardless of how tall the panel is.

#ifndef LG_MAX_WIDTH
  #if LG_TIER >= 2
    #define LG_MAX_WIDTH 320
  #elif LG_TIER == 1
    #define LG_MAX_WIDTH 240
  #else
    #define LG_MAX_WIDTH 160
  #endif
#endif

#ifndef LG_STRIPE_H
  #if LG_TIER >= 2
    #define LG_STRIPE_H 16
  #elif LG_TIER == 1
    #define LG_STRIPE_H 6
  #else
    #define LG_STRIPE_H 2
  #endif
#endif

// Most panels want big-endian 565 on the wire. TFT_eSPI's setSwapBytes(true)
// does the same job; set this instead if you drive the panel yourself.
#ifndef LG_SWAP_BYTES
  #define LG_SWAP_BYTES 0
#endif

// ---------------------------------------------------------------------------
// scene
// ---------------------------------------------------------------------------

#ifndef LG_MAX_SHAPES
  #if LG_TIER >= 2
    #define LG_MAX_SHAPES 12
  #else
    #define LG_MAX_SHAPES 4
  #endif
#endif

// ---------------------------------------------------------------------------
// edge light rays
// ---------------------------------------------------------------------------
//
// The dispersion model. On the GPU this is eight wavelength taps per pixel;
// here it is a few dozen rays traced once per frame and drawn as curves. See
// lg_rays.h for why. Cost scales as COUNT * STEPS * BANDS.

#ifndef LG_RAY_COUNT
  #if LG_TIER >= 2
    #define LG_RAY_COUNT 72
  #elif LG_TIER == 1
    #define LG_RAY_COUNT 24
  #else
    #define LG_RAY_COUNT 10
  #endif
#endif

#ifndef LG_RAY_STEPS
  #if LG_TIER >= 2
    #define LG_RAY_STEPS 18
  #elif LG_TIER == 1
    #define LG_RAY_STEPS 12
  #else
    #define LG_RAY_STEPS 8
  #endif
#endif

// Wavelengths drawn per ray. 1 collapses the fan to white and costs nothing,
// 6-8 gives the full spectral spread.
#ifndef LG_RAY_BANDS
  #if LG_TIER >= 2
    #define LG_RAY_BANDS 7
  #elif LG_TIER == 1
    #define LG_RAY_BANDS 5
  #else
    #define LG_RAY_BANDS 3
  #endif
#endif

// ---------------------------------------------------------------------------
// quality
// ---------------------------------------------------------------------------

// Central differences for the field normal cost two extra field evaluations
// over forward differences, and are worth it: the rim hairline and the
// specular both key off the normal direction, and forward differences bias it
// half a pixel diagonally, which shows up as a lopsided highlight.
#ifndef LG_CENTRAL_DIFF
  #define LG_CENTRAL_DIFF 1
#endif

// Continuous-curvature ("squircle") corners on rounded rects, matching the
// desktop shader. Costs two integer square roots and two divides, but only
// in the corner quadrant of a rounded rect — a small share of the pixels the
// field is asked about, and nothing at all for circles, pills and rings.
// Tier 0 pays that on a 16MHz AVR synthesising 64-bit multiplies, so it opts
// out and draws circular arcs.
#ifndef LG_SQUIRCLE
  #if LG_TIER >= 1
    #define LG_SQUIRCLE 1
  #else
    #define LG_SQUIRCLE 0
  #endif
#endif

#endif // LG_CONFIG_H
