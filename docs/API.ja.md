# API

> English: [API.md](API.md)

公開 API の一覧。使い方の説明は [GUIDE.ja.md](GUIDE.ja.md)。

すべて `namespace PaperCanvas`。ヘッダオンリーで `.cpp` はありません。

| ヘッダ | 内容 | 依存 |
| --- | --- | --- |
| `<PaperCanvas.h>` | すべて | LovyanGFX（または M5GFX）、LGFXVirtualCanvas |
| `<PaperCanvasBarcode.h>` | BarcodeKit を一緒に取り込む便宜ヘッダ | ＋ BarcodeKit |

`addBarcode()` は `<PaperCanvas.h>` だけでも使えます（§7）。`<PaperCanvasBarcode.h>` は BarcodeKit を include し、**依存を宣言する場所**です。

## 1. 共通型

```cpp
struct Bitmap {
  const uint8_t* data;
  uint16_t width;      // px
  uint16_t height;     // px
  uint16_t rowBytes;   // (width + 7) / 8
};
```

**bit = 1 が黒**、**MSB first**（バイトの bit7 が左端の画素）、行はバイト境界から始まり、行末の余りビットは 0。ESC/POS の `GS v 0` と同じ並びです。

```cpp
struct Rect { int16_t x, y; uint16_t w, h; };

enum class Align  : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Middle, Bottom };

enum class Fit : uint8_t {
  None,      // 原寸
  Contain,   // 縦横比を維持して矩形に収める
  Cover,     // 縦横比を維持して矩形を覆う
  Stretch,   // 縦横比を無視して矩形いっぱい
  Scale,     // 倍率を明示
};

enum class Mono : uint8_t { Threshold, Bayer4x4, Bayer8x8 };
```

```cpp
enum Warning : uint16_t {
  Warning_None            = 0,
  Warning_TextClipped     = 1 << 0,
  Warning_TextWrapped     = 1 << 1,
  Warning_ImageScaled     = 1 << 2,   // 縮小されたときだけ。拡大は報告しない
  Warning_ImageClipped    = 1 << 3,
  Warning_OutOfBounds     = 1 << 4,
  Warning_BarcodeTooSmall = 1 << 5,   // 描かなかった
};
```

```cpp
constexpr uint16_t rowBytes(uint16_t width);       // (width + 7) / 8
constexpr uint16_t mmToPx(float mm, uint16_t dpi); // 四捨五入
constexpr float    pxToMm(uint16_t px, uint16_t dpi);
```

## 2. オプション

```cpp
struct TextOptions {
  const lgfx::IFont* font = nullptr;  // nullptr = ページの現在のフォント
  float size = 0;                     // 0 = ページの現在のサイズ
  Align align = Align::Left;
  VAlign valign = VAlign::Top;        // Label のみ
  int16_t lineSpacing = 0;
  bool wrap = false;
  bool invert = false;                // 黒地に白
};

struct ImageOptions {
  Align align = Align::Center;
  VAlign valign = VAlign::Middle;     // Label のみ
  Fit fit = Fit::None;
  float scale = 1.0f;                 // Fit::Scale のとき
  Mono mono = Mono::Threshold;
  uint8_t threshold = 128;
  bool invert = false;
};

struct RowOptions {
  const lgfx::IFont* font = nullptr;
  float size = 0;
  int16_t lineSpacing = 0;
  bool wrap = false;                  // 各セルを自分の列の中で折り返す
  bool invert = false;
};
```

## 3. 列

```cpp
struct Column {
  enum class Unit : uint8_t { Px, Percent, Rest, Auto };
  Unit  unit;
  float value;
  Align align;
  char  leader;    // このセルと次のセルの間を埋める文字

  static constexpr Column px(float v, Align a, char leader = '\0');
  static constexpr Column percent(float v, Align a, char leader = '\0');
  static constexpr Column rest(Align a, char leader = '\0');
  static constexpr Column autoFit(Align a, char leader = '\0');
};
```

| 単位 | 幅 |
| --- | --- |
| `Px` | 指定値 |
| `Percent` | 「幅 − 列間の合計」に対する割合。切り捨て |
| `Rest` | 残りを均等割り。余りは最初の `Rest` へ |
| `Auto` | その行のセルに必要な幅 |

**列幅は行を追加した時点で 1 回だけ解決され、以後変わりません。** すべて整数演算で、割り算は切り捨てに統一しています。合計が幅を超えた場合は末尾の列から削り、`Warning_TextClipped` を立てます。

## 4. 共通の設定（Receipt / Label）

