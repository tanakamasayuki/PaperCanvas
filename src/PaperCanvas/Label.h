/*----------------------------------------------------------------------------/
  PaperCanvas - the label model: a fixed canvas with elements placed by rectangle.

  Where a receipt stacks and grows, a label is a fixed area and the caller says
  where each thing goes. Automatic layout is deliberately not part of this
  (docs/REQUIREMENTS.ja.md §7.2); the browser tool exists to make writing the
  rectangles bearable.

  Placing something partly or wholly outside the canvas raises a warning and is
  clipped rather than refused — one mistyped rectangle should not cost the whole
  label (docs/DECISIONS.ja.md D11).
/----------------------------------------------------------------------------*/
#pragma once

#include "PageBase.h"

namespace PaperCanvas {

class Label : public detail::PageBase {
 public:
  Label(uint16_t width, uint16_t height) : PageBase(width, height) {
    setWrap(false);  // a fixed rectangle cannot grow, so wrapping would overflow
  }

  uint16_t height() const { return _height; }

  //--------------------------------------------------------------------------

  bool addText(const Rect& r, const char* text, const TextOptions& opt = TextOptions{}) {
    detail::Element e = makeDefault(detail::ElementType::Text);
    applyTextOptions(e, opt);
    e.rect = r;
    checkBounds(r);
    if (!storeFitted(text, e, r.w)) { return false; }
    if (textBlockHeight(e, textOf(e)) > r.h) { warn(Warning_TextClipped); }
    return push(e);
  }

  bool addRow(const Rect& r, const char* const* cells, size_t n,
              const RowOptions& opt = RowOptions{}) {
    detail::Element e = makeDefault(detail::ElementType::Row);
    if (opt.font) { e.font = opt.font; }
    if (opt.size > 0) { e.size = opt.size; }
    if (opt.lineSpacing) { e.lineSpacing = opt.lineSpacing; }
    e.wrap = opt.wrap || e.wrap;
    e.invert = opt.invert;
    checkBounds(r);

    Rect box = r;
    const uint16_t rowH = buildRow(e, cells, n, box);
    if (rowH == 0) { return false; }
    if (rowH > r.h) { warn(Warning_TextClipped); }
    // buildRow sizes the element to the row's own height; the caller's
    // rectangle is what the row must live inside, so keep it.
    e.rect.h = r.h;
    return push(e);
  }

  bool addRow(const Rect& r, const char* left, const char* right) {
    const char* cells[2] = {left, right};
    return addRow(r, cells, 2);
  }

  bool addRow(const Rect& r, const char* left, const char* center, const char* right) {
    const char* cells[3] = {left, center, right};
    return addRow(r, cells, 3);
  }

  bool addImage(const Rect& r, const Bitmap& src, const ImageOptions& opt = ImageOptions{}) {
    return addImageRaw(r, src.data, src.width, src.height, src.rowBytes,
                       detail::PixelFormat::Mono1bpp, opt);
  }

  bool addImage(const Rect& r, const uint8_t* gray8, uint16_t w, uint16_t h,
                const ImageOptions& opt = ImageOptions{}) {
    return addImageRaw(r, gray8, w, h, w, detail::PixelFormat::Gray8, opt);
  }

  /// A barcode placed inside a rectangle. Returns false and raises
  /// Warning_BarcodeTooSmall if it cannot be drawn readably; nothing is drawn.
  template <class T>
  bool addBarcode(const Rect& r, const T& bc, const BarcodeOptions& opt = BarcodeOptions{}) {
    detail::Element e = makeDefault(detail::ElementType::Image);
    checkBounds(r);
    BarcodeLayout layout;
    bool tooSmall = false;
    if (!buildBarcode(e, bc, opt, r.w, r.h, layout, tooSmall)) {
      if (tooSmall) { warn(Warning_BarcodeTooSmall); }
      return false;
    }
    uint16_t ignored = 0;
    e.rect = fitImage(e, r, ignored);
    return push(e);
  }

  bool addRect(const Rect& r, bool fill = false, uint16_t thickness = 1) {
    detail::Element e = makeDefault(detail::ElementType::Rect);
    e.rect = r;
    e.filled = fill;
    e.thickness = thickness ? thickness : 1;
    checkBounds(r);
    return push(e);
  }

  /// A horizontal or vertical rule. Diagonals are not supported: a printer page
  /// is a grid of dots and an antialiased diagonal would not survive the 1bpp
  /// reduction predictably.
  bool addLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t thickness = 1) {
    detail::Element e = makeDefault(detail::ElementType::Line);
    const uint16_t t = thickness ? thickness : 1;
    if (y0 == y1) {
      const int16_t left = x0 < x1 ? x0 : x1;
      e.rect = Rect{left, y0, (uint16_t)((x0 < x1 ? x1 - x0 : x0 - x1) + 1), t};
    } else if (x0 == x1) {
      const int16_t top = y0 < y1 ? y0 : y1;
      e.rect = Rect{x0, top, t, (uint16_t)((y0 < y1 ? y1 - y0 : y0 - y1) + 1)};
    } else {
      return false;
    }
    checkBounds(e.rect);
    return push(e);
  }

 private:
  void applyTextOptions(detail::Element& e, const TextOptions& opt) {
    if (opt.font) { e.font = opt.font; }
    if (opt.size > 0) { e.size = opt.size; }
    if (opt.lineSpacing) { e.lineSpacing = opt.lineSpacing; }
    e.align = opt.align;
    e.valign = opt.valign;
    e.wrap = opt.wrap || e.wrap;
    e.invert = opt.invert;
  }

  bool addImageRaw(const Rect& r, const uint8_t* pixels, uint16_t w, uint16_t h,
                   uint16_t rowBytesIn, detail::PixelFormat fmt, const ImageOptions& opt) {
    if (!pixels || w == 0 || h == 0) { return false; }
    detail::Element e = makeDefault(detail::ElementType::Image);
    e.pixels = pixels;
    e.srcW = w;
    e.srcH = h;
    e.srcRowBytes = rowBytesIn;
    e.format = fmt;
    e.align = opt.align;
    e.valign = opt.valign;
    e.fit = opt.fit;
    e.scale = opt.scale > 0 ? opt.scale : 1.0f;
    e.mono = opt.mono;
    e.threshold = opt.threshold;
    e.invert = opt.invert;
    checkBounds(r);

    uint16_t warnBits = 0;
    e.rect = fitImage(e, r, warnBits);
    warn(warnBits);
    return push(e);
  }

  void checkBounds(const Rect& r) {
    if (r.x < 0 || r.y < 0 || (int)r.x + r.w > (int)_width ||
        (int)r.y + r.h > (int)_height) {
      warn(Warning_OutOfBounds);
    }
  }
};

}  // namespace PaperCanvas
