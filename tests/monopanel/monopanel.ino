// monopanel — Phase 0 spike (docs/DEVELOPMENT_PLAN.ja.md §2).
//
// The whole design rests on one assumption: that a Panel_Device subclass whose
// write depth is grayscale_8bit receives tile pixels through pixelcopy_t::fp_copy
// as 8-bit gray, and that LGFXVirtualScreen pushes tiles to it at the right page
// rows. If that holds, PaperCanvas owns the monochrome conversion, and dithering
// and band output both fall out naturally (docs/DECISIONS.ja.md D3).
//
// Checks, in the order they must pass:
//   HOOK   — fp_copy actually runs and non-zero gray reaches us. Everything else
//            is meaningless if the hook is dead, so this is verified first.
//   GRAY   — a gray ramp arrives with the values that were drawn, pre-threshold.
//   TILES  — tiles land at the right page rows, covering the page exactly.
//   SPLIT  — the same page at five memory limits is byte-identical.
//   BANDS  — band output concatenates to the same bytes as the full page.

#include <LovyanGFX.hpp>
#include <LGFXVirtualCanvas.h>
#include <PaperCanvas/MonoPanel.h>

#include <stdio.h>
#include <string.h>

using PaperCanvas::Bitmap;
using PaperCanvas::Mono;
using PaperCanvas::MonoSink;

static constexpr uint16_t PAGE_W = 384;
static constexpr uint16_t PAGE_H = 200;
static constexpr uint16_t ROW_BYTES = PaperCanvas::rowBytes(PAGE_W);
static constexpr size_t PAGE_BYTES = (size_t)ROW_BYTES * PAGE_H;

static uint8_t g_page[PAGE_BYTES];
static uint8_t g_ref[PAGE_BYTES];
static uint8_t g_banded[PAGE_BYTES];

// A gray level as a colour LovyanGFX will accept. Passing a uint32_t routes it
// through convert_rgb888, and grayscale_t's own conversion is (r + 2g + b) / 4,
// so an equal-channel value survives to the tile exactly.
static inline uint32_t gray(uint8_t v) { return lgfx::color888(v, v, v); }

//--------------------------------------------------------------------------
// Probe panel: same base as MonoPanel but keeps the gray line it was handed,
// so the spike can inspect what fp_copy produced before any thresholding.
//--------------------------------------------------------------------------
class ProbePanel : public PaperCanvas::MonoPanel {
 public:
  uint32_t imageCalls = 0;
  uint8_t lastGray[PAGE_W];
  uint16_t lastGrayLen = 0;
  uint16_t lastY = 0xFFFF;
  uint16_t minY = 0xFFFF;
  uint16_t maxY = 0;

  void writeImage(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                  lgfx::pixelcopy_t* param, bool dma) override {
    ++imageCalls;
    if (y < minY) { minY = (uint16_t)y; }
    if ((uint16_t)(y + h - 1) > maxY) { maxY = (uint16_t)(y + h - 1); }
    if (w <= PAGE_W) {
      // Expand the tile's first row through a copy of the descriptor so the
      // real conversion below still sees untouched state.
      lgfx::pixelcopy_t probe = *param;
      int32_t p = 0;
      const int32_t end = (int32_t)w;
      while (end != (p = probe.fp_copy(lastGray, p, end, &probe)) &&
             end != (p = probe.fp_skip(p, end, &probe))) {}
      lastGrayLen = (uint16_t)w;
      lastY = (uint16_t)y;
    }
    PaperCanvas::MonoPanel::writeImage(x, y, w, h, param, dma);
  }
};

class ProbeSink : public lgfx::LGFX_Device {
 public:
  ProbeSink() { setPanel(&_panel); }
  ProbePanel& panel() { return _panel; }
  bool begin(uint16_t w, uint16_t h) {
    if (!_panel.setPageSize(w, h)) { return false; }
    if (!init()) { return false; }
    setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    return true;
  }

 private:
  ProbePanel _panel;
};

