# Examples

> 日本語: [README.ja.md](README.ja.md)

Example sketches for PaperCanvas. They are built on M5Unified and flash straight onto an M5Stack Core BASIC or similar.

**No printer is connected, on purpose.** PaperCanvas stops at the bitmap; everything past it — device-specific commands, transports — belongs to another library. The examples show the generated 1bpp page on screen instead, which is also a real workflow: **you can check a layout without spending paper.**

| Example | What it shows |
| --- | --- |
| [HelloReceipt](HelloReceipt/) | The smallest useful receipt. Its `showBitmap()` **is the contract with whatever comes next** |
| [HelloLabel](HelloLabel/) | The smallest label: rectangles, alignment, fitting, `mmToPx()` |
| [ReceiptWithBarcode](ReceiptWithBarcode/) | BarcodeKit integration, Code 128 and EAN-13, including **what happens when a barcode will not fit** |
| [StreamBands](StreamBands/) | A long receipt emitted band by band **without holding the page**; 100 line items at constant memory |
| [PrinterWidths](PrinterWidths/) | The same content at several printable widths, button A to cycle. **What gets reported when something overflows** |

## Flashing

```sh
cd examples/HelloReceipt
arduino-cli compile --profile m5stack_core .
arduino-cli upload  --profile m5stack_core -p /dev/ttyUSB0 .
```

Change `fqbn` in `sketch.yaml` for other boards. Anything LovyanGFX (or M5GFX) runs on will do.

`ReceiptWithBarcode` also needs BarcodeKit. BarcodeKit is unreleased, so its `sketch.yaml` refers to `dir: ../../../BarcodeKit` — **put BarcodeKit in a sibling directory.**

## The first thing to get right: printable width

**The width you hand PaperCanvas matters more than anything else.** Too small and you waste paper; too large and the printer silently cuts the right edge, with no warning from anywhere.

That number is normally in the printer's spec **in dots, not millimetres**, because the printable area is narrower than the paper. Converting 58 mm at 203 dpi gives 463 dots, but nearly every 58 mm printer prints 384 — the rest is margin.

So:

- **Use the dot count the manufacturer publishes** where you have it
- Use `mmToPx()` when all you have is a physical measurement (label stock, for instance)

[PrinterWidths](PrinterWidths/) puts several widths side by side.

## Things worth knowing early

**Strings are copied into the library.** You can pass a local `snprintf` buffer straight to `addText()` / `addRow()`; it does not have to outlive `build()`.

**A setting only affects elements added after it.** Changing `setFont()` or `setAlign()` leaves what you already added alone. That is what makes `height()` answerable as you go.

**Warnings do not stop generation.** Clipped text, a shrunken image, a rectangle off the canvas — each sets a bit in `warnings()` and the page still comes out. In practice, losing a whole print to one mistyped value is worse than printing it slightly wrong.

The exception is a **barcode that will not fit**, which is not drawn at all. An unreadable barcode, once printed, **is not discovered until someone tries to scan it.**

**Do not name a global `index`.** It collides with `index()` from `<string.h>` and will not compile — `PrinterWidths` hit this, which is why it uses `paperIndex`.

## Next

- [../README.md](../README.md) — what the library is
- [../docs/GUIDE.md](../docs/GUIDE.md) — getting started, choosing a font, and what to check when the print is not what you expected
- [../docs/API.md](../docs/API.md) — the public API