```cpp
void setFont(const lgfx::IFont* font);
void setTextSize(float size);              // 既定 1.0
void setAlign(Align a);                    // 既定 Left
void setLineSpacing(int16_t px);           // 既定 0
void setWrap(bool on);                     // Receipt 既定 true / Label 既定 false
void setMono(Mono method, uint8_t threshold = 128);
void setDpi(uint16_t dpi);                 // 既定 203。換算と診断用。描画には使わない

bool setColumns(const Column* cols, size_t n);   // コピーされる。最大 8 列
void clearColumns();
void setColumnGap(uint16_t px);            // 既定 8

void setMemoryLimit(size_t bytes);         // タイルバッファ上限
void setUsePsram(bool on);                 // 既定 false
```

**設定は以後に追加する要素にのみ効きます。** 追加済みの要素は変わりません。

## 5. 共通の状態と生成

```cpp
uint16_t width() const;
size_t   count() const;                    // 積んだ要素数
uint16_t warnings() const;
void     clearWarnings();
void     clear();                          // 全要素と警告を捨てる

size_t bufferSize() const;                 // rowBytes(width) * height

bool build(uint8_t* data, size_t size);    // 全ページ

using BandFn = void (*)(const Bitmap& band, uint16_t y, void* ctx);
bool stream(BandFn fn, void* ctx = nullptr);   // 帯。ページを保持しない
```

`build()` と `stream()` は同じバイト列を生成します。

**失敗したときは何も書き込みません。** `false` を返すのは次の場合です。

- `data` が `nullptr`、または `size < bufferSize()`
- 幅か高さが 0（要素の無いレシートを含む）
- タイルバッファを確保できない

フォールバック（小さいページを作る、途中まで出す）は**しません**。

## 6. Receipt

```cpp
explicit Receipt(uint16_t printableWidth);

void setMargin(uint16_t top, uint16_t bottom, uint16_t left, uint16_t right);
uint16_t height() const;                   // 積んだ分。いつでも問える
```

要素の追加。**戻り値はその要素が占めた高さ(px)**、0 なら追加できなかったことを示します。

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
uint16_t addLine(uint16_t thickness = 1);  // 内容幅いっぱいの罫線
uint16_t addRule(char c);                  // 文字で埋める区切り線
```

**文字列はコピーされます。** 局所バッファをそのまま渡せます。**画像データはコピーされません**（`build()` まで保持してください）。

## 7. Label

```cpp
Label(uint16_t width, uint16_t height);
uint16_t height() const;
```

戻り値は成否です。

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

`addLine()` は**水平か垂直のみ**です。斜線は描けません（紙面はドットの格子で、アンチエイリアス無しの斜線は 1bpp 化で予測しづらくなるため）。

キャンバスからはみ出す矩形は `Warning_OutOfBounds` を立ててクリップされます。**エラーにはしません。**

## 8. バーコード

```cpp
struct BarcodeOptions {
  uint16_t moduleWidth = 0;   // 1モジュールのピクセル数。0 = 収まる最大の整数倍
  uint16_t barHeight = 0;     // 1次元のバー高さ。0 = 幅の 1/4
  bool quietZone = true;
  Align align = Align::Center;
  VAlign valign = VAlign::Middle;
  bool invert = false;
  float guardExtend = 0.08f;  // EAN/UPC のガードバーがデータバーより下へ伸びる割合
};

struct BarcodeLayout {
  uint16_t scale, width, height;
  uint16_t quietL, quietR, quietT, quietB;
  uint16_t barHeight, guardExtra;
  bool fits;                  // false なら読める大きさで描けない
};

template <class T>
BarcodeLayout barcodeLayout(const T& bc, const BarcodeOptions& opt,
                            uint16_t boxW, uint16_t boxH = 0);

inline size_t barcodeBufferSize(const BarcodeLayout& l);

template <class T>
bool renderBarcode(const T& bc, const BarcodeLayout& l, uint8_t* out, size_t size);
```

**倍率は必ず整数倍**になります。非整数倍はモジュール幅が不揃いになり、スキャナが別のシンボルとして読みます。

`addBarcode()` は `fits == false` のとき**何も描かず**、`Warning_BarcodeTooSmall` を立てて 0（`Label` では `false`）を返します。

### 受け付ける型

エンコーダを名指ししません。次のメンバを持つ型なら通ります。

```cpp
uint16_t width() const;                    // モジュール数
uint16_t height() const;                   // モジュール行数。1次元は 1
bool     module(uint16_t x, uint16_t y) const;   // true = 黒
uint8_t  quietLeft() const;                // quietRight / quietTop / quietBottom も
bool     barExtends(uint16_t x) const;     // ガードバーの列か
```

これは BarcodeKit の API 形状です。同じ形の別のエンコーダもそのまま使えます。

## 9. コンパイル時スイッチ

| マクロ | 効果 |
| --- | --- |
| `PAPERCANVAS_NO_DITHER` | Bayer のテーブルを落とす。`Mono::Bayer*` は閾値として振る舞う |

## 10. バージョン

```cpp
#include <PaperCanvas.h>
PAPERCANVAS_VERSION_STR      // "1.0.0"
PAPERCANVAS_VERSION_MAJOR    // MINOR / PATCH も
```
