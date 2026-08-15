# Getting started

> 日本語: [GUIDE.ja.md](GUIDE.ja.md)

Read this once before using PaperCanvas. The API listing is in [API.md](API.md).

## 1. First decide the printable width

**This one number matters more than anything else.** Too small and you waste paper; too large and the printer cuts the right edge — **and nothing anywhere tells you it happened.**

```cpp
PaperCanvas::Receipt r(384);   // printable width, in dots
```

It is normally in the printer's spec **in dots, not millimetres**, because the printable area is narrower than the paper. Converting 58 mm at 203 dpi gives 463 dots, but **nearly every 58 mm printer prints 384**. The rest is margin.

| Paper | Typical printable width |
| --- | --- |
| 58 mm | 384 dots |
| 80 mm | 576 dots |

**Use the dot count the manufacturer publishes.** `mmToPx()` is for when a physical measurement is genuinely all you have — label stock, for instance.

```cpp
constexpr uint16_t W = PaperCanvas::mmToPx(50.0f, 203);   // a 50 mm label -> 400 px
```

[examples/PrinterWidths](../examples/PrinterWidths/) puts several widths side by side.

## 2. Receipts and labels are different things

Do not mix them. They do not share methods, so a mistake is a compile error rather than a surprise on paper.

| | Receipt | Label |
| --- | --- | --- |
| Height | **decided by the content** | fixed up front |
| Placement | stacked top to bottom | rectangles |
| Word wrap | **on by default** | **off by default** |
| Overflow | grows downwards | clipped |

The wrap defaults differ because a receipt has somewhere to grow and a fixed rectangle does not.

## 3. Choosing a font

PaperCanvas ships no fonts. You pass a LovyanGFX `IFont*`.

```cpp
r.setFont(&fonts::efontJA_16);   // Japanese
r.setFont(&fonts::Font2);        // Latin only, small
```

**At 203 dpi, 1 mm is about 8 dots.** Thermal dots bleed a little beyond their nominal size, so text needs to be a size larger than it looks like it needs on screen.

| Font height | Actual size at 203 dpi | Use |
| --- | --- | --- |
| 12 px | ~1.5 mm | Fine print. **Usually too small** |
| 16 px | ~2 mm | Line items and body text. **Start here** |
| 24 px | ~3 mm | Headings, totals |
| 32 px+ | 4 mm+ | Figures meant to stand out |

For Japanese use `efontJA_*` or `lgfxJapanGothic_*` / `lgfxJapanMincho_*`. They **take a lot of flash**, so pull in only the sizes you use.

`setTextSize(2)` will scale, but **enlarging a bitmap font by an integer factor looks blocky.** If a font exists at the size you want, use that instead.

## 4. Do not build "Item … Price" out of spaces

**Padding with spaces does not work.** Proportional fonts make the space width unpredictable, and even a monospaced font leaves you counting characters — more so with mixed-width scripts.

```cpp
r.addRow("Coffee", "480");        // name left, figure hard against the right
r.addRow("Sandwich", "620");      // different name lengths, same figure position
```

Three columns work too.

```cpp
r.addRow("Coffee", "x2", "960");
```

For explicit widths:

```cpp
const PaperCanvas::Column cols[] = {
  PaperCanvas::Column::percent(55, PaperCanvas::Align::Left, '.'),   // dot leader
  PaperCanvas::Column::percent(15, PaperCanvas::Align::Center),
  PaperCanvas::Column::rest(       PaperCanvas::Align::Right),
};
r.setColumns(cols, 3);
```

**Column widths are resolved per row from the layout, never from the text length.** That is what keeps a run of rows lined up.

A cell that does not fit its column either wraps inside it (with `wrap` set) or is cut — **the other columns do not move.**

## 5. Settings apply forwards

```cpp
r.setTextSize(1);
r.addText("small");     // 1x
r.setTextSize(2);
r.addText("large");     // 2x; the line above is unchanged
```

Each element captures the settings in force when it was added. That is what makes `height()` answerable as you go.

```cpp
uint16_t h = r.height();   // at any point, including before build()
```

## 6. Images

Either 8-bit gray (one byte per pixel, 0 = black) or a 1bpp `Bitmap` in PaperCanvas's own format.

