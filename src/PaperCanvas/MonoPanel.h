/*----------------------------------------------------------------------------/
  PaperCanvas - a LovyanGFX panel that is not a display.

  LGFXVirtualScreen renders tiles and pushes them to a `LovyanGFX&` panel. Our
  output is a 1bpp bitmap, not a screen, so we supply a panel whose "display" is
  the bitmap. Tiles are drawn at grayscale_8bit; this panel receives them, asks
  LovyanGFX's own pixelcopy to expand each row into an 8-bit gray line buffer,
  then reduces that line to one bit per pixel and packs it.

  Keeping the intermediate in grayscale is what lets PaperCanvas own the
  monochrome conversion (thresholding vs ordered dither) instead of inheriting
  whatever LovyanGFX's colour conversion would do.

  See docs/DECISIONS.ja.md D3 and docs/CORE_DESIGN.ja.md §7.
/----------------------------------------------------------------------------*/
#pragma once

#include "Gfx.h"

#include <string.h>

#include "Common.h"
#include "Dither.h"

namespace PaperCanvas {

/// Called once per band with a 1bpp strip of the page. `y` is the absolute page
/// row the band starts at. The data is only valid for the duration of the call.
typedef void (*BandFn)(const Bitmap& band, uint16_t y, void* ctx);

class MonoPanel : public lgfx::Panel_Device {
 public:
  MonoPanel() {
    _write_depth = lgfx::color_depth_t::grayscale_8bit;
    _read_depth = lgfx::color_depth_t::grayscale_8bit;
  }

  ~MonoPanel() { releaseRowBuffers(); }

  //--------------------------------------------------------------------------
  // Configuration (called by Receipt / Label before rendering)
  //--------------------------------------------------------------------------

  /// Page size in pixels. Must be set before the first render.
  bool setPageSize(uint16_t w, uint16_t h) {
    releaseRowBuffers();
    auto cfg = config();
    cfg.panel_width = cfg.memory_width = w;
    cfg.panel_height = cfg.memory_height = h;
    config(cfg);
    _width = w;
    _height = h;
    _rowBytes = PaperCanvas::rowBytes(w);
    _gray = (uint8_t*)malloc(w ? w : 1);
    _bandRow = (uint8_t*)malloc(_rowBytes ? _rowBytes : 1);
    if (!_gray || !_bandRow) {
      releaseRowBuffers();
      return false;
    }
    return true;
  }

  void setMono(Mono method, uint8_t threshold = 128) {
    _mono = method;
    _threshold = threshold;
  }

  /// Write the whole page into `data`, which must hold `rowBytes * height`.
  void setPageTarget(uint8_t* data, size_t size) {
    _page = data;
    _pageSize = size;
    _bandFn = nullptr;
  }

  /// Emit bands instead of holding the page.
  void setBandTarget(BandFn fn, void* ctx) {
    _bandFn = fn;
    _bandCtx = ctx;
    _page = nullptr;
    _pageSize = 0;
  }

  uint16_t pageRowBytes() const { return _rowBytes; }

  /// Clear the destination to white. Bands have nothing to clear.
  ///
  /// Only the page itself is cleared, never the whole buffer: callers are
  /// allowed to hand over something larger (a shared arena, a buffer sized for
  /// the tallest receipt they expect) and the bytes past the page are theirs.
  void beginPage() {
    if (!_page) { return; }
    const size_t pageBytes = (size_t)_rowBytes * _height;
    memset(_page, 0, pageBytes < _pageSize ? pageBytes : _pageSize);
  }

  //--------------------------------------------------------------------------
  // Panel_Device / IPanel
  //--------------------------------------------------------------------------

  bool init(bool use_reset) override {
    (void)use_reset;
    return _gray != nullptr;
  }

  void beginTransaction(void) override {}
  void endTransaction(void) override {}

  void setRotation(uint_fast8_t r) override {
    // Rotation would move the tile/page relationship out from under the dither,
    // which is indexed by absolute page coordinates. Pages are never rotated.
    (void)r;
  }

  lgfx::color_depth_t setColorDepth(lgfx::color_depth_t depth) override {
    // The sink is always 8-bit gray regardless of what is asked for; the
    // reduction to one bit happens here, not in LovyanGFX's colour conversion.
    (void)depth;
    _write_depth = lgfx::color_depth_t::grayscale_8bit;
    _read_depth = lgfx::color_depth_t::grayscale_8bit;
    return _write_depth;
  }

