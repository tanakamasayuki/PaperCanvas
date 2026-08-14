# core 設計

内部の設計記録。日本語のみ。何を作るかは [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md)、なぜそうしたかは [DECISIONS.ja.md](DECISIONS.ja.md)、レイアウト JSON は [LAYOUT_FORMAT.ja.md](LAYOUT_FORMAT.ja.md)。

## 1. ファイル構成

```text
src/
  PaperCanvas.h             入口。共通型＋Receipt / Label
  PaperCanvasBarcode.h      BarcodeKit 連携（任意インクルード）
  papercanvas_version.h     tools/bump_version.py が生成
  PaperCanvas/
    Common.h                Bitmap, Rect, Align, Fit, Mono, Warning, mmToPx
    MonoPanel.h             Panel_Device 派生の 1bpp シンク。グレー→二値化→パック
    MonoSink.h              MonoPanel を載せた LGFX_Device
    Dither.h                閾値 / Bayer 4x4 / Bayer 8x8
    Barcode.h               バーコード配置（倍率・余白・ガードバー）。エンコーダ非依存
    Element.h               描画要素レコード（自己完結）
    PageBase.h              Receipt / Label 共通の内部エンジン（非公開）
    Receipt.h
    Label.h
```

- **ヘッダオンリー。`.cpp` は置かない。** すべて `inline` または `static constexpr`
- `PaperCanvas.h` は LovyanGFX と LGFXVirtualCanvas に依存する。BarcodeKit には依存しない
- `PaperCanvasBarcode.h` は `PaperCanvas.h` と BarcodeKit に依存する。逆はない
- **バーコード配置は `PaperCanvas/Barcode.h` にあり、エンコーダを名指ししない。** 必要なのは `width()` / `height()` / `module()` / `quiet*()` / `barExtends()` という形だけで、`addBarcode()` はテンプレートメンバ。`PaperCanvasBarcode.h` は BarcodeKit を一緒に引き込む便宜ヘッダ（§8）
- 抽象基底も `virtual` も使わない（`MonoPanel` が LovyanGFX の `Panel_Device` を継承する箇所を除く。ここは LovyanGFX 側の要求）

## 2. 全体の流れ

```text
利用者
  │ addText / addImage / addBarcode …   要素を積む（add 時に高さ確定）
  ▼
Receipt / Label      要素リストを保持する。まだ何も描かない
  │ build() / stream()
  ▼
LGFXVirtualScreen    面をタイル（横帯）に分割し、タイルごとに draw を再実行
  │ タイル sprite = grayscale_8bit, 幅 = ページ幅, 高さ = tileH
  │ pushSprite → writeImage(pixelcopy_t)
  ▼
MonoPanel            fp_copy でグレー8bit 行バッファへ展開
  │                  → Bayer / 閾値で二値化 → 1bpp へパック
  ▼
出力                 全ページバッファ  または  帯コールバック
```

要素リストは**自己完結レコード**である。各要素が自分のフォント・サイズ・寄せ方・モノクロ化方式を保持するため、タイルごとに何度 draw を再実行しても同じ結果になる。これが決定性の根拠であり、`LGFXVirtualCanvas` のコールバック再実行モデルが成立する前提でもある。

## 3. 共通型

```cpp
namespace PaperCanvas {

struct Bitmap {
  const uint8_t* data;     // rowBytes * height バイト
  uint16_t width;          // px
  uint16_t height;         // px
  uint16_t rowBytes;       // (width + 7) / 8
};                         // bit=1 が黒、MSB first、bit7 が左端

struct Rect { int16_t x, y; uint16_t w, h; };

enum class Align  : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Middle, Bottom };

enum class Fit : uint8_t {
  None,        // 原寸。はみ出す分はクリップ
  Contain,     // 縦横比を維持して矩形に収める
  Cover,       // 縦横比を維持して矩形を覆う。はみ出す分はクリップ
  Stretch,     // 縦横比を無視して矩形いっぱい
  Scale,       // 倍率を明示指定
};

enum class Mono : uint8_t {
  Threshold,   // 既定。閾値（既定 128）
  Bayer4x4,
  Bayer8x8,
};

enum class Warning : uint16_t {
  None          = 0,
  TextClipped   = 1 << 0,   // 矩形に収まらず文字が切れた
  TextWrapped   = 1 << 1,   // 折り返しが発生した
  ImageScaled   = 1 << 2,   // 画像が縮小された
  ImageClipped  = 1 << 3,   // 画像が矩形からはみ出して切れた
  OutOfBounds   = 1 << 4,   // 要素がキャンバス外に置かれた
  BarcodeTooSmall = 1 << 5, // 倍率1でも矩形に入らずバーコードを描かなかった
};

constexpr uint16_t mmToPx(float mm, uint16_t dpi);
constexpr float    pxToMm(uint16_t px, uint16_t dpi);
constexpr uint16_t rowBytes(uint16_t width) { return (width + 7) >> 3; }

}
```

