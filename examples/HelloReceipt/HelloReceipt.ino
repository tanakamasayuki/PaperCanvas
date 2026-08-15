// HelloReceipt — the smallest useful receipt.
//
// Builds a receipt and shows it on the M5 screen. There is no printer here on
// purpose: PaperCanvas stops at the bitmap, and what you do with those bytes is
// a separate library's job. Previewing on the screen is a real workflow — it is
// how you check a layout without spending paper.
//
// The one thing worth reading twice is `showBitmap()`: it is the whole contract
// with whatever comes next. bit = 1 is black, MSB first, rows start on a byte
// boundary every `rowBytes`. A printer driver reads the same bytes the same way.

#include <M5Unified.h>
#include <PaperCanvas.h>

using PaperCanvas::Align;
using PaperCanvas::Bitmap;
using PaperCanvas::Receipt;

// 58 mm paper at 203 dpi. Most small thermal printers are 384 dots wide; check
// your printer's spec, it is usually given in dots rather than millimetres.
static constexpr uint16_t PRINT_WIDTH = 384;

// Enough for a receipt about 600 px tall. bufferSize() tells you the exact
// figure once the content is in, so you can also allocate after building.
static uint8_t page[PaperCanvas::rowBytes(PRINT_WIDTH) * 600];

static Receipt receipt(PRINT_WIDTH);

static void buildReceipt() {
  receipt.setFont(&fonts::efontJA_16);
  receipt.setMargin(8, 16, 4, 4);  // top, bottom, left, right

  receipt.setAlign(Align::Center);
  receipt.addText("PaperCanvas Cafe");
  receipt.addText("2026-08-15 14:32");
  receipt.addSpace(8);

  receipt.setAlign(Align::Left);
  receipt.addRule('-');

  // The second cell is right-aligned against the margin, and stays there no
  // matter how long the name is. Padding with spaces cannot do that.
  receipt.addRow("Coffee", "480");
  receipt.addRow("Sandwich", "620");
  receipt.addRow("Cheesecake", "480");

  receipt.addLine(2);
  receipt.addRow("Total", "1580");
  receipt.addSpace(8);

  receipt.setAlign(Align::Center);
  receipt.addText("Thank you");
}

// Draw the 1bpp page on screen, one pixel per dot, so you can see what the
// printer would print.
static void showBitmap(const Bitmap& bmp, int32_t x, int32_t y) {
  M5.Display.fillRect(x, y, bmp.width, bmp.height, TFT_WHITE);
  for (uint16_t row = 0; row < bmp.height; ++row) {
    const uint8_t* line = bmp.data + (size_t)row * bmp.rowBytes;
    for (uint16_t col = 0; col < bmp.width; ++col) {
      // bit = 1 is black; bit 7 of a byte is its leftmost pixel.
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

  buildReceipt();

  // height() is answerable before build(): every element resolved its height
  // when it was added. Useful for deciding how much paper to feed.
  Serial.printf("%u x %u, %u bytes\n", receipt.width(), receipt.height(),
                (unsigned)receipt.bufferSize());

  if (!receipt.build(page, sizeof(page))) {
    M5.Display.setTextColor(TFT_RED);
    M5.Display.drawString("build failed", 4, 4);
    return;
  }

  // Anything the layout could not honour is reported here rather than by
  // failing: a receipt that comes out slightly wrong still prints.
  if (receipt.warnings()) {
    Serial.printf("warnings: 0x%04x\n", receipt.warnings());
  }

  Bitmap bmp;
  bmp.data = page;
  bmp.width = receipt.width();
  bmp.height = receipt.height();
  bmp.rowBytes = PaperCanvas::rowBytes(receipt.width());
  showBitmap(bmp, 0, 0);

  // Next step in a real application: hand `page` to a printer driver.
  // The layout above does not change when you do.
}

void loop() { M5.delay(100); }
