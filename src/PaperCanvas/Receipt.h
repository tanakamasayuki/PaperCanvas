/*----------------------------------------------------------------------------/
  PaperCanvas - the receipt model: fixed width, height driven by content.

  Elements are stacked top to bottom and each one's height is resolved as it is
  added, so height() is answerable at any point and build() has nothing left to
  measure (docs/DECISIONS.ja.md D5).

  The consequence, which the API cannot hide and the documentation has to state:
  a setting only affects elements added after it.
/----------------------------------------------------------------------------*/
#pragma once

#include "PageBase.h"

namespace PaperCanvas {

class Receipt : public detail::PageBase {
 public:
  /// @param printableWidth  printable width in pixels (e.g. 384 for 58 mm at 203 dpi)
  explicit Receipt(uint16_t printableWidth) : PageBase(printableWidth, 0) {
    setWrap(true);  // a receipt grows downwards, so wrapping is always safe here
  }

  //--------------------------------------------------------------------------

  void setMargin(uint16_t top, uint16_t bottom, uint16_t left, uint16_t right) {
    _marginTop = top;
    _marginBottom = bottom;
    _marginLeft = left;
    _marginRight = right;
  }

  /// Height of everything added so far, including margins. Valid at any time.
  uint16_t height() const {
    if (count() == 0) { return 0; }
    return (uint16_t)(_marginTop + _cursor + _marginBottom);
  }

  void clear() {
    PageBase::clear();
    _cursor = 0;
  }

  //--------------------------------------------------------------------------
  // Elements. Each returns the height it occupied, or 0 if it could not be
  // stored.
  //--------------------------------------------------------------------------

  uint16_t addText(const char* text) { return addText(text, TextOptions{}); }

  uint16_t addText(const char* text, const TextOptions& opt) {
    detail::Element e = makeDefault(detail::ElementType::Text);
    applyTextOptions(e, opt);
    e.rect.x = (int16_t)_marginLeft;
    e.rect.w = contentWidth();

    if (e.wrap ? !storeWrapped(text, e, contentWidth()) : !storeText(text, e)) { return 0; }
    e.rect.h = textBlockHeight(e, textOf(e));
    return place(e);
  }

  /// A row of cells laid out in the current columns (see setColumns()).
  ///
  /// With no columns set, the last cells take exactly the width they need and
  /// the first takes the rest — so `addRow("Coffee", "960")` puts the name on
  /// the left and the figure hard against the right margin, which is what a
  /// receipt line is. Column widths resolve once per row, so a run of rows
  /// lines up regardless of how long the individual names are.
  uint16_t addRow(const char* const* cells, size_t n) {
    return addRow(cells, n, RowOptions{});
  }

  uint16_t addRow(const char* const* cells, size_t n, const RowOptions& opt) {
    detail::Element e = makeDefault(detail::ElementType::Row);
    if (opt.font) { e.font = opt.font; }
    if (opt.size > 0) { e.size = opt.size; }
    if (opt.lineSpacing) { e.lineSpacing = opt.lineSpacing; }
    e.wrap = opt.wrap || e.wrap;
    e.invert = opt.invert;

    Rect box{(int16_t)_marginLeft, 0, contentWidth(), 0};
    if (buildRow(e, cells, n, box) == 0) { return 0; }
    return place(e);
  }

  uint16_t addRow(const char* left, const char* right) {
    const char* cells[2] = {left, right};
    return addRow(cells, 2);
  }

  uint16_t addRow(const char* left, const char* center, const char* right) {
    const char* cells[3] = {left, center, right};
    return addRow(cells, 3);
  }

  uint16_t addImage(const Bitmap& src, const ImageOptions& opt = ImageOptions{}) {
    return addImageRaw(src.data, src.width, src.height, src.rowBytes,
                       detail::PixelFormat::Mono1bpp, opt);
  }

  uint16_t addImage(const uint8_t* gray8, uint16_t w, uint16_t h,
                    const ImageOptions& opt = ImageOptions{}) {
    return addImageRaw(gray8, w, h, w, detail::PixelFormat::Gray8, opt);
  }