//--------------------------------------------------------------------------
// Drawing under test. Deliberately spans tile boundaries.
//--------------------------------------------------------------------------
static void drawPage(LGFXVirtualCanvas& g) {
  g.fillScreen(gray(255));
  g.fillRect(0, 40, PAGE_W, 30, gray(0));            // black band over a boundary
  for (uint16_t x = 0; x < PAGE_W; ++x) {            // horizontal gray ramp
    g.drawFastVLine(x, 100, 60, gray((uint8_t)((uint32_t)x * 255u / (PAGE_W - 1))));
  }
  for (uint16_t i = 0; i < PAGE_H; ++i) {            // diagonal
    g.drawPixel((int32_t)((uint32_t)i * PAGE_W / PAGE_H), i, gray(0));
  }
}

// Full-height ramp, used to read back exact gray values from one tile.
static void drawRamp(LGFXVirtualCanvas& g) {
  g.fillScreen(gray(255));
  for (uint16_t x = 0; x < PAGE_W; ++x) {
    g.drawFastVLine(x, 0, PAGE_H, gray((uint8_t)((uint32_t)x * 255u / (PAGE_W - 1))));
  }
}

static void reportCheck(const char* name, bool ok, const char* note) {
  Serial.printf("#CHECK name=%s ok=%d note=%s\n", name, ok ? 1 : 0, note);
}

static void hexRow(const uint8_t* p, uint16_t n) {
  for (uint16_t i = 0; i < n; ++i) { Serial.printf("%02x", p[i]); }
}

static bool renderPage(size_t memLimit, Mono mono, uint8_t* out, size_t outSize,
                       int* tilesOut) {
  MonoSink sink;
  if (!sink.begin(PAGE_W, PAGE_H)) { return false; }
  sink.panel().setMono(mono);
  sink.panel().setPageTarget(out, outSize);
  sink.panel().beginPage();

  LGFXVirtualScreen vs(sink);
  if (memLimit) { vs.setMemoryLimit(memLimit); }
  if (!vs.begin()) { return false; }
  if (tilesOut) { *tilesOut = vs.tileCount(); }
  return vs.render(drawPage);
}

struct BandCtx {
  uint8_t* dst;
  uint16_t rows;
  bool overflow;
};

