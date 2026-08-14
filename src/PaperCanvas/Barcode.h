/*----------------------------------------------------------------------------/
  PaperCanvas - barcode placement.

  PaperCanvas does not encode barcodes (docs/REQUIREMENTS.ja.md §5). What it
  does take on is the part every caller otherwise writes by hand and gets wrong:
  choosing a whole-number module width, reserving the quiet zone, and extending
  the EAN/UPC guard bars. Get any of those wrong and the result looks like a
  barcode and does not scan.

  Nothing here names BarcodeKit. Any type with these members works:

      uint16_t width() const;                    // modules across
      uint16_t height() const;                   // module rows; 1 for linear
      bool     module(uint16_t x, uint16_t y);   // true = black
      uint8_t  quietLeft/Right/Top/Bottom() const;
      bool     barExtends(uint16_t x) const;     // guard bar column

  That is BarcodeKit's shape, and <PaperCanvasBarcode.h> is the convenience
  header that pulls BarcodeKit in — but the core stays free of the dependency,
  so another encoder with the same shape drops in unchanged.
/----------------------------------------------------------------------------*/
#pragma once

#include <string.h>

#include "Common.h"

namespace PaperCanvas {

struct BarcodeOptions {
  uint16_t moduleWidth = 0;   ///< pixels per module; 0 = the largest whole number that fits
  uint16_t barHeight = 0;     ///< linear bar height in pixels; 0 = a quarter of the symbol width
  bool quietZone = true;      ///< reserve the format's recommended margins
  Align align = Align::Center;
  VAlign valign = VAlign::Middle;
  bool invert = false;
  /// How far the EAN/UPC guard bars run below the data bars, as a fraction of
  /// the bar height. Scanners use that step to find the symbol's edges, so it
  /// is not decoration.
  float guardExtend = 0.08f;
};

/// A barcode's geometry, worked out once. Drawing reads this and recomputes
/// nothing, so the symbol cannot drift between tiles.
struct BarcodeLayout {
  uint16_t scale = 0;       ///< pixels per module; always a whole number
  uint16_t width = 0;       ///< total pixels, quiet zone included
  uint16_t height = 0;
  uint16_t quietL = 0;      ///< quiet zone in pixels
  uint16_t quietR = 0;
  uint16_t quietT = 0;
  uint16_t quietB = 0;
  uint16_t barHeight = 0;
  uint16_t guardExtra = 0;  ///< extra pixels below the data bars on guard columns
  bool fits = false;        ///< false means it cannot be drawn readably at all
};

/// Work out how a barcode would sit in a box of this size.
///
/// Worth calling before adding one: `fits == false` means the symbol cannot be
/// drawn at even one pixel per module, and `addBarcode` will skip it rather
/// than print something unreadable.
template <class T>
BarcodeLayout barcodeLayout(const T& bc, const BarcodeOptions& opt, uint16_t boxW,
                            uint16_t boxH = 0) {
  BarcodeLayout l;
  const uint16_t mw = bc.width();
  const uint16_t mh = bc.height();
  if (mw == 0 || boxW == 0) { return l; }

  const uint16_t qL = opt.quietZone ? bc.quietLeft() : 0;
  const uint16_t qR = opt.quietZone ? bc.quietRight() : 0;
  const uint16_t qT = opt.quietZone ? bc.quietTop() : 0;
  const uint16_t qB = opt.quietZone ? bc.quietBottom() : 0;
  const uint16_t modulesX = (uint16_t)(mw + qL + qR);
  const bool twoD = mh > 1;

  // A module must be a whole number of pixels wide. At a fractional scale some
  // modules come out a pixel wider than others, and a scanner reads the uneven
  // widths as a different symbol — the single most common way a generated
  // barcode ends up unreadable.
  uint16_t scale = opt.moduleWidth;
  if (scale == 0) {
    scale = (uint16_t)(boxW / modulesX);
    if (twoD && boxH) {
      const uint16_t modulesY = (uint16_t)(mh + qT + qB);
      const uint16_t byHeight = (uint16_t)(boxH / (modulesY ? modulesY : 1));
      if (byHeight < scale) { scale = byHeight; }
    }
  }
  if (scale == 0) { return l; }

  l.scale = scale;
  l.quietL = (uint16_t)(qL * scale);
  l.quietR = (uint16_t)(qR * scale);
  l.quietT = (uint16_t)(qT * scale);
  l.quietB = (uint16_t)(qB * scale);
  l.width = (uint16_t)(modulesX * scale);

  if (twoD) {
    l.barHeight = (uint16_t)(mh * scale);
  } else {
    uint16_t bh = opt.barHeight ? opt.barHeight : (uint16_t)(l.width / 4);
    if (bh == 0) { bh = 1; }
    l.barHeight = bh;
    l.guardExtra = (uint16_t)(bh * (opt.guardExtend > 0 ? opt.guardExtend : 0));
  }
  l.height = (uint16_t)(l.quietT + l.barHeight + l.guardExtra + l.quietB);
  l.fits = l.width <= boxW && (boxH == 0 || l.height <= boxH);
  return l;
}

/// Bytes a rendered barcode of this layout occupies.
inline size_t barcodeBufferSize(const BarcodeLayout& l) {
  return (size_t)rowBytes(l.width) * l.height;
}

/// Render into a 1bpp buffer (bit = 1 black, MSB first), the same format the
/// rest of PaperCanvas uses. Going through a Bitmap keeps barcodes on the same
/// blitting and dithering path as any other image, rather than a second one.
template <class T>
bool renderBarcode(const T& bc, const BarcodeLayout& l, uint8_t* out, size_t size) {
  const uint16_t stride = rowBytes(l.width);
  if (!out || !l.scale || size < (size_t)stride * l.height) { return false; }
  memset(out, 0, (size_t)stride * l.height);

  const uint16_t mh = bc.height();
  const bool twoD = mh > 1;
  const uint16_t rows = twoD ? mh : 1;

  for (uint16_t my = 0; my < rows; ++my) {
    for (uint16_t mx = 0; mx < bc.width(); ++mx) {
      if (!bc.module(mx, my)) { continue; }
      const uint16_t x0 = (uint16_t)(l.quietL + mx * l.scale);
      uint16_t y0, h;
      if (twoD) {
        y0 = (uint16_t)(l.quietT + my * l.scale);
        h = l.scale;
      } else {
        y0 = l.quietT;
        h = (uint16_t)(l.barHeight + (bc.barExtends(mx) ? l.guardExtra : 0));
      }
      const uint16_t yEnd = (uint16_t)(y0 + h < l.height ? y0 + h : l.height);
      const uint16_t xEnd = (uint16_t)(x0 + l.scale < l.width ? x0 + l.scale : l.width);
      for (uint16_t y = y0; y < yEnd; ++y) {
        uint8_t* row = out + (size_t)y * stride;
        for (uint16_t x = x0; x < xEnd; ++x) {
          row[x >> 3] |= (uint8_t)(0x80u >> (x & 7));
        }
      }
    }
  }
  return true;
}

}  // namespace PaperCanvas
