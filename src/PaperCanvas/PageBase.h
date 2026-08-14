/*----------------------------------------------------------------------------/
  PaperCanvas - the engine Receipt and Label share.

  Not a public class and not an abstract base: Receipt and Label are two
  concrete types with different vocabularies (stack vs place), and this holds
  the part that is genuinely the same — storing elements, measuring text,
  drawing an element onto a tile, and running the tiled render.

  See docs/CORE_DESIGN.ja.md §2 and docs/DECISIONS.ja.md D6.
/----------------------------------------------------------------------------*/
#pragma once

#include <LovyanGFX.hpp>
#include <LGFXVirtualCanvas.h>

#include <stdlib.h>
#include <string.h>

#include "Common.h"
#include "Dither.h"
#include "Element.h"
#include "MonoPanel.h"

namespace PaperCanvas {
namespace detail {

/// Growable byte storage. Two of these hold the whole document: one for the
/// element records, one for the text they refer to.
class Arena {
 public:
  ~Arena() { free(_data); }

  void clear() { _used = 0; }

  bool reserve(size_t need) {
    if (need <= _cap) { return true; }
    size_t cap = _cap ? _cap : 256;
    while (cap < need) { cap += cap >> 1; }
    uint8_t* p = (uint8_t*)realloc(_data, cap);
    if (!p) { return false; }
    _data = p;
    _cap = cap;
    return true;
  }

  /// Returns the offset the bytes landed at, or SIZE_MAX on failure.
  size_t append(const void* src, size_t n) {
    if (!reserve(_used + n)) { return (size_t)-1; }
    memcpy(_data + _used, src, n);
    const size_t at = _used;
    _used += n;
    return at;
  }

  size_t appendByte(uint8_t b) { return append(&b, 1); }

  uint8_t* data() { return _data; }
  const uint8_t* data() const { return _data; }
  size_t used() const { return _used; }

 private:
  uint8_t* _data = nullptr;
  size_t _cap = 0;
  size_t _used = 0;
};

/// Shared state and behaviour for Receipt and Label.
class PageBase {
 public:
  PageBase(uint16_t width, uint16_t height) : _width(width), _height(height) {}

  ~PageBase() { _measure.deleteSprite(); }

  //--------------------------------------------------------------------------
  // Settings. These are captured into each element as it is added, so changing
  // one never disturbs what has already been laid out (docs/DECISIONS.ja.md D5).
  //--------------------------------------------------------------------------

  void setDpi(uint16_t dpi) { _dpi = dpi; }
  uint16_t dpi() const { return _dpi; }

  void setFont(const lgfx::IFont* font) { _font = font; }
  void setTextSize(float size) { _size = size > 0 ? size : 1.0f; }
  void setAlign(Align a) { _align = a; }
  void setLineSpacing(int16_t px) { _lineSpacing = px; }
  void setWrap(bool on) { _wrap = on; }

  void setMono(Mono method, uint8_t threshold = 128) {
    _mono = method;
    _threshold = threshold;
  }

  void setMemoryLimit(size_t bytes) { _memLimit = bytes; }
  void setUsePsram(bool on) { _usePsram = on; }

  //--------------------------------------------------------------------------
  // State
  //--------------------------------------------------------------------------

  uint16_t width() const { return _width; }
  size_t count() const { return _elements.used() / sizeof(Element); }
  uint16_t warnings() const { return _warnings; }
  void clearWarnings() { _warnings = 0; }

  void clear() {
    _elements.clear();
    _text.clear();
    _warnings = 0;
  }

  //--------------------------------------------------------------------------
  // Generation
  //--------------------------------------------------------------------------

  size_t bufferSize() const { return (size_t)rowBytes(_width) * pageHeight(); }

  bool build(uint8_t* data, size_t size) {
    if (!data || size < bufferSize()) { return false; }
    MonoSink sink;
    if (!beginSink(sink)) { return false; }
    sink.panel().setPageTarget(data, size);
    sink.panel().beginPage();
    return runTiles(sink);
  }

