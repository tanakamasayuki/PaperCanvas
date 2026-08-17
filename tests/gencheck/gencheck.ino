// gencheck — does the tool's output actually compile and run?
//
// The browser tool emits a C++ header (docs/WEB_TOOL.ja.md §2). A generator can
// produce something that reads correctly and does not compile, or compiles and
// lays out nothing — and neither shows up in the tool's own preview, because
// the preview never touches this code.
//
// So a representative generated header is committed here and exercised: it must
// compile, build a page, and produce the height the tool predicted.
//
// MyReceipt.h is generated output. Regenerate it from the tool rather than
// editing it by hand.

#include <LovyanGFX.hpp>
#include <PaperCanvas.h>

#include "MyLabel.h"
#include "MyReceipt.h"

static constexpr uint16_t W = 384;
static uint8_t g_page[PaperCanvas::rowBytes(W) * 800];

void setup() {
  Serial.begin(115200);
  Serial.printf("TEST start gencheck\n");

  // The item count is the caller's, not the tool's: this is the whole point of
  // the generated table. Three items where the tool previewed two.
  MyReceiptItem items[] = {
      {"コーヒー", "x2", "960"},
      {"サンドイッチ", "x1", "620"},
      {"とても長い商品名のテスト", "x1", "120"},
  };
  MyReceiptData d = {"PaperCanvas Cafe", "2026-08-17 14:32", "1580", items,
                     sizeof(items) / sizeof(items[0])};

  PaperCanvas::Receipt r(W);
  buildMyReceipt(r, d);

  const bool ok = r.build(g_page, sizeof(g_page));
  Serial.printf("#GEN items=%u height=%u count=%lu ok=%d warn=0x%04x\n",
                (unsigned)d.itemCount, r.height(), (unsigned long)r.count(), ok ? 1 : 0,
                r.warnings());

  // One row per item, so removing an item must shorten the page by exactly one
  // row. If it did not, the loop is not doing what the tool showed.
  PaperCanvas::Receipt r2(W);
  MyReceiptData d2 = d;
  d2.itemCount = 2;
  buildMyReceipt(r2, d2);
  r2.build(g_page, sizeof(g_page));
  Serial.printf("#GEN2 items=2 height=%u\n", r2.height());

  // The label side of the generator: a fixed canvas, no repetition, but the
  // same struct-and-build-function shape.
  MyLabelData ld = {"産地直送", "トマト", "1kg", "580", "2026-09-01"};
  PaperCanvas::Label lb(400, 240);
  buildMyLabel(lb, ld);
  static uint8_t labelPage[PaperCanvas::rowBytes(400) * 240];
  const bool lok = lb.build(labelPage, sizeof(labelPage));
  Serial.printf("#GENLABEL h=%u count=%lu ok=%d warn=0x%04x\n", lb.height(),
                (unsigned long)lb.count(), lok ? 1 : 0, lb.warnings());

  Serial.printf("#DONE\n");
  Serial.printf("TEST done\n");
}

void loop() {}
