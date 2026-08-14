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

/// One column of a row. Widths resolve once, when the row is added, so a run of
/// rows lines up no matter how long the individual cells are.
///
/// Padding a line out with spaces cannot do this: proportional fonts make the
/// space width unpredictable, and even a monospaced font leaves the caller
/// counting characters. See docs/DECISIONS.ja.md D13.
struct Column {
  enum class Unit : uint8_t {
    Px,       ///< an exact pixel width
    Percent,  ///< a share of the width left after gaps
    Rest,     ///< whatever is left over; split evenly if there is more than one
    Auto,     ///< exactly as wide as this row's cell needs
  };

  Unit unit = Unit::Rest;
  float value = 0;
  Align align = Align::Left;
  char leader = '\0';  ///< fills the space between this cell and the next

  static constexpr Column px(float v, Align a, char leader = '\0') {
    return Column{Unit::Px, v, a, leader};
  }
  static constexpr Column percent(float v, Align a, char leader = '\0') {
    return Column{Unit::Percent, v, a, leader};
  }
  static constexpr Column rest(Align a, char leader = '\0') {
    return Column{Unit::Rest, 0, a, leader};
  }
  static constexpr Column autoFit(Align a, char leader = '\0') {
    return Column{Unit::Auto, 0, a, leader};
  }
};

struct RowOptions {
  const lgfx::IFont* font = nullptr;
  float size = 0;
  int16_t lineSpacing = 0;
  bool wrap = false;    ///< wrap each cell inside its own column
  bool invert = false;
};

namespace detail {

enum class ElementType : uint8_t {
  Text,
  Row,    ///< several cells side by side in resolved column boxes
  Image,
  Space,
  Line,   ///< a horizontal rule drawn as a filled rectangle
  Rule,   ///< a horizontal rule drawn by repeating a character
  Rect,   ///< labels only
};

/// A cell with its column box already resolved to pixels. Resolving once, here,
/// is what keeps the drawn result from disagreeing with the measured height:
/// the tiled render calls the draw path once per tile, and any width recomputed
/// per tile is a width that can come out differently.
struct Cell {
  uint32_t textOffset;
  uint16_t textLength;
  int16_t x;       ///< absolute page x of the column box
  uint16_t w;      ///< column box width
  Align align;
  char leader;
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
  /// Either a pointer the caller keeps alive (a logo in flash, typically), or,
  /// when `pixelsOwned` is set, an offset into the page's own pixel arena. A
  /// barcode is generated at run time and has nowhere else to live, so it is
  /// copied in; a caller's static image is not, because copying a large logo
  /// into RAM would be a poor trade.
  const uint8_t* pixels;
  uint32_t pixelOffset;
  bool pixelsOwned;
  uint16_t srcW;
  uint16_t srcH;
  uint16_t srcRowBytes;
  PixelFormat format;
  Fit fit;
  float scale;
  Mono mono;
  uint8_t threshold;

  // --- row ------------------------------------------------------------------
  uint32_t cellOffset;  ///< byte offset into the page's cell arena
  uint8_t cellCount;

  // --- line / rule / rect ---------------------------------------------------
  uint16_t thickness;
  char ruleChar;
  bool filled;
};

}  // namespace detail
}  // namespace PaperCanvas