  bool stream(BandFn fn, void* ctx = nullptr) {
    if (!fn) { return false; }
    MonoSink sink;
    if (!beginSink(sink)) { return false; }
    sink.panel().setBandTarget(fn, ctx);
    return runTiles(sink);
  }

 protected:
  //--------------------------------------------------------------------------
  // Element construction
  //--------------------------------------------------------------------------

  Element makeDefault(ElementType type) const {
    Element e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.rect = Rect{0, 0, 0, 0};
    e.font = _font;
    e.size = _size;
    e.lineSpacing = _lineSpacing;
    e.align = _align;
    e.valign = VAlign::Top;
    e.wrap = _wrap;
    e.scale = 1.0f;
    e.mono = _mono;
    e.threshold = _threshold;
    e.thickness = 1;
    return e;
  }

  bool push(const Element& e) {
    return _elements.append(&e, sizeof(Element)) != (size_t)-1;
  }

  Element* elementAt(size_t i) {
    return (Element*)(_elements.data() + i * sizeof(Element));
  }
  const Element* elementAt(size_t i) const {
    return (const Element*)(_elements.data() + i * sizeof(Element));
  }

  /// Copy text into the arena so the caller's buffer need not outlive the call.
  /// Returns false if it could not be stored.
  bool storeText(const char* s, Element& e) {
    if (!s) { s = ""; }
    const size_t len = strlen(s);
    const size_t at = _text.append(s, len + 1);
    if (at == (size_t)-1) { return false; }
    e.textOffset = (uint32_t)at;
    e.textLength = (uint16_t)len;
    return true;
  }

  const char* textOf(const Element& e) const {
    return (const char*)(_text.data() + e.textOffset);
  }

  void warn(uint16_t bits) { _warnings |= bits; }

  //--------------------------------------------------------------------------
  // Measuring
  //--------------------------------------------------------------------------

  /// A 1x1 sprite is enough to answer font metrics, and costs two bytes.
  lgfx::LGFX_Sprite& measurer() {
    if (!_measure.getBuffer()) {
      _measure.setColorDepth(8);
      _measure.createSprite(1, 1);
    }
    return _measure;
  }

  void applyFont(lgfx::LGFX_Sprite& m, const Element& e) {
    if (e.font) { m.setFont(e.font); }
    m.setTextSize(e.size);
  }

  uint16_t lineHeight(const Element& e) {
    auto& m = measurer();
    applyFont(m, e);
    return (uint16_t)m.fontHeight();
  }

  uint16_t textWidthOf(const Element& e, const char* s, size_t len) {
    auto& m = measurer();
    applyFont(m, e);
    if (len == strlen(s)) { return (uint16_t)m.textWidth(s); }
    // Measuring a prefix needs a copy; keep it on the stack and chunk if long.
    char buf[256];
    if (len < sizeof(buf)) {
      memcpy(buf, s, len);
      buf[len] = '\0';
      return (uint16_t)m.textWidth(buf);
    }
    return (uint16_t)m.textWidth(s);
  }

  /// Number of lines in text that already contains its final newlines.
  static uint16_t lineCount(const char* s) {
    uint16_t n = 1;
    for (; *s; ++s) {
      if (*s == '\n') { ++n; }
    }
    return n;
  }

  uint16_t textBlockHeight(const Element& e, const char* s) {
    const uint16_t lines = lineCount(s);
    const uint16_t lh = lineHeight(e);
    return (uint16_t)(lines * lh + (lines - 1) * (e.lineSpacing > 0 ? e.lineSpacing : 0));
  }

  //--------------------------------------------------------------------------
  // Wrapping
  //--------------------------------------------------------------------------

  static size_t utf8Advance(const char* s) {
    const uint8_t c = (uint8_t)*s;
    if (c < 0x80) { return 1; }
    if ((c & 0xE0) == 0xC0) { return 2; }
    if ((c & 0xF0) == 0xE0) { return 3; }
    if ((c & 0xF8) == 0xF0) { return 4; }
    return 1;
  }