`Warning` はビットフラグ。`warnings()` が積算値を返し、`clearWarnings()` で消せる。動的確保を伴わないので組み込みでも安全に使える。

## 4. Receipt

```cpp
PaperCanvas::Receipt r(384);          // 印字可能幅(px)
```

### 4.1 設定

設定は**以後に追加する要素にのみ効く**。add 時に要素へ焼き込むため、後から設定を変えても既に積んだ要素は変わらない。これは決定性と `height()` の即時性を両立させるための規則であり、[DECISIONS.ja.md](DECISIONS.ja.md) D5 に理由がある。

| メソッド | 既定 | 意味 |
| --- | --- | --- |
| `setDpi(uint16_t)` | 203 | `mmToPx` ヘルパーと診断用。描画には使わない |
| `setMargin(t, b, l, r)` | 0 | 紙面の余白(px) |
| `setFont(const lgfx::IFont*)` | LGFX 既定 | フォント |
| `setTextSize(float)` | 1.0 | 文字倍率 |
| `setAlign(Align)` | Left | 横方向の寄せ |
| `setLineSpacing(int16_t)` | 0 | 行間の追加ピクセル |
| `setWrap(bool)` | **true** | 自動折り返し |
| `setMono(Mono, uint8_t threshold=128)` | Threshold | モノクロ化方式 |
| `setMemoryLimit(size_t)` | 0（LGFXVirtualCanvas 既定） | タイルバッファ上限 |
| `setUsePsram(bool)` | false | タイルを PSRAM に置く |

### 4.2 要素の追加

```cpp
uint16_t addText(const char* text);              // 現在の設定で積む
uint16_t addText(const char* text, const TextOptions& opt);
uint16_t addRow(const char* left, const char* right);                     // 2 列
uint16_t addRow(const char* left, const char* center, const char* right); // 3 列
uint16_t addRow(const char* const* cells, size_t n);                      // 任意の列数
uint16_t addImage(const Bitmap& src, const ImageOptions& opt = {});
uint16_t addImage(const uint8_t* gray8, uint16_t w, uint16_t h, const ImageOptions& opt = {});
uint16_t addSpace(uint16_t px);                  // 空白
uint16_t addLine(uint16_t thickness = 1);        // 全幅の罫線
uint16_t addRule(char c);                        // 文字で埋める区切り線（"------"）
```

戻り値は**その要素が占めた高さ(px)**。`height()` は現在の総高さを返す。

### 4.2.1 `addRow` — 行内の列レイアウト（**実装済み。レビュー待ち**）

> 推奨形で実装し、`tests/row/` が通っている。**API の見直しは可能**。レビューでの論点は末尾に挙げる。

**なぜ必要か**

「品名は左、金額は右」はレシートで最も多い行だが、**空白でパディングする方法は成立しない**。プロポーショナルフォントでは空白の幅が揃わず、等幅でも必要な空白数を利用者が数えることになる。行内の列配置はライブラリが持つ。

**採った形 — グリッド 1 本**

列を宣言し、そこへセルを流し込む。**1 列・2 列の簡易ヘルパーは別実装にせず、暗黙の列定義として同じ経路に載せた。** 実装が 1 本で済み、簡単なケースでも列の概念が破綻しない。

```cpp
using namespace PaperCanvas;

// 明示的な列定義。以後の addRow に効く
const Column cols[] = {
  Column::percent(55, Align::Left, '.'),  // 品名（リーダーはドット）
  Column::percent(15, Align::Center),     // 数量
  Column::rest(       Align::Right),      // 金額（残り幅）
};
r.setColumns(cols, 3);
r.setColumnGap(8);                        // 列間の間隔(px)

r.addRow("コーヒー",     "x2", "¥960");
r.addRow("サンドイッチ", "x1", "¥620");
```

```cpp
// 列定義なしなら暗黙のレイアウト。レシート行の一番普通の形になる
r.clearColumns();
r.addRow("コーヒー", "¥960");     // 品名が残り幅、金額は必要な幅だけ右端に
```