static void bandSink(const Bitmap& band, uint16_t y, void* p) {
  BandCtx* c = (BandCtx*)p;
  if ((size_t)(y + 1) * ROW_BYTES > PAGE_BYTES) {
    c->overflow = true;
    return;
  }
  memcpy(c->dst + (size_t)y * ROW_BYTES, band.data, band.rowBytes);
  ++c->rows;
}

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start monopanel\n");
  Serial.printf("#PAGE w=%u h=%u rowBytes=%u\n", PAGE_W, PAGE_H, ROW_BYTES);

  //------------------------------------------------------------ HOOK / TILES
  {
    static uint8_t buf[PAGE_BYTES];
    ProbeSink probe;
    bool ok = probe.begin(PAGE_W, PAGE_H);
    probe.panel().setMono(Mono::Threshold);
    probe.panel().setPageTarget(buf, sizeof(buf));
    probe.panel().beginPage();

    LGFXVirtualScreen vs(probe);
    vs.setMemoryLimit(16 * 1024);
    ok = ok && vs.begin();
    Serial.printf("#PROBE tiles=%d tileH=%d depth=%d\n", vs.tileCount(), vs.tileHeight(),
           (int)probe.getColorDepth());
    ok = ok && vs.render(drawPage);

    const bool called = probe.panel().imageCalls > 0;
    bool nonZero = false;
    for (uint16_t i = 0; i < probe.panel().lastGrayLen; ++i) {
      if (probe.panel().lastGray[i] != 0) { nonZero = true; break; }
    }
    Serial.printf("#PROBE calls=%lu lastY=%u len=%u minY=%u maxY=%u\n",
           (unsigned long)probe.panel().imageCalls, probe.panel().lastY,
           probe.panel().lastGrayLen, probe.panel().minY, probe.panel().maxY);

    reportCheck("render_ok", ok, "begin+render returned true");
    reportCheck("hook_writeImage_called", called, "fp_copy path reached");
    reportCheck("hook_gray_nonzero", nonZero, "non-zero gray observed pre-threshold");
    reportCheck("depth_is_grayscale8",
                probe.getColorDepth() == lgfx::color_depth_t::grayscale_8bit,
                "tiles are 8-bit gray");
    reportCheck("tiles_cover_page",
                probe.panel().minY == 0 && probe.panel().maxY == PAGE_H - 1,
                "tile rows span the page exactly");
  }

  //------------------------------------------------------------------- GRAY
  {
    static uint8_t buf[PAGE_BYTES];
    ProbeSink probe;
    probe.begin(PAGE_W, PAGE_H);
    probe.panel().setMono(Mono::Threshold);
    probe.panel().setPageTarget(buf, sizeof(buf));
    probe.panel().beginPage();

    // One tile, so the sampled row is a full page row.
    LGFXVirtualScreen vs(probe);
    vs.setSplitCount(1);
    vs.begin();
    vs.render(drawRamp);

    uint16_t mismatches = 0;
    uint16_t firstBadX = 0xFFFF;
    uint8_t firstGot = 0, firstWant = 0;
    for (uint16_t x = 0; x < probe.panel().lastGrayLen; ++x) {
      const uint8_t want = (uint8_t)((uint32_t)x * 255u / (PAGE_W - 1));
      if (probe.panel().lastGray[x] != want) {
        if (firstBadX == 0xFFFF) {
          firstBadX = x;
          firstGot = probe.panel().lastGray[x];
          firstWant = want;
        }
        ++mismatches;
      }
    }
    Serial.printf("#GRAY len=%u mismatches=%u firstBadX=%u got=%u want=%u\n",
           probe.panel().lastGrayLen, mismatches, firstBadX, firstGot, firstWant);
    reportCheck("gray_ramp_exact", mismatches == 0 && probe.panel().lastGrayLen == PAGE_W,
                "fp_copy delivers the drawn gray values");
  }

  //------------------------------------------------------------------ SPLIT
  {
    static const size_t LIMITS[] = {0, 64 * 1024, 19 * 1024, 8 * 1024, 4 * 1024};
    bool allMatch = true;
    for (size_t i = 0; i < sizeof(LIMITS) / sizeof(LIMITS[0]); ++i) {
      int tiles = 0;
      const bool ok = renderPage(LIMITS[i], Mono::Bayer4x4, g_page, sizeof(g_page), &tiles);
      if (i == 0) {
        memcpy(g_ref, g_page, sizeof(g_page));
      } else if (memcmp(g_ref, g_page, sizeof(g_page)) != 0) {
        allMatch = false;
      }
      Serial.printf("#SPLIT limit=%lu tiles=%d ok=%d row0=", (unsigned long)LIMITS[i], tiles,
             ok ? 1 : 0);
      hexRow(g_page, ROW_BYTES);
      Serial.printf("\n");
    }
    reportCheck("split_invariant", allMatch, "every memory limit produces identical bytes");
  }

  //------------------------------------------------------------------ BANDS
  {
    memset(g_banded, 0, sizeof(g_banded));
    BandCtx ctx{g_banded, 0, false};

    MonoSink sink;
    sink.begin(PAGE_W, PAGE_H);
    sink.panel().setMono(Mono::Bayer4x4);
    sink.panel().setBandTarget(bandSink, &ctx);

    LGFXVirtualScreen vs(sink);
    vs.setMemoryLimit(8 * 1024);
    vs.begin();
    vs.render(drawPage);

    // g_ref holds the Bayer4x4 full-page render from the SPLIT block.
    const bool same = memcmp(g_ref, g_banded, sizeof(g_banded)) == 0;
    Serial.printf("#BANDS rows=%u overflow=%d\n", ctx.rows, ctx.overflow ? 1 : 0);
    reportCheck("bands_equal_page", same && !ctx.overflow && ctx.rows == PAGE_H,
                "band output concatenates to the full page");
  }

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}
