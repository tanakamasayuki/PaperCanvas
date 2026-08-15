# Examples

> English: [README.md](README.md)

PaperCanvas のサンプルスケッチ。M5Unified ベースで、M5Stack Core BASIC などにそのまま書き込めます。

**プリンターは繋ぎません。** PaperCanvas はビットマップを作るところで止まり、その先（機種固有コマンド、通信）は別ライブラリの仕事だからです。サンプルは生成した 1bpp を画面に表示します。これは実用的な使い方でもあって、**紙を使わずにレイアウトを確認できます**。

| example | 内容 |
| --- | --- |
| [HelloReceipt](HelloReceipt/) | レシートの最小例。1bpp を画面に出す `showBitmap()` が**その先のライブラリとの契約そのもの** |
| [HelloLabel](HelloLabel/) | ラベルの最小例。矩形配置、寄せ方、フィット、`mmToPx()` |
| [ReceiptWithBarcode](ReceiptWithBarcode/) | BarcodeKit 連携。Code 128 と EAN-13。**収まらないときに描かない**動作も見せる |
| [StreamBands](StreamBands/) | 長尺レシートを**ページを保持せずに**帯で流す。100 行の明細でメモリが増えないこと |
| [PrinterWidths](PrinterWidths/) | 印字可能幅の違いを見比べる。ボタン A で切り替え。**溢れたときに何が報告されるか** |

## 書き込み方

```sh
cd examples/HelloReceipt
arduino-cli compile --profile m5stack_core .
arduino-cli upload  --profile m5stack_core -p /dev/ttyUSB0 .
```

`sketch.yaml` の `fqbn` を書き換えれば他のボードでも動きます。LovyanGFX（または M5GFX）が動く環境が必要です。

`ReceiptWithBarcode` だけ BarcodeKit も要ります。BarcodeKit は未リリースなので `sketch.yaml` が `dir: ../../../BarcodeKit` で参照しています。**兄弟ディレクトリに BarcodeKit を置いてください。**

## 最初に決めること — 印字可能幅

**PaperCanvas に渡す幅が正しいかどうかが、他の何より効きます。** 小さすぎれば紙が無駄になり、大きすぎれば右端がプリンター側で切られます。切られたことはどこからも通知されません。

この値は**プリンターの仕様書にドット数で書かれている**のが普通です。紙幅より印字可能幅のほうが狭いためで、58mm 紙・203dpi を素直に換算すると 463 ドットになりますが、実際の 58mm プリンターはほぼ 384 ドットです。残りは余白。

したがって:

- **メーカーが出しているドット数を使う**のが第一
- `mmToPx()` は、物理寸法しか分からないとき（ラベル用紙のサイズなど）に使う

[PrinterWidths](PrinterWidths/) が幅ごとの見え方を並べます。

## 覚えておくと楽なこと

**文字列はライブラリ側にコピーされます。** `snprintf` した局所バッファをそのまま `addText()` / `addRow()` に渡して構いません。`build()` まで生かしておく必要はありません。

**設定は以後に追加する要素にのみ効きます。** `setFont()` / `setAlign()` を変えても、すでに積んだ要素は変わりません。`height()` を積みながら問えるのはこのためです。

**警告は生成を止めません。** 文字が切れた、画像が縮んだ、矩形がはみ出した、は `warnings()` のビットで返り、紙は出ます。指定ミス 1 つで印刷全体が失敗するほうが実運用では困るからです。

例外は**収まらないバーコード**で、これだけは描きません。読めないバーコードは、刷られてしまうと**スキャンするまで誰も気づけません**。

**`index` という名前のグローバル変数を作らないでください。** `<string.h>` の `index()` と衝突してコンパイルが通りません（`PrinterWidths` はこれを踏んだので `paperIndex` にしています）。

## 次に読むもの

- [../README.ja.md](../README.ja.md) — ライブラリの概要
- [../docs/GUIDE.ja.md](../docs/GUIDE.ja.md) — 入門ガイド。フォントの選び方、印刷結果が思ったとおりにならないときの確認手順
- [../docs/API.ja.md](../docs/API.ja.md) — 公開 API の一覧