  /// Rewrite `src` into the arena with newlines inserted so no line exceeds
  /// `limit` pixels, and record where it landed on `e`.
  ///
  /// Wrapping happens once, here, rather than at draw time: the tiled render
  /// calls the draw path once per tile, and a break decision recomputed each
  /// time is a break decision that can disagree with itself.
  bool storeWrapped(const char* src, Element& e, uint16_t limit) {
    if (limit == 0) { return storeText(src, e); }
    auto& m = measurer();
    applyFont(m, e);

    const size_t start = _text.used();
    uint16_t lineW = 0;
    bool wrapped = false;
    char ch[8];

    for (const char* p = src; *p;) {
      if (*p == '\n') {
        if (_text.appendByte('\n') == (size_t)-1) { return false; }
        lineW = 0;
        ++p;
        continue;
      }
      const size_t n = utf8Advance(p);
      memcpy(ch, p, n);
      ch[n] = '\0';
      const uint16_t cw = (uint16_t)m.textWidth(ch);
      if (lineW && lineW + cw > limit) {
        if (_text.appendByte('\n') == (size_t)-1) { return false; }
        lineW = 0;
        wrapped = true;
      }
      if (_text.append(p, n) == (size_t)-1) { return false; }
      lineW = (uint16_t)(lineW + cw);
      p += n;
    }
    if (_text.appendByte('\0') == (size_t)-1) { return false; }

    e.textOffset = (uint32_t)start;
    e.textLength = (uint16_t)(_text.used() - start - 1);
    if (wrapped) { warn(Warning_TextWrapped); }
    return true;
  }

  //--------------------------------------------------------------------------
  // Drawing
  //--------------------------------------------------------------------------

  /// Total page height. Receipt overrides this with its running total.
  virtual uint16_t pageHeight() const { return _height; }

  void drawAll(LGFXVirtualCanvas& g) {
    g.fillScreen(kWhite);
    const size_t n = count();
    for (size_t i = 0; i < n; ++i) { drawElement(g, *elementAt(i)); }
  }

  void drawElement(LGFXVirtualCanvas& g, const Element& e) {
    switch (e.type) {
      case ElementType::Text: drawText(g, e); break;
      case ElementType::Image: drawImage(g, e); break;
      case ElementType::Line: g.fillRect(e.rect.x, e.rect.y, e.rect.w, e.rect.h, kBlack); break;
      case ElementType::Rule: drawRule(g, e); break;
      case ElementType::Rect:
        if (e.filled) {
          g.fillRect(e.rect.x, e.rect.y, e.rect.w, e.rect.h, kBlack);
        } else {
          for (uint16_t t = 0; t < e.thickness; ++t) {
            g.drawRect(e.rect.x + t, e.rect.y + t, e.rect.w - 2 * t, e.rect.h - 2 * t, kBlack);
          }
        }
        break;
      case ElementType::Space: break;
    }
  }

  void drawText(LGFXVirtualCanvas& g, const Element& e) {
    const char* s = textOf(e);
    if (e.font) { g.setFont(e.font); }
    g.setTextSize(e.size);
    g.setTextDatum(lgfx::textdatum_t::top_left);
    g.setTextColor(e.invert ? kWhite : kBlack);

    auto& m = measurer();
    applyFont(m, e);
    const uint16_t lh = (uint16_t)m.fontHeight();
    const int16_t step = (int16_t)(lh + (e.lineSpacing > 0 ? e.lineSpacing : 0));

    if (e.invert) { g.fillRect(e.rect.x, e.rect.y, e.rect.w, e.rect.h, kBlack); }

    int16_t y = e.rect.y;
    const char* line = s;
    char buf[256];
    while (true) {
      const char* nl = strchr(line, '\n');
      const size_t len = nl ? (size_t)(nl - line) : strlen(line);
      const size_t copy = len < sizeof(buf) ? len : sizeof(buf) - 1;
      memcpy(buf, line, copy);
      buf[copy] = '\0';

      const uint16_t w = (uint16_t)m.textWidth(buf);
      int16_t x = e.rect.x;
      if (e.align == Align::Center) { x = (int16_t)(e.rect.x + (int)(e.rect.w - w) / 2); }
      else if (e.align == Align::Right) { x = (int16_t)(e.rect.x + (int)e.rect.w - w); }
      g.drawString(buf, x, y);

      if (!nl) { break; }
      line = nl + 1;
      y = (int16_t)(y + step);
    }
  }

