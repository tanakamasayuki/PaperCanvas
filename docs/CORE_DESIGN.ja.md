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
    Element.h               描画要素レコード（自己完結）
    PageBase.h              Receipt / Label 共通の内部エンジン（非公開）
    Receipt.h
    Label.h
  PaperCanvasBarcode/
    BarcodeKit.h            BarcodeKit のシンボルを要素へ変換
```

- **ヘッダオンリー。`.cpp` は置かない。** すべて `inline` または `static constexpr`
- `PaperCanvas.h` は LovyanGFX と LGFXVirtualCanvas に依存する。BarcodeKit には依存しない
- `PaperCanvasBarcode.h` は `PaperCanvas.h` と BarcodeKit に依存する。逆はない
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
uint16_t addRow(...);                            // 行内の列レイアウト。§4.2.1（**未確定**）
uint16_t addImage(const Bitmap& src, const ImageOptions& opt = {});
uint16_t addImage(const uint8_t* gray8, uint16_t w, uint16_t h, const ImageOptions& opt = {});
uint16_t addSpace(uint16_t px);                  // 空白
uint16_t addLine(uint16_t thickness = 1);        // 全幅の罫線
uint16_t addRule(char c);                        // 文字で埋める区切り線（"------"）
```

戻り値は**その要素が占めた高さ(px)**。`height()` は現在の総高さを返す。

### 4.2.1 `addRow` — 行内の列レイアウト（**未確定。詰め直す**）

> **この節は仕様が固まっていない。** 方向性と論点だけ記録してある。実装（フェーズ 2）に入る前に決める。

**確定していること**

- 「品名は左、金額は右」はレシートで最も多い行であり、**空白でパディングする方法は成立しない**（プロポーショナルフォントでは幅が揃わず、必要な空白数を利用者が数えることになる）。行内の列配置はライブラリが持つ
- **1 列（左／中／右のいずれかへ寄せる）と 2 列（左右）は簡単**。基準位置が紙面の端と中心線だけで決まり、列幅を決める必要がない
- **3 列以上はグリッドが要る**。列幅の配分を決めないと位置が定まらず、行ごとに揺れて表として読めなくなる

**未確定 — 簡易ヘルパーとグリッドの関係**

1 列・2 列を「別実装の簡易ヘルパー」として持つか、「定義済みのグリッド」として持つか。後者なら実装が 1 本で済むが、簡単なケースに列定義の概念が漏れる。**後で決める。**

なお 1 列は既存の `addText` + `setAlign(Align)` でも表現できるので、`addRow` の 1 列版が要るかどうかもここで一緒に決める。

**有力な方向 — グリッド（列を先に定義する）**

3 列以上について、**列幅（px または %）と寄せを持つ列定義を先に置き、そこへセルを流し込む**形にする。

```cpp
using namespace PaperCanvas;

// 列を定義する。以後の addRow に効く
const Column cols[] = {
  Column::percent(50, Align::Left),     // 品名
  Column::percent(20, Align::Center),   // 数量
  Column::rest(      Align::Right),     // 金額（残り幅）
};
r.setColumns(cols, 3);

r.addRow({"コーヒー",     "x2", "¥960"});
r.addRow({"サンドイッチ", "x1", "¥620"});
```

```cpp
struct Column {
  enum class Unit : uint8_t { Px, Percent, Rest };
  Unit     unit;
  float    value;                    // Px なら px、Percent なら 0..100、Rest なら未使用
  Align    align  = Align::Left;
  char     leader = '\0';            // このセルを埋める文字
  uint16_t gap    = 8;               // 次の列との最小間隔(px)

  static constexpr Column px(float v, Align a);
  static constexpr Column percent(float v, Align a);
  static constexpr Column rest(Align a);          // 残り幅を取る。1 行に 1 つまで
};
```

この形なら、

- 列数がいくつでも入る（`addRow` の overload を増やさなくてよい）
- 列が**縦に必ず揃う**。行ごとの文字長に左右されない
- ラベル側でも矩形幅に対して同じ規則で使える

**詰めるべき論点**

| 論点 | 内容 |
| --- | --- |
| **簡易ヘルパーとグリッドの関係** | 1 列・2 列を別実装にするか、定義済みグリッドにするか。1 列版がそもそも要るか（`addText` + `setAlign` で足りる） |
| 列定義の持ち方 | `Receipt` の設定として持つ（以後の行に効く）か、行ごとに渡すか、両方か |
| `%` の基準 | 紙面幅か、余白を引いた印字幅か。ラベルでは矩形幅 |
| 端数の丸め | `%` から px への変換で合計が幅と合わないときの配分規則。**決定性のため規則を固定する必要がある** |
| セルが列幅に収まらないとき | 折り返す／クリップする／列を押し広げる。列を押し広げると縦が揃わなくなる |
| `Rest` が複数あるとき | 均等割りか、エラーか |
| リーダー文字 | 列に持たせるか（上記案）、行オプションに持たせるか |
| 列定義の API 形 | 配列＋長さ（上記案）か、`std::initializer_list` か、ビルダーか。**C++11 下限・動的確保なしの制約下で決める** |

**収まらないときの共通規則（これは維持する）**

- **右端の列（金額）は絶対に切らない**
- 溢れたときは警告を立てるが**生成は続行する**（`Warning::TextClipped` / `TextWrapped`）
- 空文字列のセルは幅 0 として扱い、描画しない

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
void addRow(const Rect& r, ...);          // §4.2.1（**未確定**）
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
  // 行内の列レイアウトは §4.2.1（未確定）。以下は方向性のイメージ
  static const PaperCanvas::Column cols[] = {
    PaperCanvas::Column::percent(55, PaperCanvas::Align::Left),    // 品名
    PaperCanvas::Column::percent(15, PaperCanvas::Align::Center),  // 数量
    PaperCanvas::Column::rest(       PaperCanvas::Align::Right),   // 金額
  };
  r.setColumns(cols, 3);
  r.addRow({"コーヒー",     "x2", "¥960"});
  r.addRow({"サンドイッチ", "x1", "¥620"});
  r.addRule('-');
  r.addRow({"合計", "", "¥1580"});

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