  void setInvert(bool invert) override { _invert = invert; }
  void setSleep(bool) override {}
  void setPowerSave(bool) override {}

  void writeCommand(uint32_t, uint_fast8_t) override {}
  void writeData(uint32_t, uint_fast8_t) override {}

  void initDMA(void) override {}
  void waitDMA(void) override {}
  bool dmaBusy(void) override { return false; }
  void waitDisplay(void) override {}
  bool displayBusy(void) override { return false; }
  bool isReadable(void) const override { return false; }
  bool isBusShared(void) const override { return false; }

  void display(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t) override {}

  uint32_t readCommand(uint_fast16_t, uint_fast8_t, uint_fast8_t) override { return 0; }
  uint32_t readData(uint_fast8_t, uint_fast8_t) override { return 0; }
  void readRect(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t, void*,
                lgfx::pixelcopy_t*) override {}
  void copyRect(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t,
                uint_fast16_t) override {}

  void setWindow(uint_fast16_t xs, uint_fast16_t ys, uint_fast16_t xe, uint_fast16_t ye) override {
    _winXs = xs;
    _winYs = ys;
    _winXe = xe;
    _winYe = ye;
    _winX = xs;
    _winY = ys;
  }

  void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t rawcolor) override {
    if (x >= _width || y >= _height) { return; }
    setBit(x, y, isBlack(rawToGray(rawcolor), x, y));
  }

