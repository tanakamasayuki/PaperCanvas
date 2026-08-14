/*----------------------------------------------------------------------------/
  PaperCanvas - grayscale to 1bit reduction.

  Every method here is a pure function of (gray value, absolute page x, absolute
  page y). That is the whole point: the result cannot depend on where a tile
  boundary happened to fall, so the same page renders identically at any split
  count. Error diffusion would break that and is not offered.

  See docs/DECISIONS.ja.md D4.
/----------------------------------------------------------------------------*/
#pragma once

#include "Common.h"

namespace PaperCanvas {
namespace detail {

#ifndef PAPERCANVAS_NO_DITHER

/// Bayer 4x4, scaled to 0..255 so it compares directly against an 8-bit gray.
static constexpr uint8_t kBayer4[16] = {
    8,  136, 40,  168,
    200, 72, 232, 104,
    56,  184, 24,  152,
    248, 120, 216, 88,
};

/// Bayer 8x8, same scaling.
static constexpr uint8_t kBayer8[64] = {
    2,   130, 34,  162, 10,  138, 42,  170,
    194, 66,  226, 98,  202, 74,  234, 106,
    50,  178, 18,  146, 58,  186, 26,  154,
    242, 114, 210, 82,  250, 122, 218, 90,
    14,  142, 46,  174, 6,   134, 38,  166,
    206, 78,  238, 110, 198, 70,  230, 102,
    62,  190, 30,  158, 54,  182, 22,  150,
    254, 126, 222, 94,  246, 118, 214, 86,
};

#endif  // PAPERCANVAS_NO_DITHER

/// True when this pixel should be printed black.
///
/// `gray` is 0..255 with 0 = black. `x` and `y` are absolute page coordinates,
/// never tile-local — passing tile-local coordinates here is exactly the bug
/// the split-invariance test exists to catch.
static inline bool isBlack(uint8_t gray, Mono method, uint8_t threshold,
                           uint16_t x, uint16_t y) {
  switch (method) {
#ifndef PAPERCANVAS_NO_DITHER
    case Mono::Bayer4x4:
      return gray < kBayer4[((y & 3) << 2) | (x & 3)];
    case Mono::Bayer8x8:
      return gray < kBayer8[((y & 7) << 3) | (x & 7)];
#endif
    case Mono::Threshold:
    default:
      return gray < threshold;
  }
}

}  // namespace detail
}  // namespace PaperCanvas
