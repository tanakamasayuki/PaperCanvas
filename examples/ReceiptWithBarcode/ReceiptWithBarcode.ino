// ReceiptWithBarcode — a receipt with a scannable barcode on it.
//
// PaperCanvas does not encode barcodes; BarcodeKit does. Including
// <PaperCanvasBarcode.h> is what brings BarcodeKit in — <PaperCanvas.h> alone
// does not depend on it.
//
// What PaperCanvas takes on is placement, and placement is where a barcode
// stops being readable:
//
//   * modules must be a whole number of pixels wide. At a fractional scale some
//     modules come out a pixel wider than others and a scanner reads the uneven
//     widths as a different symbol.
//   * the quiet zone must be left blank, or the scanner cannot find the ends.
//   * EAN/UPC guard bars must run below the data bars.
//
// You do not have to think about any of that. But you do have to check the
// return value: a barcode that cannot be drawn readably is not drawn at all.

#include <M5Unified.h>
#include <PaperCanvas.h>
#include <PaperCanvasBarcode.h>

using PaperCanvas::Align;
using PaperCanvas::BarcodeOptions;
using PaperCanvas::Bitmap;
using PaperCanvas::Receipt;

static constexpr uint16_t PRINT_WIDTH = 384;
static uint8_t page[PaperCanvas::rowBytes(PRINT_WIDTH) * 600];

// BarcodeKit wants the buffer from you and sizes it at compile time.
static uint8_t code128buf[BarcodeKit::Code128::bufferSize(20)];
static uint8_t ean13buf[BarcodeKit::EAN13::bufferSize()];

static Receipt receipt(PRINT_WIDTH);

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

void setup() {
  M5.begin();
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_DARKGREY);

  receipt.setFont(&fonts::efontJA_16);
  receipt.setMargin(8, 16, 4, 4);
  receipt.setAlign(Align::Center);
  receipt.addText("PaperCanvas Cafe");
  receipt.setAlign(Align::Left);
  receipt.addRule('-');
  receipt.addRow("Coffee", "480");
  receipt.addRow("Sandwich", "620");
  receipt.addLine(2);
  receipt.addRow("Total", "1100");
  receipt.addSpace(12);

  //--------------------------------------------------------------- Code 128
  BarcodeKit::Code128 order;
  if (!order.encode("T20260815-0042", code128buf, sizeof(code128buf))) {
    Serial.println("encode failed");
    return;
  }

  BarcodeOptions opt;
  opt.barHeight = 60;              // bar height in pixels
  opt.align = Align::Center;
  // opt.moduleWidth = 0 (default) picks the largest whole number of pixels per
  // module that fits the printable width. Set it if you need a specific size.

  // You can ask before committing. `fits == false` means it cannot be drawn at
  // even one pixel per module, so addBarcode would skip it.
  const auto layout = PaperCanvas::barcodeLayout(order, opt, receipt.width());
  Serial.printf("code128: %u modules/px, %u x %u, fits=%d\n", layout.scale, layout.width,
                layout.height, layout.fits ? 1 : 0);

  if (receipt.addBarcode(order, opt) == 0) {
    // Nothing was drawn. An unreadable barcode on a receipt is worse than a
    // gap, because nobody finds out until someone tries to scan it.
    Serial.println("barcode too small — nothing drawn");
  }

  //---------------------------------------------------------------- EAN-13
  // Twelve digits in, the check digit is computed for you. The guard bars will
  // be drawn taller than the data bars, which is what the standard requires.
  receipt.addSpace(8);
  BarcodeKit::EAN13 jan;
  if (jan.encode("490123456789", ean13buf, sizeof(ean13buf))) {
    BarcodeOptions janOpt;
    janOpt.barHeight = 50;
    janOpt.align = Align::Center;
    receipt.addBarcode(jan, janOpt);
    Serial.printf("ean13 text: %s\n", jan.text());  // includes the check digit
  }

  if (!receipt.build(page, sizeof(page))) {
    M5.Display.setTextColor(TFT_RED);
    M5.Display.drawString("build failed", 4, 4);
    return;
  }
  if (receipt.warnings() & PaperCanvas::Warning_BarcodeTooSmall) {
    Serial.println("a barcode was skipped");
  }

  Bitmap bmp;
  bmp.data = page;
  bmp.width = receipt.width();
  bmp.height = receipt.height();
  bmp.rowBytes = PaperCanvas::rowBytes(receipt.width());
  showBitmap(bmp, 0, 0);
}

void loop() { M5.delay(100); }
