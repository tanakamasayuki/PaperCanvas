/*----------------------------------------------------------------------------/
  PaperCanvas - the layout record.

  Tiling re-runs the draw callback once per tile, so an element has to draw
  identically every time it is visited. That rules out anything that depends on
  "current" state at draw time: each record carries its own font, size,
  alignment and monochrome settings, resolved at the moment it was added.

  This is also why a setting change only affects elements added afterwards
  (docs/DECISIONS.ja.md D5) — by the time build() runs, there is no "current
  font" left to consult.
/----------------------------------------------------------------------------*/
#pragma once

#include <LovyanGFX.hpp>

#include "Common.h"

namespace PaperCanvas {

struct TextOptions {
  const lgfx::IFont* font = nullptr;  ///< nullptr = use the page's current font
  float size = 0;                     ///< 0 = use the page's current size
  Align align = Align::Left;
  VAlign valign = VAlign::Top;        ///< labels only
  int16_t lineSpacing = 0;
  bool wrap = false;
  bool invert = false;                ///< white on black
};

struct ImageOptions {
  Align align = Align::Center;
  VAlign valign = VAlign::Middle;     ///< labels only
  Fit fit = Fit::None;
  float scale = 1.0f;                 ///< Fit::Scale only
  Mono mono = Mono::Threshold;
  uint8_t threshold = 128;
  bool invert = false;
};

namespace detail {

enum class ElementType : uint8_t {
  Text,
  Image,
  Space,
  Line,   ///< a horizontal rule drawn as a filled rectangle
  Rule,   ///< a horizontal rule drawn by repeating a character
  Rect,   ///< labels only
};

/// How image pixel data was handed over.
enum class PixelFormat : uint8_t {
  Mono1bpp,   ///< packed 1bpp, MSB first, bit=1 black — PaperCanvas's own format
  Gray8,      ///< one byte per pixel, 0 = black
};

struct Element {
  ElementType type;

  /// Resolved placement. For a receipt, `rect.x`/`rect.w` span the printable
  /// width and `rect.y`/`rect.h` are filled in as elements are stacked. For a
  /// label these are exactly what the caller passed.
  Rect rect;

  // --- text -----------------------------------------------------------------
  uint32_t textOffset;   ///< byte offset into the page's text arena
  uint16_t textLength;
  const lgfx::IFont* font;
  float size;
  int16_t lineSpacing;
  Align align;
  VAlign valign;
  bool wrap;
  bool invert;

  // --- image ----------------------------------------------------------------
  const uint8_t* pixels;
  uint16_t srcW;
  uint16_t srcH;
  uint16_t srcRowBytes;
  PixelFormat format;
  Fit fit;
  float scale;
  Mono mono;
  uint8_t threshold;

  // --- line / rule / rect ---------------------------------------------------
  uint16_t thickness;
  char ruleChar;
  bool filled;
};

}  // namespace detail
}  // namespace PaperCanvas