  uint16_t addSpace(uint16_t px) {
    detail::Element e = makeDefault(detail::ElementType::Space);
    e.rect.x = (int16_t)_marginLeft;
    e.rect.w = contentWidth();
    e.rect.h = px;
    return place(e);
  }

  /// A solid horizontal rule across the content width.
  uint16_t addLine(uint16_t thickness = 1) {
    detail::Element e = makeDefault(detail::ElementType::Line);
    e.rect.x = (int16_t)_marginLeft;
    e.rect.w = contentWidth();
    e.rect.h = thickness ? thickness : 1;
    e.thickness = e.rect.h;
    return place(e);
  }

  /// A separator drawn by repeating a character, as receipts conventionally do.
  uint16_t addRule(char c) {
    detail::Element e = makeDefault(detail::ElementType::Rule);
    e.ruleChar = c;
    e.rect.x = (int16_t)_marginLeft;
    e.rect.w = contentWidth();
    if (!storeText("", e)) { return 0; }
    e.rect.h = lineHeight(e);
    return place(e);
  }

 protected:
  uint16_t pageHeight() const override { return height(); }

 private:
  uint16_t contentWidth() const {
    const int w = (int)_width - (int)_marginLeft - (int)_marginRight;
    return (uint16_t)(w > 0 ? w : 0);
  }

  void applyTextOptions(detail::Element& e, const TextOptions& opt) {
    if (opt.font) { e.font = opt.font; }
    if (opt.size > 0) { e.size = opt.size; }
    if (opt.lineSpacing) { e.lineSpacing = opt.lineSpacing; }
    e.align = opt.align != Align::Left ? opt.align : e.align;
    e.wrap = opt.wrap || e.wrap;
    e.invert = opt.invert;
  }

  uint16_t addImageRaw(const uint8_t* pixels, uint16_t w, uint16_t h, uint16_t rowBytesIn,
                       detail::PixelFormat fmt, const ImageOptions& opt) {
    if (!pixels || w == 0 || h == 0) { return 0; }
    detail::Element e = makeDefault(detail::ElementType::Image);
    e.pixels = pixels;
    e.srcW = w;
    e.srcH = h;
    e.srcRowBytes = rowBytesIn;
    e.format = fmt;
    e.align = opt.align;
    e.valign = VAlign::Top;  // a stacked image has no box to sit inside vertically
    e.fit = opt.fit;
    e.scale = opt.scale > 0 ? opt.scale : 1.0f;
    e.mono = opt.mono;
    e.threshold = opt.threshold;
    e.invert = opt.invert;

    // The box an image is fitted into is the content width by its natural
    // height; a receipt has no height constraint to fit against.
    Rect box{(int16_t)_marginLeft, 0, contentWidth(), h};
    if (e.fit == Fit::Cover || e.fit == Fit::Stretch) {
      // Both need a height to work against, and the only sensible one here is
      // the height the image would take at the content width.
      box.h = (uint16_t)(((uint64_t)h * contentWidth() + w / 2) / w);
    }
    uint16_t warnBits = 0;
    Rect placed = fitImage(e, box, warnBits);
    // Clipping is judged against the page width only; the receipt grows to fit
    // whatever height results, so a "too tall" image is not a problem here.
    if (placed.w > contentWidth()) { warnBits |= Warning_ImageClipped; }
    else { warnBits &= (uint16_t)~Warning_ImageClipped; }
    warn(warnBits);

    e.rect = placed;
    e.rect.y = 0;
    return place(e);
  }

  /// Stack an element and hand back the height it took.
  uint16_t place(detail::Element& e) {
    e.rect.y = (int16_t)(_marginTop + _cursor);
    if (!push(e)) { return 0; }
    _cursor = (uint16_t)(_cursor + e.rect.h);
    return e.rect.h;
  }

  uint16_t _marginTop = 0;
  uint16_t _marginBottom = 0;
  uint16_t _marginLeft = 0;
  uint16_t _marginRight = 0;
  uint16_t _cursor = 0;
};

}  // namespace PaperCanvas
