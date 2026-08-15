# API

> 日本語: [API.ja.md](API.ja.md)

The public API. For how to use it, see [GUIDE.md](GUIDE.md).

Everything is in `namespace PaperCanvas`. Header-only; there are no `.cpp` files.

| Header | Contents | Depends on |
| --- | --- | --- |
| `<PaperCanvas.h>` | Everything | LovyanGFX (or M5GFX), LGFXVirtualCanvas |
| `<PaperCanvasBarcode.h>` | Convenience header that also pulls in BarcodeKit | + BarcodeKit |

`addBarcode()` works with `<PaperCanvas.h>` alone (§8). `<PaperCanvasBarcode.h>` includes BarcodeKit and exists so **the dependency is declared where it is used**.

## 1. Common types

```cpp
struct Bitmap {
  const uint8_t* data;
  uint16_t width;      // px
  uint16_t height;     // px
  uint16_t rowBytes;   // (width + 7) / 8
};
```

**bit = 1 is black**, **MSB first** (bit 7 of a byte is its leftmost pixel), rows start on a byte boundary, and spare bits at the end of a row are 0. This is the ESC/POS `GS v 0` layout.

```cpp
struct Rect { int16_t x, y; uint16_t w, h; };

enum class Align  : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Middle, Bottom };

enum class Fit : uint8_t {
  None,      // original size
  Contain,   // scale to fit inside the rect, keeping the aspect ratio
  Cover,     // scale to cover the rect, keeping the aspect ratio
  Stretch,   // fill the rect exactly, ignoring the aspect ratio
  Scale,     // an explicit factor
};

enum class Mono : uint8_t { Threshold, Bayer4x4, Bayer8x8 };
```

```cpp
enum Warning : uint16_t {
  Warning_None            = 0,
  Warning_TextClipped     = 1 << 0,
  Warning_TextWrapped     = 1 << 1,
  Warning_ImageScaled     = 1 << 2,   // only on reduction; enlarging is not reported
  Warning_ImageClipped    = 1 << 3,
  Warning_OutOfBounds     = 1 << 4,
  Warning_BarcodeTooSmall = 1 << 5,   // nothing was drawn
};
```

```cpp
constexpr uint16_t rowBytes(uint16_t width);       // (width + 7) / 8
constexpr uint16_t mmToPx(float mm, uint16_t dpi); // rounds to nearest
constexpr float    pxToMm(uint16_t px, uint16_t dpi);
```

## 2. Options

```cpp
struct TextOptions {
  const lgfx::IFont* font = nullptr;  // nullptr = the page's current font
  float size = 0;                     // 0 = the page's current size
  Align align = Align::Left;
  VAlign valign = VAlign::Top;        // labels only
  int16_t lineSpacing = 0;
  bool wrap = false;
  bool invert = false;                // white on black
};

struct ImageOptions {
  Align align = Align::Center;
  VAlign valign = VAlign::Middle;     // labels only
  Fit fit = Fit::None;
  float scale = 1.0f;                 // Fit::Scale only
  Mono mono = Mono::Threshold;
  uint8_t threshold = 128;
  bool invert = false;
};

struct RowOptions {
  const lgfx::IFont* font = nullptr;
  float size = 0;
  int16_t lineSpacing = 0;
  bool wrap = false;                  // wrap each cell inside its own column
  bool invert = false;
};
```

## 3. Columns

```cpp
struct Column {
  enum class Unit : uint8_t { Px, Percent, Rest, Auto };
  Unit  unit;
  float value;
  Align align;
  char  leader;    // fills the space between this cell and the next

  static constexpr Column px(float v, Align a, char leader = '\0');
  static constexpr Column percent(float v, Align a, char leader = '\0');
  static constexpr Column rest(Align a, char leader = '\0');
  static constexpr Column autoFit(Align a, char leader = '\0');
};
```

| Unit | Width |
| --- | --- |
| `Px` | Exactly that |
| `Percent` | A share of the width left after the gaps; truncated |
| `Rest` | Whatever remains, split evenly; the remainder goes to the first `Rest` |
| `Auto` | Exactly what this row's cell needs |

**Widths are resolved once when the row is added and never recomputed.** All integer arithmetic, all division truncating. If the total exceeds the width, columns are shrunk from the last one back and `Warning_TextClipped` is raised.

## 4. Shared settings (Receipt and Label)

```cpp
void setFont(const lgfx::IFont* font);
void setTextSize(float size);              // default 1.0
void setAlign(Align a);                    // default Left
void setLineSpacing(int16_t px);           // default 0
void setWrap(bool on);                     // Receipt default true, Label default false
void setMono(Mono method, uint8_t threshold = 128);
void setDpi(uint16_t dpi);                 // default 203; for conversion and diagnostics only

bool setColumns(const Column* cols, size_t n);   // copied; 8 columns maximum
void clearColumns();
void setColumnGap(uint16_t px);            // default 8

void setMemoryLimit(size_t bytes);         // tile buffer budget
void setUsePsram(bool on);                 // default false
```

**A setting only affects elements added after it.** What is already on the page does not change.

## 5. Shared state and generation

