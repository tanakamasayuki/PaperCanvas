// barcode — the three things that decide whether a printed symbol scans.
//
// PaperCanvas does not encode barcodes; BarcodeKit does. What PaperCanvas owns
// is the placement, and placement is where a barcode stops being readable:
//
//   whole-number module width — a fractional scale makes some modules a pixel
//     wider than others, and a scanner reads the uneven widths as a different
//     symbol. This is the most common way a generated barcode fails.
//   quiet zone — without the margin the scanner cannot find the symbol's edges.
//   guard bars — EAN/UPC guards run below the data bars; that step is what the
//     scanner uses to locate the ends.
//
// Each is checked from the rendered page, plus the case that must not print at
// all: a symbol too small to be read.
//
// The pages are written to output/*.pbm so the Python side can decode them with
// zxing-cpp, which is as close to a real scanner as a host test gets.

#include <LovyanGFX.hpp>
#include <PaperCanvas.h>
#include <PaperCanvasBarcode.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ink.h"

using PaperCanvas::Align;
using PaperCanvas::BarcodeLayout;
using PaperCanvas::BarcodeOptions;
using PaperCanvas::Label;
using PaperCanvas::Receipt;
using PaperCanvas::Rect;

static constexpr uint16_t PAGE_W = 384;
static constexpr uint16_t ROW_BYTES = PaperCanvas::rowBytes(PAGE_W);
static constexpr uint16_t MAX_H = 400;
static uint8_t g_page[ROW_BYTES * MAX_H];
static uint8_t g_ref[ROW_BYTES * MAX_H];

static uint8_t g_bcbuf[BarcodeKit::Code128::bufferSize(24)];
static uint8_t g_eanbuf[BarcodeKit::EAN13::bufferSize()];
static uint8_t g_qrbuf[BarcodeKit::QRCode::bufferSize(6)];

static void reportCheck(const char* name, bool ok, const char* note) {
  Serial.printf("#CHECK name=%s ok=%d note=%s\n", name, ok ? 1 : 0, note);
}

static void savePbm(const char* path, const uint8_t* page, uint16_t w, uint16_t h) {
  FILE* f = fopen(path, "wb");
  if (!f) { return; }
  fprintf(f, "P4\n%u %u\n", w, h);
  fwrite(page, 1, (size_t)PaperCanvas::rowBytes(w) * h, f);
  fclose(f);
}