  void drawRule(LGFXVirtualCanvas& g, const Element& e) {
    const char c[2] = {e.ruleChar, '\0'};
    auto& m = measurer();
    applyFont(m, e);
    const uint16_t cw = (uint16_t)m.textWidth(c);
    if (cw == 0) { return; }
    if (e.font) { g.setFont(e.font); }
    g.setTextSize(e.size);
    g.setTextDatum(lgfx::textdatum_t::top_left);
    g.setTextColor(kBlack);
    const uint16_t n = (uint16_t)(e.rect.w / cw);
    // Centre the leftover so a rule that does not divide evenly is not
    // lopsided; a receipt separator that hugs one margin reads as a mistake.
    int16_t x = (int16_t)(e.rect.x + (e.rect.w - n * cw) / 2);
    for (uint16_t i = 0; i < n; ++i) {
      g.drawString(c, x, e.rect.y);
      x = (int16_t)(x + cw);
    }
  }

  /// Nearest-neighbour blit with the element's own monochrome reduction applied
  /// before the pixels reach the tile.
  ///
  /// Reducing here rather than letting the panel do it is what makes per-element
  /// dithering possible at all: the panel sees one page-wide setting, so an
  /// image that wants a different one has to arrive already black or white.
  /// Both paths index the dither by absolute page coordinates, so the result is
  /// still independent of the tile count.
  void drawImage(LGFXVirtualCanvas& g, const Element& e) {
    if (!e.pixels || e.srcW == 0 || e.srcH == 0) { return; }
    const Rect& d = e.rect;
    if (d.w == 0 || d.h == 0) { return; }

    // 16.16 fixed point so the sampling grid is exactly reproducible.
    const uint32_t stepX = ((uint32_t)e.srcW << 16) / d.w;
    const uint32_t stepY = ((uint32_t)e.srcH << 16) / d.h;

    // Typed as grayscale_t so pushImage takes the same-depth path; passing a
    // raw byte pointer with an explicit depth selects the palette overload
    // instead, which does not apply here.
    static constexpr uint16_t kRowMax = 512;
    lgfx::grayscale_t row[kRowMax];
    const uint16_t rowLimit = d.w < kRowMax ? d.w : kRowMax;

    for (uint16_t dy = 0; dy < d.h; ++dy) {
      const uint16_t sy = (uint16_t)(((uint32_t)dy * stepY) >> 16);
      if (sy >= e.srcH) { break; }
      const int16_t py = (int16_t)(d.y + dy);
      for (uint16_t dx = 0; dx < rowLimit; ++dx) {
        const uint16_t sx = (uint16_t)(((uint32_t)dx * stepX) >> 16);
        uint8_t gray = samplePixel(e, sx, sy);
        if (e.invert) { gray = (uint8_t)(255 - gray); }
        const bool black =
            isBlack(gray, e.mono, e.threshold, (uint16_t)(d.x + dx), (uint16_t)py);
        row[dx] = (uint8_t)(black ? 0 : 255);
      }
      g.pushImage(d.x, py, rowLimit, 1, row);
    }
  }

  static uint8_t samplePixel(const Element& e, uint16_t x, uint16_t y) {
    if (e.format == PixelFormat::Gray8) {
      return e.pixels[(size_t)y * e.srcRowBytes + x];
    }
    const uint8_t byte = e.pixels[(size_t)y * e.srcRowBytes + (x >> 3)];
    const bool bit = (byte >> (7 - (x & 7))) & 1;
    return bit ? 0 : 255;  // 1 = black in PaperCanvas's own 1bpp format
  }