```cpp
struct Column {
  enum class Unit : uint8_t { Px, Percent, Rest, Auto };
  Unit  unit   = Unit::Rest;
  float value  = 0;              // Px なら px、Percent なら 0..100
  Align align  = Align::Left;
  char  leader = '\0';           // このセルと次のセルの間を埋める文字

  static constexpr Column px(float v, Align a, char leader = '\0');
  static constexpr Column percent(float v, Align a, char leader = '\0');
  static constexpr Column rest(Align a, char leader = '\0');      // 残りを分け合う
  static constexpr Column autoFit(Align a, char leader = '\0');   // そのセルに必要な幅
};
```

**API**

```cpp
bool setColumns(const Column* cols, size_t n);   // 定義はコピーされる。最大 8 列
void clearColumns();                             // 暗黙のレイアウトへ戻す
void setColumnGap(uint16_t px);                  // 既定 8

uint16_t addRow(const char* const* cells, size_t n);
uint16_t addRow(const char* const* cells, size_t n, const RowOptions& opt);
uint16_t addRow(const char* left, const char* right);                       // 2 列
uint16_t addRow(const char* left, const char* center, const char* right);   // 3 列
```

**暗黙のレイアウト**（`setColumns` していないとき、または列数が一致しないとき）

先頭列が `Rest(Left)`、以降が `Auto`（最後は `Right`、間は `Center`）。品名は伸び縮みし、数字は必要な幅だけ取って右端に付く。レシート行の性質そのもの。

**列幅の解決規則（決定的。add 時に 1 回だけ）**

1. `Auto` はその行のセルの自然幅を取る
2. `Px` は指定値
3. `Percent` は「幅 − 列間の合計」に対する割合。**切り捨て**
4. `Rest` は残りを均等割り。**割り切れない分は最初の `Rest` 列へ**
5. `Rest` が無く余りがある場合は最後の列が受け取る
6. 合計が幅を超えた場合は**末尾の列から削る**（読者が縦に追う先頭列の位置を保つため）。`Warning_TextClipped` を立てる

すべて整数演算で、割り算は切り捨てに統一している。浮動小数の丸めに依存する箇所を作らないため。

**セルが列幅に収まらないとき**

- `wrap` が真なら**その列の中で折り返す**。行の高さは全セルの最大値。`Warning_TextWrapped`
- 偽ならクリップ。`Warning_TextClipped`
- **列は絶対に押し広げない。** 押し広げると以降の行がずれ、`addRow` の存在理由が消える

列幅が固定である以上「右端の列が押し出される」状況が起きないので、たたき台にあった段階的な縮退規則は不要になった。

**リーダー文字**

列に持たせる。描画時、そのセルの実際の文字の終わりから**次のセルの文字の始まり**まで埋める（列の端までではない。次のセルの余白の下へ潜り込ませないため）。折り返したセルでは 1 行目にのみ引く。

**空セル**

空文字列は幅 0 として扱う。`addRow("合計", "", "¥1580")` で 3 列 API のまま「中央を空ける」が書ける。

**レビューでの論点**

| 論点 | 現状 |
| --- | --- |
| 1 列版 `addRow` が要るか | 用意していない。`addText` + `setAlign` で足りると判断した |
| 列定義を行ごとに渡す overload | 用意していない。ページ設定のみ |
| `Auto` 列の扱い | その行のセル幅で決まるので、行ごとに幅が変わりうる。列を跨いで揃えたいなら `Px` / `Percent` を使う |
| 最大列数 8 | `kMaxColumns`。スタック上の一時配列の都合 |
| 列間 `gap` | 全列共通。列ごとに変えたい要求が出るかは未知 |

```cpp
uint16_t width()  const;   // 印字可能幅
uint16_t height() const;   // 現在の総高さ（余白込み）。いつでも問える
size_t   count()  const;   // 積んだ要素数
void     clear();          // 全要素を捨てる
```

### 4.3 生成

```cpp
// 全ページ。data は利用者が用意する
bool build(uint8_t* data, size_t size);
size_t bufferSize() const;    // rowBytes(width()) * height()

// 帯出力。ページ全体を保持しない
using BandFn = void (*)(const Bitmap& band, uint16_t y, void* ctx);
bool stream(BandFn fn, void* ctx = nullptr);
```

`build()` / `stream()` は同じ画素を生成する。テストでこれを検証する。

## 5. Label

```cpp
PaperCanvas::Label lb(400, 240);      // 幅・高さ(px)
```

設定は Receipt と同じだが、`setWrap` の既定が **false**。要素の追加は矩形を第一引数に取る。