// Widths of the runs of identical pixels along a row. For a barcode drawn at a
// whole-number scale every run must be a multiple of that scale; a fractional
// scale shows up here as runs that are not.
static bool runsAreMultiplesOf(const uint8_t* page, uint16_t y, uint16_t from, uint16_t to,
                               uint16_t scale, uint16_t* badRun) {
  uint16_t run = 0;
  int prev = -1;
  for (uint16_t x = from; x < to; ++x) {
    const int bit = (page[(size_t)y * ROW_BYTES + (x >> 3)] >> (7 - (x & 7))) & 1;
    if (bit == prev) {
      ++run;
    } else {
      if (prev >= 0 && run % scale) {
        *badRun = run;
        return false;
      }
      prev = bit;
      run = 1;
    }
  }
  if (prev >= 0 && run % scale) {
    *badRun = run;
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start barcode\n");
  mkdir("output", 0755);

  BarcodeKit::Code128 bc;
  const bool encoded = (bool)bc.encode("PAPERCANVAS-1", g_bcbuf, sizeof(g_bcbuf));
  Serial.printf("#ENCODE code128=%d modules=%u qL=%u qR=%u\n", encoded ? 1 : 0, bc.width(),
                bc.quietLeft(), bc.quietRight());
  reportCheck("encode_ok", encoded, "BarcodeKit encoded the sample");

  //-------------------------------------------------- layout is whole-number
  {
    BarcodeOptions opt;
    opt.barHeight = 60;
    const BarcodeLayout l = PaperCanvas::barcodeLayout(bc, opt, PAGE_W);
    const uint16_t modules = (uint16_t)(bc.width() + bc.quietLeft() + bc.quietRight());
    Serial.printf("#LAYOUT scale=%u width=%u height=%u qL=%u fits=%d modules=%u\n", l.scale,
                  l.width, l.height, l.quietL, l.fits ? 1 : 0, modules);
    reportCheck("layout_fits", l.fits, "the sample fits the printable width");
    reportCheck("layout_whole_scale", l.scale >= 1 && l.width == modules * l.scale,
                "width is an exact multiple of the module scale");
    reportCheck("layout_largest_scale", (uint16_t)((l.scale + 1) * modules) > PAGE_W,
                "the chosen scale is the largest whole one that fits");
    reportCheck("layout_quiet_zone", l.quietL == bc.quietLeft() * l.scale,
                "the quiet zone is scaled with the modules");
  }

  //----------------------------- whole-module runs, at a scale above one
  // The sample above is long enough that only one pixel per module fits, and at
  // scale 1 "every run is a multiple of the scale" is true of any image at all.
  // A short symbol gives a scale of several pixels, where the check has teeth.
  {
    BarcodeKit::Code128 shortBc;
    static uint8_t shortBuf[BarcodeKit::Code128::bufferSize(8)];
    shortBc.encode("PC-1", shortBuf, sizeof(shortBuf));

    BarcodeOptions opt;
    opt.barHeight = 40;
    opt.align = Align::Left;
    const BarcodeLayout l = PaperCanvas::barcodeLayout(shortBc, opt, PAGE_W);
    Receipt r(PAGE_W);
    r.addBarcode(shortBc, opt);
    const uint16_t h = r.height();
    r.build(g_page, sizeof(g_page));
    savePbm("output/code128_short.pbm", g_page, PAGE_W, h);

    uint16_t bad = 0;
    const bool multiples =
        runsAreMultiplesOf(g_page, (uint16_t)(h / 2), l.quietL,
                           (uint16_t)(l.width - l.quietR), l.scale, &bad);
    Serial.printf("#SHORT scale=%u width=%u runsOk=%d badRun=%u\n", l.scale, l.width,
                  multiples ? 1 : 0, bad);
    reportCheck("short_scale_above_one", l.scale > 1,
                "a short symbol gets more than one pixel per module");
    reportCheck("short_runs_whole", multiples,
                "every bar and space is a whole number of modules wide");
  }

  //--------------------------------------------------------- rendered page
  uint16_t pageH = 0;
  uint16_t scale = 0;
  {
    BarcodeOptions opt;
    opt.barHeight = 60;
    opt.align = Align::Center;
    Receipt r(PAGE_W);
    const uint16_t h = r.addBarcode(bc, opt);
    pageH = r.height();
    const bool ok = r.build(g_page, sizeof(g_page));
    savePbm("output/code128.pbm", g_page, PAGE_W, pageH);
    memcpy(g_ref, g_page, sizeof(g_page));

    const BarcodeLayout l = PaperCanvas::barcodeLayout(bc, opt, PAGE_W);
    scale = l.scale;
    Serial.printf("#PAGE h=%u added=%u ok=%d warn=0x%04x\n", pageH, h, ok ? 1 : 0,
                  r.warnings());
    reportCheck("page_builds", ok && h > 0, "a receipt with a barcode generates");
    reportCheck("page_height_is_layout", pageH == l.height,
                "the stacked height is the barcode's own height");

    // Quiet zone: the leftmost ink must start after the margin, not at 0.
    const Ink s = inkSpan(g_page, ROW_BYTES, PAGE_W, 0, pageH);
    Serial.printf("#QUIET first=%d last=%d quietPx=%u\n", s.first, s.last, l.quietL);
    const int16_t barsStart = (int16_t)((PAGE_W - l.width) / 2 + l.quietL);
    reportCheck("quiet_zone_left_blank", s.first >= barsStart,
                "no ink inside the left quiet zone");
    reportCheck("quiet_zone_right_blank",
                s.last <= (int16_t)((PAGE_W - l.width) / 2 + l.width - l.quietR) - 1,
                "no ink inside the right quiet zone");

    // Module widths: every run on a bar row must be a multiple of the scale.
    uint16_t bad = 0;
    const bool multiples =
        runsAreMultiplesOf(g_page, (uint16_t)(pageH / 2), (uint16_t)barsStart,
                           (uint16_t)(barsStart + l.width - l.quietL - l.quietR), l.scale,
                           &bad);
    Serial.printf("#RUNS scale=%u ok=%d badRun=%u\n", l.scale, multiples ? 1 : 0, bad);
    reportCheck("module_runs_whole", multiples,
                "every bar and space is a whole number of modules wide");
  }

  //----------------------------------------------------------- guard bars
  // EAN-13's guards run below the data bars. Compare the ink height at a guard
  // column against a data column.
  {
    BarcodeKit::EAN13 ean;
    const bool ok = (bool)ean.encode("490123456789", g_eanbuf, sizeof(g_eanbuf));
    reportCheck("ean_encode_ok", ok, "EAN-13 encoded");

    BarcodeOptions opt;
    opt.barHeight = 60;
    opt.quietZone = true;
    Receipt r(PAGE_W);
    r.addBarcode(ean, opt);
    const uint16_t h = r.height();
    r.build(g_page, sizeof(g_page));
    savePbm("output/ean13.pbm", g_page, PAGE_W, h);

    const BarcodeLayout l = PaperCanvas::barcodeLayout(ean, opt, PAGE_W);
    const int16_t left = (int16_t)((PAGE_W - l.width) / 2);

    // Column 0 of the symbol is a guard; find a data column that is black.
    uint16_t guardX = 0, dataX = 0;
    for (uint16_t mx = 0; mx < ean.width(); ++mx) {
      if (!ean.module(mx, 0)) { continue; }
      const uint16_t px = (uint16_t)(left + l.quietL + mx * l.scale);
      if (ean.barExtends(mx)) {
        if (!guardX) { guardX = px; }
      } else if (!dataX) {
        dataX = px;
      }
    }
    uint16_t guardBottom = 0, dataBottom = 0;
    for (uint16_t y = 0; y < h; ++y) {
      if ((g_page[(size_t)y * ROW_BYTES + (guardX >> 3)] >> (7 - (guardX & 7))) & 1) {
        guardBottom = y;
      }
      if ((g_page[(size_t)y * ROW_BYTES + (dataX >> 3)] >> (7 - (dataX & 7))) & 1) {
        dataBottom = y;
      }
    }
    Serial.printf("#GUARD guardX=%u dataX=%u guardBottom=%u dataBottom=%u extra=%u\n", guardX,
                  dataX, guardBottom, dataBottom, l.guardExtra);
    reportCheck("guard_bars_extend", guardBottom > dataBottom,
                "guard bars run below the data bars");
  }

  //--------------------------------------------------------------- QR code
  {
    BarcodeKit::QRCode qr;
    const bool ok = (bool)qr.encode("https://example.com/", g_qrbuf, sizeof(g_qrbuf));
    reportCheck("qr_encode_ok", ok, "QR encoded");

    BarcodeOptions opt;
    Label lb(PAGE_W, 200);
    const bool placed = lb.addBarcode(Rect{0, 0, PAGE_W, 200}, qr, opt);
    lb.build(g_page, sizeof(g_page));
    savePbm("output/qr.pbm", g_page, PAGE_W, 200);
    const BarcodeLayout l = PaperCanvas::barcodeLayout(qr, opt, PAGE_W, 200);
    Serial.printf("#QR modules=%u scale=%u width=%u height=%u placed=%d\n", qr.width(),
                  l.scale, l.width, l.height, placed ? 1 : 0);
    reportCheck("qr_placed", placed, "a QR code is placed on a label");
    reportCheck("qr_square", l.width - l.quietL - l.quietR ==
                                 l.height - l.quietT - l.quietB,
                "a QR symbol comes out square");
  }

  //------------------------------------------------ too small: draw nothing
  // A barcode squeezed below one pixel per module cannot be read, and printing
  // it anyway hides that fact until someone tries to scan it.
  {
    Receipt r(20);  // far too narrow for the symbol
    const uint16_t h = r.addBarcode(bc, BarcodeOptions{});
    Serial.printf("#TOOSMALL added=%u count=%lu warn=0x%04x\n", h, (unsigned long)r.count(),
                  r.warnings());
    reportCheck("too_small_not_drawn", h == 0 && r.count() == 0,
                "a barcode that cannot be read is not added at all");
    reportCheck("too_small_warned",
                (r.warnings() & PaperCanvas::Warning_BarcodeTooSmall) != 0,
                "an unplaceable barcode raises BarcodeTooSmall");
  }

  //--------------------------------------------------------- split invariance
  {
    static const size_t LIMITS[] = {0, 16 * 1024, 8 * 1024, 4 * 1024};
    bool allMatch = true;
    for (size_t i = 0; i < sizeof(LIMITS) / sizeof(LIMITS[0]); ++i) {
      BarcodeOptions opt;
      opt.barHeight = 60;
      opt.align = Align::Center;
      Receipt r(PAGE_W);
      r.addBarcode(bc, opt);
      r.setMemoryLimit(LIMITS[i]);
      const bool ok = r.build(g_page, sizeof(g_page));
      if (memcmp(g_ref, g_page, (size_t)ROW_BYTES * pageH) != 0) { allMatch = false; }
      Serial.printf("#SPLIT limit=%lu ok=%d\n", (unsigned long)LIMITS[i], ok ? 1 : 0);
    }
    reportCheck("split_invariant", allMatch, "barcodes do not depend on the tile count");
  }

  (void)scale;
  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}