  /// Resolve the destination rectangle for an image inside `box`.
  Rect fitImage(const Element& e, const Rect& box, uint16_t& warnings) const {
    uint32_t w = e.srcW;
    uint32_t h = e.srcH;
    switch (e.fit) {
      case Fit::None:
        break;
      case Fit::Scale:
        w = (uint32_t)(e.srcW * e.scale + 0.5f);
        h = (uint32_t)(e.srcH * e.scale + 0.5f);
        break;
      case Fit::Stretch:
        w = box.w;
        h = box.h;
        break;
      case Fit::Contain:
      case Fit::Cover: {
        // Compare cross-products instead of dividing, so the choice of axis is
        // exact rather than dependent on float rounding.
        const bool widthLimits = (uint64_t)e.srcW * box.h > (uint64_t)e.srcH * box.w;
        const bool useWidth = (e.fit == Fit::Contain) ? widthLimits : !widthLimits;
        if (useWidth) {
          w = box.w;
          h = (uint32_t)(((uint64_t)e.srcH * box.w + e.srcW / 2) / e.srcW);
        } else {
          h = box.h;
          w = (uint32_t)(((uint64_t)e.srcW * box.h + e.srcH / 2) / e.srcH);
        }
        break;
      }
    }
    if (w == 0) { w = 1; }
    if (h == 0) { h = 1; }
    if (w != e.srcW || h != e.srcH) { warnings |= Warning_ImageScaled; }
    if (w > box.w || h > box.h) { warnings |= Warning_ImageClipped; }

    Rect r;
    r.w = (uint16_t)(w > 0xFFFF ? 0xFFFF : w);
    r.h = (uint16_t)(h > 0xFFFF ? 0xFFFF : h);
    r.x = box.x;
    r.y = box.y;
    if (e.align == Align::Center) { r.x = (int16_t)(box.x + ((int)box.w - (int)r.w) / 2); }
    else if (e.align == Align::Right) { r.x = (int16_t)(box.x + (int)box.w - (int)r.w); }
    if (e.valign == VAlign::Middle) { r.y = (int16_t)(box.y + ((int)box.h - (int)r.h) / 2); }
    else if (e.valign == VAlign::Bottom) { r.y = (int16_t)(box.y + (int)box.h - (int)r.h); }
    return r;
  }

  //--------------------------------------------------------------------------

  static constexpr uint32_t kBlack = 0x000000u;
  static constexpr uint32_t kWhite = 0xFFFFFFu;

  uint16_t _width;
  uint16_t _height;
  uint16_t _dpi = 203;

  const lgfx::IFont* _font = nullptr;
  float _size = 1.0f;
  Align _align = Align::Left;
  int16_t _lineSpacing = 0;
  bool _wrap = false;

  Mono _mono = Mono::Threshold;
  uint8_t _threshold = 128;

  size_t _memLimit = 0;
  bool _usePsram = false;

  uint16_t _warnings = Warning_None;

  Arena _elements;
  Arena _text;
  lgfx::LGFX_Sprite _measure;

 private:
  bool beginSink(MonoSink& sink) {
    const uint16_t h = pageHeight();
    if (_width == 0 || h == 0) { return false; }
    if (!sink.begin(_width, h)) { return false; }
    sink.panel().setMono(_mono, _threshold);
    return true;
  }

  static void drawThunk(LGFXVirtualCanvas& g, void* ctx) {
    ((PageBase*)ctx)->drawAll(g);
  }

  bool runTiles(MonoSink& sink) {
    LGFXVirtualScreen vs(sink);
    if (_memLimit) { vs.setMemoryLimit(_memLimit); }
    if (_usePsram) { vs.setUsePsram(true); }
    if (!vs.begin()) { return false; }
    return vs.render(drawThunk, this);
  }
};

}  // namespace detail
}  // namespace PaperCanvas
