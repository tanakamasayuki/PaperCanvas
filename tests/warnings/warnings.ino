// warnings — each flag under the condition it names, and under nothing else.
//
// A diagnostic that fires when it should not is worse than none: callers stop
// reading it. So every case here asserts the exact flag word, not just that the
// expected bit is set — a stray bit fails the same as a missing one.
//
// The other half of the contract is that warnings do not stop generation
// (docs/DECISIONS.ja.md D11): a page with every warning raised must still come
// out, because one mistyped rectangle should not cost the whole print.

#include <LovyanGFX.hpp>
#include <PaperCanvas.h>
#include <PaperCanvasBarcode.h>

#include <stdio.h>
#include <string.h>

using PaperCanvas::Align;
using PaperCanvas::BarcodeOptions;
using PaperCanvas::Fit;
using PaperCanvas::ImageOptions;
using PaperCanvas::Label;
using PaperCanvas::Receipt;
using PaperCanvas::Rect;
using PaperCanvas::TextOptions;

static constexpr uint16_t W = 200;
static constexpr uint16_t H = 120;
static constexpr uint16_t ROW_BYTES = PaperCanvas::rowBytes(W);
static uint8_t g_page[ROW_BYTES * H];

static constexpr uint16_t SRC_W = 40;
static constexpr uint16_t SRC_H = 20;
static uint8_t g_img[SRC_W * SRC_H];
static uint8_t g_bcbuf[BarcodeKit::Code128::bufferSize(16)];

static void reportCheck(const char* name, bool ok, const char* note) {
  Serial.printf("#CHECK name=%s ok=%d note=%s\n", name, ok ? 1 : 0, note);
}

