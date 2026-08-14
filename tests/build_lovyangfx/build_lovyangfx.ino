// build_lovyangfx — the library compiles and works with LovyanGFX alone.
//
// Every other test includes <LovyanGFX.hpp> first, which is not how a caller
// reaches for a header. This one includes <PaperCanvas.h> on its own to prove it
// pulls in what it needs, and exercises one of each element type so a header
// that compiles but has an unusable API still fails.

#include <PaperCanvas.h>

#include <stdio.h>
#include <string.h>

using PaperCanvas::Align;
using PaperCanvas::Label;
using PaperCanvas::Receipt;
using PaperCanvas::Rect;

static constexpr uint16_t W = 200;
static uint8_t g_page[PaperCanvas::rowBytes(W) * 300];
static uint8_t g_img[16 * 8];

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start build_lovyangfx");
  memset(g_img, 0x80, sizeof(g_img));

  Receipt r(W);
  r.setFont(&fonts::Font2);
  r.setMargin(4, 4, 4, 4);
  r.setAlign(Align::Center);
  r.addText("Receipt");
  r.addSpace(4);
  r.addRule('-');
  r.addRow("Item", "100");
  r.addRow("Item", "x2", "200");
  r.addLine(2);
  r.addImage(g_img, 16, 8);
  const bool receiptOk = r.build(g_page, sizeof(g_page));
  Serial.printf("RECEIPT h=%u count=%lu ok=%d\n", r.height(), (unsigned long)r.count(),
                receiptOk ? 1 : 0);

  Label lb(W, 120);
  lb.setFont(&fonts::Font2);
  lb.addRect(Rect{0, 0, W, 120}, false, 1);
  lb.addText(Rect{4, 4, 192, 20}, "Label");
  lb.addRow(Rect{4, 30, 192, 20}, "Item", "100");
  lb.addImage(Rect{4, 56, 60, 40}, g_img, 16, 8);
  lb.addLine(4, 100, 196, 100);
  const bool labelOk = lb.build(g_page, sizeof(g_page));
  Serial.printf("LABEL h=%u count=%lu ok=%d\n", lb.height(), (unsigned long)lb.count(),
                labelOk ? 1 : 0);

  // Both output paths, so neither is left uncompiled.
  const bool streamOk = r.stream([](const PaperCanvas::Bitmap&, uint16_t, void*) {}, nullptr);
  Serial.printf("STREAM ok=%d\n", streamOk ? 1 : 0);

  Serial.println(receiptOk && labelOk && streamOk ? "RESULT ok" : "RESULT failed");
  Serial.println("TEST done");
}

void loop() {}
