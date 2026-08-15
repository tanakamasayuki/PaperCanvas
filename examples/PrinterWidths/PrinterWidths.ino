// PrinterWidths — matching the paper you actually have.
//
// The one number you must get right is the printable width in dots. Everything
// else follows from it: too small and you waste paper, too large and the right
// edge is cut off by the printer with no warning from anywhere.
//
// It is usually in the printer's spec sheet as dots, not millimetres, because
// the paper is wider than the printable area. 58 mm paper at 203 dpi works out
// to 463 dots, but almost every 58 mm printer prints 384 — the rest is margin.
// So prefer the number the manufacturer gives, and use mmToPx() only when you
// genuinely have a physical measurement.
//
// This sketch renders the same content at several widths so the difference is
// visible, and shows what the library reports when something does not fit.

#include <M5Unified.h>
#include <PaperCanvas.h>

using PaperCanvas::Align;
using PaperCanvas::Bitmap;
using PaperCanvas::Receipt;

static constexpr uint16_t DPI = 203;
static constexpr uint16_t MAX_W = 576;
static constexpr uint16_t MAX_H = 220;
static uint8_t page[PaperCanvas::rowBytes(MAX_W) * MAX_H];

struct Paper {
  const char* name;
  uint16_t dots;  ///< printable width from the printer's spec
};

// Common small thermal printers. Check yours; these are typical, not universal.
static const Paper PAPERS[] = {
    {"58mm", 384},
    {"80mm", 576},
    {"58mm narrow", 320},
    {"tiny", 200},
};

static void showBitmap(const Bitmap& bmp, int32_t x, int32_t y) {
  M5.Display.fillRect(x, y, bmp.width, bmp.height, TFT_WHITE);
  for (uint16_t row = 0; row < bmp.height; ++row) {
    const uint8_t* line = bmp.data + (size_t)row * bmp.rowBytes;
    for (uint16_t col = 0; col < bmp.width; ++col) {
      if ((line[col >> 3] >> (7 - (col & 7))) & 1) {
        M5.Display.drawPixel(x + col, y + row, TFT_BLACK);
      }
    }
  }
}

// The same content every time. Only the width changes.
static void compose(Receipt& r) {
  r.setFont(&fonts::efontJA_16);
  r.setMargin(4, 8, 4, 4);
  r.setAlign(Align::Center);
  r.addText("PaperCanvas Cafe");
  r.setAlign(Align::Left);
  r.addRule('-');
  r.addRow("Coffee", "480");
  r.addRow("Extra large sandwich", "620");
  r.addLine(2);
  r.addRow("Total", "1100");
}

// Not named `index`: <string.h> declares index(), and a global by that name
// collides with it in a way that fails to compile.
static size_t paperIndex = 0;

static void render() {
  const Paper& paper = PAPERS[paperIndex];
  Receipt r(paper.dots);
  r.setWrap(false);  // so an over-long name is reported rather than rewrapped
  compose(r);

  M5.Display.fillScreen(TFT_DARKGREY);
  const bool ok = r.build(page, sizeof(page));

  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setCursor(0, MAX_H + 4);
  M5.Display.printf("%s: %u dots = %.1f mm\n", paper.name, paper.dots,
                    PaperCanvas::pxToMm(paper.dots, DPI));
  M5.Display.printf("height %u px, %u bytes\n", r.height(), (unsigned)r.bufferSize());
  M5.Display.printf("warnings 0x%04x %s\n", r.warnings(), ok ? "" : "(build failed)");

  // TextClipped at the narrow widths: "Extra large sandwich" no longer fits its
  // column. The receipt is still produced — that is the policy, so one long
  // product name never costs you the whole print.
  if (r.warnings() & PaperCanvas::Warning_TextClipped) {
    M5.Display.println("a name was cut to fit");
  }

  if (ok) {
    Bitmap bmp;
    bmp.data = page;
    bmp.width = r.width();
    bmp.height = r.height();
    bmp.rowBytes = PaperCanvas::rowBytes(r.width());
    showBitmap(bmp, 0, 0);
  }

  Serial.printf("%s dots=%u height=%u warn=0x%04x\n", paper.name, paper.dots, r.height(),
                r.warnings());
}

void setup() {
  M5.begin();
  M5.Display.setRotation(1);
  render();
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) {
    paperIndex = (paperIndex + 1) % (sizeof(PAPERS) / sizeof(PAPERS[0]));
    render();
  }
  M5.delay(10);
}
