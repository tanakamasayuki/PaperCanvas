/*----------------------------------------------------------------------------/
  PaperCanvas - Monochrome 1bpp bitmap generator for receipt and label printers.

  https://github.com/tanakamasayuki/PaperCanvas

  Licence: MIT
  Author:  TANAKA Masayuki
/----------------------------------------------------------------------------*/
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace PaperCanvas {

/// A monochrome bitmap. `bit = 1` is black (printed), MSB first: bit7 of a byte
/// is the leftmost pixel of the group. Rows always start on a byte boundary and
/// spare bits at the end of a row are 0 (white). This matches the ESC/POS raster
/// bit image layout (`GS v 0`), so most drivers can pass it through unchanged.
///
/// See docs/REQUIREMENTS.ja.md §10 and docs/DECISIONS.ja.md D10.
struct Bitmap {
  const uint8_t* data;
  uint16_t width;
  uint16_t height;
  uint16_t rowBytes;
};

struct Rect {
  int16_t x;
  int16_t y;
  uint16_t w;
  uint16_t h;
};

enum class Align : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Middle, Bottom };

enum class Fit : uint8_t {
  None,     ///< draw at original size; anything outside the rect is clipped
  Contain,  ///< scale to fit inside the rect, keeping the aspect ratio
  Cover,    ///< scale to cover the rect, keeping the aspect ratio; excess clipped
  Stretch,  ///< scale to the rect exactly, ignoring the aspect ratio
  Scale,    ///< scale by an explicit factor
};

/// How grayscale is reduced to one bit. Ordered dithering is indexed by absolute
/// page coordinates, so the result never depends on how the page was tiled.
/// Error diffusion is deliberately absent (docs/DECISIONS.ja.md D4).
enum class Mono : uint8_t {
  Threshold,
  Bayer4x4,
  Bayer8x8,
};

/// Diagnostics. These accumulate as elements are laid out; generation continues
/// regardless (docs/DECISIONS.ja.md D11). The one exception is a barcode that
/// does not fit even at scale 1, which is skipped rather than drawn unreadable.
enum Warning : uint16_t {
  Warning_None            = 0,
  Warning_TextClipped     = 1 << 0,
  Warning_TextWrapped     = 1 << 1,
  Warning_ImageScaled     = 1 << 2,  ///< reduced, so detail was lost; enlarging is not reported
  Warning_ImageClipped    = 1 << 3,
  Warning_OutOfBounds     = 1 << 4,
  Warning_BarcodeTooSmall = 1 << 5,
};

/// Bytes per row for a bitmap of this pixel width.
static constexpr uint16_t rowBytes(uint16_t width) {
  return (uint16_t)((width + 7) >> 3);
}

/// Millimetres to pixels at a given resolution. Rounds to nearest.
/// The API itself is pixel-only (docs/DECISIONS.ja.md D7); this is the one place
/// physical dimensions are converted, and callers do it once at the boundary.
static constexpr uint16_t mmToPx(float mm, uint16_t dpi) {
  return (uint16_t)((mm * dpi) / 25.4f + 0.5f);
}

static constexpr float pxToMm(uint16_t px, uint16_t dpi) {
  return (px * 25.4f) / (float)dpi;
}

}  // namespace PaperCanvas
