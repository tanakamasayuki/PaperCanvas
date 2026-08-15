# PaperCanvas

> 日本語: [README.ja.md](README.ja.md)

Lays out receipt and label content and produces the **monochrome 1-bit bitmap** you hand to a printer.

> **In development.** The C++ library works and its tests pass, but nothing is released yet; 1.0.0 will be cut once the browser tool is ready too. See [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md) (Japanese) for where things stand.

## What it does

- **Page generation, and nothing else** — no connections, no transports, no device commands. Swap the printer and your layout code does not change
- **Two models, kept apart** — a receipt stacks downwards and grows with its content; a label places rectangles on a fixed canvas
- **Length does not cost memory** — pages can be streamed band by band. A hundred line items uses the same working memory as ten
- **The same input always gives the same output** — changing the internal tile count does not alter a single bit, and the test suite proves it
- **Columns line up** — no padding "Item … Price" with spaces. Column widths come from the layout, not from how long this row's text happens to be
- **Barcodes just get placed** — whole-number module width, quiet zone and guard bars are handled. If it cannot be read at that size, **it is not drawn**

## Installing

Through the Arduino IDE library manager, or from [Releases](https://github.com/tanakamasayuki/PaperCanvas/releases) once published.

Requires LovyanGFX (or M5GFX / M5Unified) and [LGFXVirtualCanvas](https://github.com/tanakamasayuki/LGFXVirtualCanvas).

## Using it

### A receipt

```cpp
#include <M5Unified.h>
#include <PaperCanvas.h>

PaperCanvas::Receipt r(384);        // printable width in dots; typical for 58 mm at 203 dpi

void makeReceipt() {
  r.setFont(&fonts::efontJA_16);
  r.setMargin(8, 16, 4, 4);

  r.setAlign(PaperCanvas::Align::Center);
  r.addText("Thank you for visiting");
  r.setAlign(PaperCanvas::Align::Left);
  r.addRule('-');

  r.addRow("Coffee",   "480");      // name on the left, figure hard against the right
  r.addRow("Sandwich", "620");
  r.addLine(2);
  r.addRow("Total", "1100");

  static uint8_t page[PaperCanvas::rowBytes(384) * 400];
  if (r.build(page, sizeof(page))) {
    sendToPrinter(page, r.width(), r.height());   // another library's job from here
  }
}
```

`height()` is answerable at any point while you build, which is handy when you need to know the paper feed in advance.

### A label

```cpp
PaperCanvas::Label lb(400, 240);    // 50 x 30 mm at 203 dpi

lb.setFont(&fonts::efontJA_16);
lb.addRect({0, 0, 400, 240}, false, 2);
lb.addText({8, 8, 384, 20}, "Farm fresh", {.align = PaperCanvas::Align::Center});
lb.addRow({8, 34, 384, 20}, "Tomato", "1kg", "580");
lb.addImage({8, 60, 120, 100}, logo, logoW, logoH, {.fit = PaperCanvas::Fit::Contain});

static uint8_t page[PaperCanvas::rowBytes(400) * 240];
lb.build(page, sizeof(page));
```

### Streaming a long receipt

The page is never held, so the number of line items does not change the working memory.

```cpp
r.setMemoryLimit(16 * 1024);        // this, not the receipt length, sets memory use
r.stream([](const PaperCanvas::Bitmap& band, uint16_t y, void*) {
  sendBand(band.data, band.rowBytes, y);
});
```

### Barcodes

```cpp
#include <PaperCanvasBarcode.h>     // including this is what adds the BarcodeKit dependency

uint8_t buf[BarcodeKit::Code128::bufferSize(20)];
BarcodeKit::Code128 bc;
bc.encode("T20260815-0042", buf, sizeof(buf));

if (r.addBarcode(bc, {.barHeight = 60}) == 0) {
  // It would not fit even at one pixel per module, so nothing was drawn.
}
```

The module width is always a whole number of pixels. At a fractional scale some modules come out a pixel wider than others and a scanner reads the uneven widths as a different symbol.

## Output format

```cpp
struct Bitmap {
  const uint8_t* data;
  uint16_t width, height, rowBytes;   // rowBytes = (width + 7) / 8
};
```

- **bit = 1 is black** (printed)
- **MSB first** — bit 7 of a byte is its leftmost pixel
- Rows start on a byte boundary; spare bits at the end of a row are 0

This is the same layout as the ESC/POS raster bit image (`GS v 0`), so most drivers can pass it through unchanged. Device-specific conversion is still deliberately somebody else's job.

## Examples

Five of them in [examples/](examples/); start with [HelloReceipt](examples/HelloReceipt/). The list is in [examples/README.md](examples/README.md).

**No printer is connected.** They show the generated 1bpp page on screen, so you can check a layout without spending paper.

## What it does not do

Connecting or transmitting over BLE/serial, generating ESC/POS or vendor commands, print job management, barcode encoding, automatic label layout, or complex typesetting (line-breaking rules, ruby, vertical writing). It ships no fonts either — you pass one of LovyanGFX's.

Details in [docs/REQUIREMENTS.ja.md](docs/REQUIREMENTS.ja.md) §5 (Japanese).

## Requirements

| | |
| --- | --- |
| Primary target | ESP32 family |
| Required | LovyanGFX or M5GFX, plus LGFXVirtualCanvas |
| Optional | BarcodeKit (only for `PaperCanvasBarcode.h`) |
| Not supported | **AVR** — LovyanGFX does not run there |
| C++ | C++11 or later |

## Documentation

| Document | Contents |
| --- | --- |
| [docs/GUIDE.md](docs/GUIDE.md) | Getting started: choosing the printable width and a font, and **what to check when the print is not what you expected** |
| [docs/API.md](docs/API.md) | The public API |
| [examples/README.md](examples/README.md) | The example sketches |

The full index is in [docs/README.ja.md](docs/README.ja.md) (Japanese).

## Licence

MIT.