```cpp
void addText(const Rect& r, const char* text, const TextOptions& opt = {});
void addRow(const Rect& r, const char* const* cells, size_t n);   // §4.2.1
void addImage(const Rect& r, const Bitmap& src, const ImageOptions& opt = {});
void addRect(const Rect& r, bool fill = false, uint16_t thickness = 1);
void addLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t thickness = 1);
```

`addRow` はラベルでも同じ規則で働く。列幅の基準が紙面の印字幅ではなく**指定矩形の幅**になる点だけが違う。

生成 API（`build` / `stream` / `bufferSize` / `warnings`）は Receipt と同一。

矩形がキャンバス外に出た場合は `Warning::OutOfBounds` を立て、はみ出した分をクリップする。**エラーにはしない**（1要素の指定ミスで紙面全体が出ないほうが困るため）。

## 6. TextOptions / ImageOptions

```cpp
struct TextOptions {
  const lgfx::IFont* font = nullptr;   // nullptr = 現在の設定を使う
  float    size    = 0;                // 0 = 現在の設定を使う
  Align    align   = Align::Left;
  VAlign   valign  = VAlign::Top;      // Label のみ意味がある
  int16_t  lineSpacing = 0;
  bool     wrap    = false;
  bool     invert  = false;            // 白地に黒 ⇄ 黒地に白
};

struct ImageOptions {
  Align  align  = Align::Center;
  VAlign valign = VAlign::Middle;
  Fit    fit    = Fit::None;
  float  scale  = 1.0f;                // Fit::Scale のとき
  Mono   mono   = Mono::Threshold;     // 要素単位で上書きできる
  uint8_t threshold = 128;
  bool   invert = false;
};
```

## 7. モノクロ化とタイル分割

### 7.1 MonoPanel

`lgfx::Panel_Device` を継承した 1bpp シンク。LGFXVirtualScreen から見れば普通のパネルに見えるが、実体は画面ではなくビットマップ生成器である。

- `setColorDepth()` は `grayscale_8bit` を返す。これにより LGFX の `pixelcopy_t` が**グレー 8bit への変換 fp_copy を用意してくれる**
- `writeImage()` が呼ばれたら、`param->fp_copy` で 1 行分をグレー行バッファ（`uint8_t[width]`）へ展開し、行ごとに二値化して 1bpp へパックする
- 出力先モードは 2 つ: 全ページバッファへの書き込み、または帯コールバック
- `readRect()` / `copyRect()` は未対応。呼ばれたら何もしない（LGFXVirtualCanvas は使わない）
- DMA 系（`initDMA` / `waitDMA` / `dmaBusy`）はすべて no-op

`MonoSink` は `lgfx::LGFX_Device` を継承し、コンストラクタで `MonoPanel` を `setPanel()` する。`LGFXVirtualScreen` は `LovyanGFX&` を取るので、これをそのまま渡せる。

> **実装上の要注意点。** `pixelcopy_t::fp_copy` がグレースケール変換を正しく供給するかは LovyanGFX の内部挙動に依存する。実装の第一歩をこの検証（スパイク）に充て、崩れたら退避案へ切り替える。退路は [DECISIONS.ja.md](DECISIONS.ja.md) D3 に書いてある。

### 7.2 なぜグレースケール中間を挟むか

タイルを直接 1bpp で描くと、画像も文字も問答無用で閾値二値化される。グレー 8bit を挟むことで、

- 画像のディザが効く（写真が実用的に印刷できる）
- モノクロ化方式を PaperCanvas が完全に握れる。LGFX の色変換任せにならない
- 要素ごとに方式を変えられる

メモリ代償は小さい。384px 幅・19KB 予算なら tileH ≈ 50 行で、これは LGFXVirtualCanvas の既定タイル予算にそのまま乗る。

### 7.3 ディザと決定性

順序ディザ（Bayer）は**閾値マトリクスをページ座標で引く**。

```cpp
bool black = gray < bayer[(pageY & 3)][(pageX & 3)];
```

添字がページ絶対座標なので、タイルの切れ目がどこにあっても同じ画素は同じ結果になる。LGFXVirtualCanvas SPEC §12.1 が警告する「タイル境界をまたぐ近傍依存描画」に該当しない。

誤差拡散は該当してしまうため採らない（[DECISIONS.ja.md](DECISIONS.ja.md) D4）。

### 7.4 レシートの高さ確定

タイル分割にはページ高さが先に必要になる。PaperCanvas は **add 時に要素の高さを確定して積み上げる**ので、`build()` の時点では総高さが既知である。