static void expectWarn(const char* name, uint16_t got, uint16_t want) {
  Serial.printf("#WARN case=%s got=0x%04x want=0x%04x\n", name, got, want);
  reportCheck(name, got == want, "the exact warning word");
}

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start warnings\n");
  memset(g_img, 0, sizeof(g_img));

  //---------------------------------------------------------------- quiet cases
  // Things that fit must raise nothing at all.
  {
    Receipt r(W);
    r.setFont(&fonts::Font2);
    r.setWrap(false);
    r.addText("Short");
    r.addSpace(4);
    r.addLine(1);
    r.addRule('-');
    expectWarn("quiet_receipt", r.warnings(), PaperCanvas::Warning_None);
  }
  {
    Label lb(W, H);
    lb.setFont(&fonts::Font2);
    lb.addText(Rect{10, 10, 180, 20}, "Short");
    lb.addRect(Rect{0, 0, W, H}, false, 1);
    expectWarn("quiet_label", lb.warnings(), PaperCanvas::Warning_None);
  }
  {
    // An image at its natural size inside a box that holds it.
    Label lb(W, H);
    ImageOptions io;
    io.fit = Fit::None;
    lb.addImage(Rect{10, 10, 100, 60}, g_img, SRC_W, SRC_H, io);
    expectWarn("quiet_image", lb.warnings(), PaperCanvas::Warning_None);
  }

  //-------------------------------------------------------------- TextWrapped
  {
    Receipt r(W);
    r.setFont(&fonts::Font2);
    r.setWrap(true);
    r.addText("A line long enough that it must be broken across two lines here");
    expectWarn("text_wrapped", r.warnings(), PaperCanvas::Warning_TextWrapped);
  }

  //-------------------------------------------------------------- TextClipped
  {
    Receipt r(W);
    r.setFont(&fonts::Font2);
    r.setWrap(false);
    r.addText("A line long enough that it must be broken across two lines here");
    expectWarn("text_clipped", r.warnings(), PaperCanvas::Warning_TextClipped);
  }

  //-------------------------------------------------------------- ImageScaled
  {
    Label lb(W, H);
    ImageOptions io;
    io.fit = Fit::Scale;
    io.scale = 0.5f;
    lb.addImage(Rect{0, 0, 100, 60}, g_img, SRC_W, SRC_H, io);
    expectWarn("image_scaled", lb.warnings(), PaperCanvas::Warning_ImageScaled);
  }

  //------------------------------------------------------------- ImageClipped
  {
    Label lb(W, H);
    ImageOptions io;
    io.fit = Fit::None;
    lb.addImage(Rect{0, 0, 10, 10}, g_img, SRC_W, SRC_H, io);
    expectWarn("image_clipped", lb.warnings(), PaperCanvas::Warning_ImageClipped);
  }

  //-------------------------------------------------------------- OutOfBounds
  {
    Label lb(W, H);
    lb.setFont(&fonts::Font2);
    lb.addRect(Rect{(int16_t)(W - 5), 0, 50, 10}, true);
    expectWarn("out_of_bounds", lb.warnings(), PaperCanvas::Warning_OutOfBounds);
  }
  {
    Label lb(W, H);
    lb.addRect(Rect{-5, 0, 50, 10}, true);
    expectWarn("out_of_bounds_negative", lb.warnings(), PaperCanvas::Warning_OutOfBounds);
  }

  //---------------------------------------------------------- BarcodeTooSmall
  {
    BarcodeKit::Code128 bc;
    bc.encode("HELLO-WORLD", g_bcbuf, sizeof(g_bcbuf));
    Receipt r(24);  // narrower than the symbol needs at one pixel per module
    r.addBarcode(bc, BarcodeOptions{});
    expectWarn("barcode_too_small", r.warnings(), PaperCanvas::Warning_BarcodeTooSmall);
  }
  {
    // The same barcode with room raises nothing.
    BarcodeKit::Code128 bc;
    bc.encode("HELLO-WORLD", g_bcbuf, sizeof(g_bcbuf));
    Receipt r(384);
    BarcodeOptions opt;
    opt.barHeight = 40;
    r.addBarcode(bc, opt);
    expectWarn("barcode_fits_quiet", r.warnings(), PaperCanvas::Warning_None);
  }

  //---------------------------------------------------------- clearWarnings()
  {
    Receipt r(W);
    r.setFont(&fonts::Font2);
    r.setWrap(false);
    r.addText("A line long enough that it must be broken across two lines here");
    const bool raised = r.warnings() != 0;
    r.clearWarnings();
    Serial.printf("#CLEARWARN raised=%d after=0x%04x\n", raised ? 1 : 0, r.warnings());
    reportCheck("clear_warnings", raised && r.warnings() == 0,
                "clearWarnings() resets the accumulated flags");
  }

  //---------------------------------------------- warnings never stop the build
  // Everything wrong at once. The page must still come out: what a caller does
  // with a receipt that failed to generate is nothing, and a partly-wrong
  // receipt is at least readable.
  {
    Label lb(W, H);
    lb.setFont(&fonts::Font2);
    lb.setWrap(false);
    lb.addText(Rect{10, 10, 40, 20}, "Far too long for forty pixels");
    ImageOptions io;
    io.fit = Fit::Scale;
    io.scale = 0.25f;
    lb.addImage(Rect{0, 40, 10, 10}, g_img, SRC_W, SRC_H, io);
    lb.addRect(Rect{(int16_t)(W - 5), 60, 50, 10}, true);
    const bool ok = lb.build(g_page, sizeof(g_page));
    const uint16_t w = lb.warnings();
    Serial.printf("#ALLWARN ok=%d warn=0x%04x count=%lu\n", ok ? 1 : 0, w,
                  (unsigned long)lb.count());
    reportCheck("warnings_do_not_stop_build", ok, "a page with warnings still generates");
    reportCheck("warnings_accumulate",
                (w & PaperCanvas::Warning_TextClipped) &&
                    (w & PaperCanvas::Warning_ImageScaled) &&
                    (w & PaperCanvas::Warning_OutOfBounds),
                "warnings accumulate rather than replacing each other");
    reportCheck("warned_elements_kept", lb.count() == 3,
                "an element that raised a warning is still on the page");
  }

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}
