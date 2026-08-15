// StreamBands — generating a long receipt without holding it in memory.
//
// build() gives you the whole page at once, which needs rowBytes * height bytes.
// A 384 px receipt 4000 px long is 192 KB — fine with PSRAM, not fine without.
//
// stream() hands you the page one band at a time instead. Memory then depends
// on the tile budget, not on how long the receipt is: the 4000 px receipt below
// uses the same working memory as a 200 px one.
//
// The bands are exactly the bytes build() would have produced, in order. That
// is checked in the test suite, not just asserted here — you can concatenate
// them and get the same page.

#include <M5Unified.h>
#include <PaperCanvas.h>

using PaperCanvas::Align;
using PaperCanvas::Bitmap;
using PaperCanvas::Receipt;

static constexpr uint16_t PRINT_WIDTH = 384;

// Note what is *not* here: a page buffer. Streaming never allocates one.
static Receipt receipt(PRINT_WIDTH);

// Stand-in for a printer. A real one would push these bytes down BLE or serial;
// the point is that it only ever sees one band at a time.
struct Printer {
  uint32_t rows = 0;
  uint32_t blackDots = 0;
};

static void sendBand(const Bitmap& band, uint16_t y, void* ctx) {
  Printer* p = (Printer*)ctx;
  ++p->rows;
  for (uint16_t b = 0; b < band.rowBytes; ++b) {
    p->blackDots += __builtin_popcount(band.data[b]);
  }
  // A real driver would write band.data here. `y` is the absolute page row, so
  // a driver that needs positioning has it. The data is only valid during this
  // call — copy it if you need it later.
  (void)y;
}

void setup() {
  M5.begin();
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);

  receipt.setFont(&fonts::efontJA_16);
  receipt.setMargin(8, 16, 4, 4);

  // A deliberately long receipt: 100 line items.
  receipt.setAlign(Align::Center);
  receipt.addText("Long receipt");
  receipt.setAlign(Align::Left);
  receipt.addRule('-');
  for (int i = 1; i <= 100; ++i) {
    char name[32], price[16];
    snprintf(name, sizeof(name), "Item %d", i);
    snprintf(price, sizeof(price), "%d", 100 + i);
    // The strings are copied when they are added, so these stack buffers going
    // out of scope is fine.
    receipt.addRow(name, price);
  }
  receipt.addLine(2);
  receipt.addRow("Total", "15050");

  // Cap the tile budget. This, not the receipt length, decides working memory.
  receipt.setMemoryLimit(16 * 1024);

  const uint16_t h = receipt.height();
  const size_t wholePage = receipt.bufferSize();

  Printer printer;
  const uint32_t before = ESP.getFreeHeap();
  const bool ok = receipt.stream(sendBand, &printer);
  const uint32_t after = ESP.getFreeHeap();

  M5.Display.setCursor(0, 0);
  M5.Display.printf("height     %u px\n", h);
  M5.Display.printf("whole page %u bytes\n", (unsigned)wholePage);
  M5.Display.printf("streamed   %u rows\n", (unsigned)printer.rows);
  M5.Display.printf("black dots %u\n", (unsigned)printer.blackDots);
  M5.Display.printf("heap delta %d\n", (int)(before - after));
  M5.Display.printf("%s\n", ok ? "ok" : "FAILED");

  Serial.printf("height=%u wholePage=%u rows=%u ok=%d\n", h, (unsigned)wholePage,
                (unsigned)printer.rows, ok ? 1 : 0);

  // printer.rows == h: every row of the page arrived exactly once.
}

void loop() { M5.delay(100); }