  void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                               uint32_t rawcolor) override {
    const uint8_t gray = rawToGray(rawcolor);
    const uint_fast16_t xe = (x + w > _width) ? _width : x + w;
    const uint_fast16_t ye = (y + h > _height) ? _height : y + h;
    for (uint_fast16_t yy = y; yy < ye; ++yy) {
      for (uint_fast16_t xx = x; xx < xe; ++xx) {
        setBit(xx, yy, isBlack(gray, xx, yy));
      }
    }
  }

  void writeBlock(uint32_t rawcolor, uint32_t length) override {
    // A run of identical pixels walking the window set by setWindow().
    while (length--) {
      drawPixelPreclipped(_winX, _winY, rawcolor);
      advanceWindow();
    }
  }

  void writePixels(lgfx::pixelcopy_t* param, uint32_t len, bool) override {
    // Pixels streamed through the window. Convert a window row at a time so the
    // same fp_copy path is used as in writeImage().
    while (len) {
      const uint32_t runEnd = (_winXe + 1u) - _winX;
      uint32_t run = (len < runEnd) ? len : runEnd;
      if (_winY < _height && _gray) {
        const int32_t pos = _winX;
        const int32_t end = pos + (int32_t)run;
        int32_t p = pos;
        while (end != (p = param->fp_copy(_gray, p, end, param)) &&
               end != (p = param->fp_skip(p, end, param))) {}
        packRow(_winX, run, _winY);
      }
      _winX += run;
      if (_winX > _winXe) {
        _winX = _winXs;
        ++_winY;
      }
      len -= run;
    }
  }

  /// The path that matters: LGFXVirtualScreen pushes each finished tile here.
  void writeImage(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                  lgfx::pixelcopy_t* param, bool) override {
    if (!_gray || w == 0 || h == 0) { return; }
    if (x >= _width || y >= _height) { return; }
    if (x + w > _width) { w = _width - x; }
    if (y + h > _height) { h = _height - y; }

    // fp_copy advances param->src_x32 as it walks a row, so the source position
    // has to be put back before the next row or every row reads further into
    // (and eventually past) the tile buffer. Panel_FrameBufferBase restores it
    // the same way; nextx is 0 because pages are never rotated.
    const uint32_t nexty = 1u << lgfx::pixelcopy_t::FP_SCALE;
    uint32_t sx32 = param->src_x32;
    uint32_t sy32 = param->src_y32;

    for (uint_fast16_t row = 0; row < h; ++row) {
      const int32_t pos = (int32_t)x;
      const int32_t end = pos + (int32_t)w;
      int32_t p = pos;
      // fp_copy writes _write_depth pixels (8-bit gray) into _gray, indexed by
      // pixel position, and returns where it stopped. fp_skip walks over
      // transparent runs. This is the same loop Panel_FrameBufferBase uses.
      while (end != (p = param->fp_copy(_gray, p, end, param)) &&
             end != (p = param->fp_skip(p, end, param))) {}
      packRow(x, w, y + row);
      param->src_x32 = sx32;
      param->src_y32 = (sy32 += nexty);
    }
  }

  void writeImageARGB(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                      lgfx::pixelcopy_t* param) override {
    writeImage(x, y, w, h, param, false);
  }

 private:
  void releaseRowBuffers() {
    if (_gray) { free(_gray); _gray = nullptr; }
    if (_bandRow) { free(_bandRow); _bandRow = nullptr; }
  }

  void advanceWindow() {
    if (++_winX > _winXe) {
      _winX = _winXs;
      if (++_winY > _winYe) { _winY = _winYs; }
    }
  }

  static uint8_t rawToGray(uint32_t rawcolor) { return (uint8_t)(rawcolor & 0xFF); }

  bool isBlack(uint8_t gray, uint_fast16_t x, uint_fast16_t y) const {
    if (_invert) { gray = (uint8_t)(255 - gray); }
    return detail::isBlack(gray, _mono, _threshold, (uint16_t)x, (uint16_t)y);
  }

  /// Reduce `_gray[x .. x+w)` to bits and store them at page row `y`.
  void packRow(uint_fast16_t x, uint_fast16_t w, uint_fast16_t y) {
    if (y >= _height) { return; }
    for (uint_fast16_t i = 0; i < w; ++i) {
      const uint_fast16_t px = x + i;
      setBit(px, y, isBlack(_gray[px], px, y));
    }
    if (_bandFn && x + w >= _width) { flushBandRow(y); }
  }

  void setBit(uint_fast16_t x, uint_fast16_t y, bool black) {
    uint8_t* row = rowPtr(y);
    if (!row) { return; }
    const uint8_t mask = (uint8_t)(0x80u >> (x & 7));
    if (black) {
      row[x >> 3] |= mask;
    } else {
      row[x >> 3] &= (uint8_t)~mask;
    }
  }

  uint8_t* rowPtr(uint_fast16_t y) {
    if (_page) {
      const size_t off = (size_t)y * _rowBytes;
      return (off + _rowBytes <= _pageSize) ? _page + off : nullptr;
    }
    if (_bandFn) {
      if ((int32_t)y != _bandRowY) {
        memset(_bandRow, 0, _rowBytes);
        _bandRowY = (int32_t)y;
      }
      return _bandRow;
    }
    return nullptr;
  }

  void flushBandRow(uint_fast16_t y) {
    Bitmap band;
    band.data = _bandRow;
    band.width = _width;
    band.height = 1;
    band.rowBytes = _rowBytes;
    _bandFn(band, (uint16_t)y, _bandCtx);
    memset(_bandRow, 0, _rowBytes);
    _bandRowY = -1;
  }

  uint8_t* _gray = nullptr;
  uint8_t* _bandRow = nullptr;
  int32_t _bandRowY = -1;

  uint8_t* _page = nullptr;
  size_t _pageSize = 0;
  BandFn _bandFn = nullptr;
  void* _bandCtx = nullptr;

  uint16_t _rowBytes = 0;
  Mono _mono = Mono::Threshold;
  uint8_t _threshold = 128;
  bool _invert = false;

  uint_fast16_t _winXs = 0, _winYs = 0, _winXe = 0, _winYe = 0;
  uint_fast16_t _winX = 0, _winY = 0;
};

/// A LovyanGFX device whose panel is a MonoPanel. LGFXVirtualScreen takes a
/// `LovyanGFX&`, so this is what gets handed to it.
class MonoSink : public lgfx::LGFX_Device {
 public:
  MonoSink() { setPanel(&_panel); }

  MonoPanel& panel() { return _panel; }

  bool begin(uint16_t w, uint16_t h) {
    if (!_panel.setPageSize(w, h)) { return false; }
    if (!init()) { return false; }
    // LGFXVirtualScreen creates its tile sprites at `getColorDepth()`, which
    // reads the device's write conversion rather than the panel's. Setting it
    // here is what makes the tiles 8-bit gray.
    setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    return true;
  }

 private:
  MonoPanel _panel;
};

}  // namespace PaperCanvas