```cpp
r.addImage(gray8, w, h, {.fit = PaperCanvas::Fit::Contain});
```

**Specify a dither if you are printing a photograph.**

```cpp
r.addImage(photo, w, h, {.mono = PaperCanvas::Mono::Bayer4x4});
```

| Method | Suits |
| --- | --- |
| `Threshold` (default) | Logos, line art, anything already black and white |
| `Bayer4x4` | Photographs; produces midtones |
| `Bayer8x8` | Photographs; smoother, coarser pattern |

**You must keep the image data alive.** Unlike strings, images are not copied — duplicating a large logo into RAM would be a poor trade. It has to survive until `build()`.

## 7. Barcodes

```cpp
#include <PaperCanvasBarcode.h>
```

Whole-number scaling, the quiet zone and guard bars are handled for you. **The only thing to check is the return value.**

```cpp
if (r.addBarcode(bc, {.barHeight = 60}) == 0) {
  // It would not fit even at one pixel per module, so nothing was drawn.
}
```

**A barcode too small to read is not drawn.** Printed, it would not be discovered until someone tried to scan it. You can also ask in advance:

```cpp
auto l = PaperCanvas::barcodeLayout(bc, opt, r.width());
if (!l.fits) { /* not enough width */ }
```

## 8. Long receipts

A long list of items makes a large page: 384 px by 4000 px is 192 KB.

```cpp
r.setMemoryLimit(16 * 1024);
r.stream([](const PaperCanvas::Bitmap& band, uint16_t y, void*) {
  sendBand(band.data, band.rowBytes, y);
});
```

`stream()` never holds the page. **Working memory follows the tile budget, not the receipt length.** The bands are the same bytes `build()` would produce, in order.

With PSRAM, `setUsePsram(true)` puts the tiles there — slower memory, but far fewer tiles.

## 9. Warnings do not stop generation

```cpp
if (r.warnings() & PaperCanvas::Warning_TextClipped) { /* text was cut */ }
```

| Warning | Meaning |
| --- | --- |
| `TextClipped` | Text did not fit and was cut |
| `TextWrapped` | Wrapping happened |
| `ImageScaled` | An image was **reduced** (enlarging is not reported) |
| `ImageClipped` | An image overflowed its rectangle |
| `OutOfBounds` | An element was placed outside the canvas |
| `BarcodeTooSmall` | A barcode was **not drawn** |

**Losing a whole print to one mistyped value is worse in practice than printing it slightly wrong**, so the page still comes out. The barcode case is the one exception.

## 10. When the print is not what you expected

Work down the list.

### The right edge is cut off

**The printable width is larger than the printer's.** See §1. Use the dot count from the spec; a value converted from the paper width is always too large.

### Text is cut, or disappears part way

Check `warnings()` for `TextClipped`.

- On a receipt, `setWrap(true)` (the default) wraps it
- On a label, widen the rectangle or use a smaller font
- If it is a cell, revisit the column widths (§4)

### The text is too small to read

See the table in §3. **You need a size larger than it looks on screen** — thermal dots bleed.

### An image comes out solid black or solid white

`Mono::Threshold` (the default) cuts at 128. For a photograph, ask for `Bayer4x4` (§6).

If a logo turns to mush, the source is probably antialiased. Move the threshold, or supply it as 1bpp in the first place.

### A barcode will not scan

1. **Was it drawn at all?** `addBarcode()` returning 0 means it was not (§7)
2. **Is the quiet zone still blank?** Leave `quietZone = true` (the default). Anything you place over the margin the library reserved defeats it
3. **Are the bars too narrow?** Set `moduleWidth` explicitly to make them wider
4. **Is the print too faint?** Print density is a printer setting, outside PaperCanvas

### The output changed when I changed the tile count

**That is a bug.** Output is guaranteed not to depend on the memory limit. Please report it with a reproduction.

### `build()` returns false

- The buffer is smaller than `bufferSize()`
- The receipt has no elements (height 0)
- The canvas width or height is 0
- The tile buffer could not be allocated

**A failed build writes nothing.** Fix the buffer and call it again; the page does not have to be rebuilt.

## 11. Next

- [API.md](API.md) — the public API
- [../examples/README.md](../examples/README.md) — the example sketches
- [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) — what is in scope and what is not (Japanese)
