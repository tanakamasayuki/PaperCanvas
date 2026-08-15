# PaperCanvas

> English: [README.md](README.md)

レシートとラベルの内容を配置し、プリンターへ渡せる**モノクロ 1bit ビットマップ**を生成するライブラリ。

> **開発中です。** C++ ライブラリは動作しテストも通っていますが、まだリリースしていません。ブラウザツールが揃った時点で 1.0.0 とします。現在地は [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md) を参照してください。

## 特長

- **紙面の生成だけを担当** — 接続・通信・機種固有コマンドは扱わない。プリンターを別のものに替えても、レイアウトのコードは変わらない
- **レシートとラベルで別のモデル** — レシートは上から積んで内容の分だけ伸び、ラベルは固定キャンバスに矩形で置く。混ぜない
- **長尺でもメモリが増えない** — ページを保持せず帯（バンド）で流せる。100 行の明細でも 10 行でも作業メモリは同じ
- **同じ入力から必ず同じ出力** — 内部の分割数を変えても**1 ビットも変わらない**。自動テストで保証している
- **列が縦に揃う** — 「品名 … 金額」を空白でパディングしなくてよい。列幅はレイアウトから決まり、文字の長さに左右されない
- **バーコードは置くだけ** — 整数倍率・クワイエットゾーン・ガードバー延長を肩代わりする。読めない大きさなら**描かない**

## インストール

Arduino IDE のライブラリマネージャ、または [Releases](https://github.com/tanakamasayuki/PaperCanvas/releases) から ZIP を取得してください（リリース後）。

LovyanGFX（または M5GFX / M5Unified）と [LGFXVirtualCanvas](https://github.com/tanakamasayuki/LGFXVirtualCanvas) が必要です。

## 使い方

### レシート

```cpp
#include <M5Unified.h>
#include <PaperCanvas.h>

PaperCanvas::Receipt r(384);        // 印字可能幅(px)。58mm / 203dpi の一般的な値

void makeReceipt() {
  r.setFont(&fonts::efontJA_16);
  r.setMargin(8, 16, 4, 4);

  r.setAlign(PaperCanvas::Align::Center);
  r.addText("ご来店ありがとうございます");
  r.setAlign(PaperCanvas::Align::Left);
  r.addRule('-');

  r.addRow("コーヒー",     "480");   // 品名は左、金額は右端に揃う
  r.addRow("サンドイッチ", "620");
  r.addLine(2);
  r.addRow("合計", "1100");

  static uint8_t page[PaperCanvas::rowBytes(384) * 400];
  if (r.build(page, sizeof(page))) {
    sendToPrinter(page, r.width(), r.height());   // ここから先は別ライブラリ
  }
}
```

`height()` は積みながらいつでも問えます（紙送り量を先に知りたいときに使えます）。

### ラベル

```cpp
PaperCanvas::Label lb(400, 240);    // 50 x 30mm / 203dpi

lb.setFont(&fonts::efontJA_16);
lb.addRect({0, 0, 400, 240}, false, 2);
lb.addText({8, 8, 384, 20}, "産地直送", {.align = PaperCanvas::Align::Center});
lb.addRow({8, 34, 384, 20}, "トマト", "1kg", "580");
lb.addImage({8, 60, 120, 100}, logo, logoW, logoH, {.fit = PaperCanvas::Fit::Contain});

static uint8_t page[PaperCanvas::rowBytes(400) * 240];
lb.build(page, sizeof(page));
```

### 長尺レシートを帯で流す

ページ全体を持たないので、明細が何行あっても作業メモリは変わりません。

```cpp
r.setMemoryLimit(16 * 1024);        // 作業メモリはここで決まる
r.stream([](const PaperCanvas::Bitmap& band, uint16_t y, void*) {
  sendBand(band.data, band.rowBytes, y);
});
```

### バーコード

```cpp
#include <PaperCanvasBarcode.h>     // これを足すと BarcodeKit に依存する

uint8_t buf[BarcodeKit::Code128::bufferSize(20)];
BarcodeKit::Code128 bc;
bc.encode("T20260815-0042", buf, sizeof(buf));

if (r.addBarcode(bc, {.barHeight = 60}) == 0) {
  // 倍率1でも収まらなかった。何も描いていない
}
```

倍率は必ず整数倍になります。非整数倍はモジュール幅が不揃いになり、スキャナが別のシンボルとして読みます。

## 出力形式

```cpp
struct Bitmap {
  const uint8_t* data;
  uint16_t width, height, rowBytes;   // rowBytes = (width + 7) / 8
};
```

- **bit = 1 が黒**（印字）
- **MSB first** — バイトの bit7 が左端の画素
- 行は必ずバイト境界から始まり、行末の余りビットは 0

ESC/POS のラスタービットイメージ（`GS v 0`）と同じ並びなので、多くの機種で変換なしに渡せます。ビット順や機種固有コマンドの変換は**別ライブラリの責務**という原則は変わりません。

## サンプル

[examples/](examples/) に 5 本あります。まずは [HelloReceipt](examples/HelloReceipt/)。一覧は [examples/README.ja.md](examples/README.ja.md)。

**プリンターは繋ぎません。** 生成した 1bpp を画面に表示するので、紙を使わずにレイアウトを確認できます。

## このライブラリがやらないこと

BLE・シリアル等の接続と送信、ESC/POS など機種固有コマンドの生成、印刷ジョブ管理、バーコードの符号化、ラベルの自動レイアウト、複雑な組版（禁則・ルビ・縦書き）。フォント資産も同梱しません（LovyanGFX のものを渡します）。

詳細は [docs/REQUIREMENTS.ja.md](docs/REQUIREMENTS.ja.md) §5。

## 必要な環境

| | |
| --- | --- |
| 主対象 | ESP32 系 |
| 必須 | LovyanGFX または M5GFX、LGFXVirtualCanvas |
| 任意 | BarcodeKit（`PaperCanvasBarcode.h` を使う場合） |
| 対象外 | **AVR**（LovyanGFX が動かないため） |
| C++ | C++11 以上 |

## ドキュメント

| 文書 | 内容 |
| --- | --- |
| [docs/GUIDE.ja.md](docs/GUIDE.ja.md) | 入門ガイド。印字可能幅の決め方、フォントの選び方、**思ったとおりに刷れないときの確認手順** |
| [docs/API.ja.md](docs/API.ja.md) | 公開 API の一覧 |
| [examples/README.ja.md](examples/README.ja.md) | サンプル一覧 |

全体の案内は [docs/README.ja.md](docs/README.ja.md) にあります。

## ライセンス

MIT。