```cpp
uint16_t width() const;
size_t   count() const;                    // elements added
uint16_t warnings() const;
void     clearWarnings();
void     clear();                          // drop all elements and warnings

size_t bufferSize() const;                 // rowBytes(width) * height

bool build(uint8_t* data, size_t size);    // the whole page

using BandFn = void (*)(const Bitmap& band, uint16_t y, void* ctx);
bool stream(BandFn fn, void* ctx = nullptr);   // bands; the page is never held
```

`build()` and `stream()` produce the same bytes.

**A failed call writes nothing.** `false` means one of:

- `data` is null, or `size < bufferSize()`
- The width or height is 0 (including a receipt with no elements)
- The tile buffer could not be allocated

There is **no fallback** — it will not quietly produce a smaller or partial page.

## 6. Receipt

```cpp
explicit Receipt(uint16_t printableWidth);

void setMargin(uint16_t top, uint16_t bottom, uint16_t left, uint16_t right);
uint16_t height() const;                   // what has been stacked; valid at any time
```

Adding elements. **The return value is the height that element took**, or 0 if it could not be added.

```cpp
uint16_t addText(const char* text);
uint16_t addText(const char* text, const TextOptions& opt);

uint16_t addRow(const char* left, const char* right);
uint16_t addRow(const char* left, const char* center, const char* right);
uint16_t addRow(const char* const* cells, size_t n);
uint16_t addRow(const char* const* cells, size_t n, const RowOptions& opt);

uint16_t addImage(const Bitmap& src, const ImageOptions& opt = {});
uint16_t addImage(const uint8_t* gray8, uint16_t w, uint16_t h, const ImageOptions& opt = {});

template <class T>
uint16_t addBarcode(const T& bc, const BarcodeOptions& opt = {});

uint16_t addSpace(uint16_t px);
uint16_t addLine(uint16_t thickness = 1);  // a rule across the content width
uint16_t addRule(char c);                  // a separator made of repeated characters
```

**Strings are copied**, so a local buffer is fine. **Image data is not** — keep it alive until `build()`.

## 7. Label

```cpp
Label(uint16_t width, uint16_t height);
uint16_t height() const;
```

These return success.

```cpp
bool addText(const Rect& r, const char* text, const TextOptions& opt = {});

bool addRow(const Rect& r, const char* left, const char* right);
bool addRow(const Rect& r, const char* left, const char* center, const char* right);
bool addRow(const Rect& r, const char* const* cells, size_t n, const RowOptions& opt = {});

bool addImage(const Rect& r, const Bitmap& src, const ImageOptions& opt = {});
bool addImage(const Rect& r, const uint8_t* gray8, uint16_t w, uint16_t h,
              const ImageOptions& opt = {});

template <class T>
bool addBarcode(const Rect& r, const T& bc, const BarcodeOptions& opt = {});

bool addRect(const Rect& r, bool fill = false, uint16_t thickness = 1);
bool addLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t thickness = 1);
```

`addLine()` draws **horizontal or vertical rules only**. A page is a grid of dots, and an unantialiased diagonal does not survive the 1bpp reduction predictably.

A rectangle that leaves the canvas raises `Warning_OutOfBounds` and is clipped. **It is not an error.**

## 8. Barcodes

```cpp
struct BarcodeOptions {
  uint16_t moduleWidth = 0;   // pixels per module; 0 = the largest whole number that fits
  uint16_t barHeight = 0;     // linear bar height; 0 = a quarter of the symbol width
  bool quietZone = true;
  Align align = Align::Center;
  VAlign valign = VAlign::Middle;
  bool invert = false;
  float guardExtend = 0.08f;  // how far EAN/UPC guard bars run below the data bars
};

struct BarcodeLayout {
  uint16_t scale, width, height;
  uint16_t quietL, quietR, quietT, quietB;
  uint16_t barHeight, guardExtra;
  bool fits;                  // false = cannot be drawn readably
};

template <class T>
BarcodeLayout barcodeLayout(const T& bc, const BarcodeOptions& opt,
                            uint16_t boxW, uint16_t boxH = 0);

inline size_t barcodeBufferSize(const BarcodeLayout& l);

template <class T>
bool renderBarcode(const T& bc, const BarcodeLayout& l, uint8_t* out, size_t size);
```

**The scale is always a whole number of pixels.** At a fractional scale some modules come out a pixel wider than others and a scanner reads the uneven widths as a different symbol.

When `fits == false`, `addBarcode()` **draws nothing**, raises `Warning_BarcodeTooSmall`, and returns 0 (`false` on a `Label`).

### What it accepts

No encoder is named. Any type with these members works:

```cpp
uint16_t width() const;                    // modules across
uint16_t height() const;                   // module rows; 1 for linear
bool     module(uint16_t x, uint16_t y) const;   // true = black
uint8_t  quietLeft() const;                // and quietRight / quietTop / quietBottom
bool     barExtends(uint16_t x) const;     // is this a guard bar column
```

That is BarcodeKit's shape. Another encoder with the same shape drops in unchanged.

## 9. Compile-time switches

| Macro | Effect |
| --- | --- |
| `PAPERCANVAS_NO_DITHER` | Drops the Bayer tables; `Mono::Bayer*` then behaves as a threshold |

## 10. Version

```cpp
#include <PaperCanvas.h>
PAPERCANVAS_VERSION_STR      // "1.0.0"
PAPERCANVAS_VERSION_MAJOR    // and MINOR / PATCH
```