テキストの高さ測定には 1×1 の `LGFX_Sprite`（2 バイト）を 1 枚だけ持ち、`setFont()` してから `fontHeight()` / `textWidth()` を問う。折り返しが有効なときは、この測定で行数を先に決める。

## 8. BarcodeKit 連携

`PaperCanvasBarcode.h` を include したときだけ生える。

```cpp
#include <PaperCanvas.h>
#include <PaperCanvasBarcode.h>      // これを足すと BarcodeKit に依存する

BarcodeKit::Code128 bc;
bc.encode("ABC-12345", buf, sizeof(buf));

PaperCanvas::BarcodeOptions bo;
bo.moduleWidth = 2;      // 0 = 幅に合わせて自動（必ず整数倍）
bo.barHeight   = 60;
bo.quietZone   = true;   // 既定 ON
bo.align       = Align::Center;

r.addBarcode(bc, bo);                       // Receipt
lb.addBarcode({10, 10, 200, 80}, bc, bo);   // Label
```

- 倍率は**必ず整数倍**にする。非整数倍はモジュール幅が不均一になり読み取り性能が落ちる
- クワイエットゾーンは BarcodeKit の `quietLeft/Right/Top/Bottom()` から取り、既定で付ける
- EAN/UPC のガードバー延長は `barExtends(x)` を見て処理する
- 倍率 1 でも収まらない場合は**何も描かず** `Warning::BarcodeTooSmall` を立てる（読めないバーコードを刷らないため。BarcodeKit の `fits=false` と同じ方針）

BarcodeKit を使わない利用者は、任意のバーコードライブラリの出力を `Bitmap` にして `addImage()` すればよい。この経路は `PaperCanvas.h` だけで動く。

## 9. 診断

`warnings()` はビットフラグの積算値を返す。**警告があっても生成は続行する**（1 要素の問題で紙面全体が出ないほうが困るため）。

生成そのものが失敗するのは次の場合だけで、`build()` / `stream()` が `false` を返す。

- バッファ不足（`bufferSize()` 未満）
- タイルバッファの確保失敗
- キャンバスサイズが 0 または上限超過

確保失敗時にタイル数を増やすなどのフォールバックは**しない**。失敗は失敗として返す（LGFXVirtualCanvas SPEC §10.3 と同じ方針）。

## 10. コンパイル時スイッチ

| マクロ | 既定 | 意味 |
| --- | --- | --- |
| `PAPERCANVAS_MAX_ELEMENTS` | 0（動的） | 要素リストを固定長にする。0 以外なら動的確保しない |
| `PAPERCANVAS_NO_DITHER` | 未定義 | Bayer テーブルを落とす（Flash 節約） |

## 11. 想定コード（レシート全体）

```cpp
#include <LovyanGFX.hpp>
#include <PaperCanvas.h>
#include <PaperCanvasBarcode.h>

PaperCanvas::Receipt r(384);        // 58mm / 203dpi

void makeReceipt() {
  r.setFont(&fonts::efontJA_16);
  r.setAlign(PaperCanvas::Align::Center);
  r.addImage(logo, {.fit = PaperCanvas::Fit::Contain});
  r.addText("ご来店ありがとうございます");
  r.addSpace(8);

  r.setAlign(PaperCanvas::Align::Left);
  r.addRule('-');
  // 行内の列レイアウト（§4.2.1）
  static const PaperCanvas::Column cols[] = {
    PaperCanvas::Column::percent(55, PaperCanvas::Align::Left),    // 品名
    PaperCanvas::Column::percent(15, PaperCanvas::Align::Center),  // 数量
    PaperCanvas::Column::rest(       PaperCanvas::Align::Right),   // 金額
  };
  r.setColumns(cols, 3);
  r.addRow("コーヒー",     "x2", "¥960");
  r.addRow("サンドイッチ", "x1", "¥620");
  r.addRule('-');
  r.addRow("合計", "", "¥1580");

  BarcodeKit::Code128 bc;
  bc.encode("T20260815-0042", buf, sizeof(buf));
  r.setAlign(PaperCanvas::Align::Center);
  r.addBarcode(bc, {.barHeight = 60});

  // ページ全体を作る
  static uint8_t page[PaperCanvas::rowBytes(384) * 800];
  if (r.build(page, sizeof(page))) {
    sendToPrinter(page, r.width(), r.height());   // 別ライブラリの責務
  }

  // または帯で流す（ページを保持しない）
  r.stream([](const PaperCanvas::Bitmap& band, uint16_t y, void*) {
    sendBand(band, y);
  });
}
```
