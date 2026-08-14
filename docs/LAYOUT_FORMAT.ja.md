# レイアウト定義フォーマット

ラベル専用ブラウザツールと組み込み側で共有する中間形式の定義。

**v1.0 ではこの形式の定義のみを提供する。組み込み側のパーサは v1.1 以降。** 理由は [DECISIONS.ja.md](DECISIONS.ja.md) D12。ブラウザツールはこの形式に従って出力し、利用者は当面「JSON を見ながら C++ を書く」か「ツールが生成した C++ コードを貼る」ことになる。

## 1. 方針

- 座標・サイズはすべて **px**（[REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) §9）
- 物理寸法はメタ情報として保持するが、**描画には使わない**。`dpi` は px との換算とツールの表示にのみ用いる
- 値が省略された場合は PaperCanvas の既定値と一致させる。ツールは既定値を出力しなくてよい
- **前方互換**: 未知のキーは無視する。未知の `type` を持つ要素は無視し、警告する
- フォントは**名前で参照する**。フォント資産は同梱しない（D8）ので、名前から `IFont*` への解決は利用者または生成コードの責務

## 2. 全体構造

```json
{
  "papercanvas": 1,
  "kind": "label",
  "canvas": {
    "width": 400,
    "height": 240,
    "dpi": 203,
    "mm": { "width": 50.0, "height": 30.0 }
  },
  "defaults": {
    "font": "efontJA_16",
    "size": 1.0,
    "mono": "threshold",
    "threshold": 128
  },
  "elements": [ ]
}
```

| キー | 必須 | 内容 |
| --- | --- | --- |
| `papercanvas` | ○ | 形式バージョン。現在 `1` |
| `kind` | ○ | `"label"` または `"receipt"` |
| `canvas.width` / `canvas.height` | ○ | px。`kind` が `receipt` のとき `height` は省略可（内容で決まる） |
| `canvas.dpi` | | 既定 203。換算と表示のみに使う |
| `canvas.mm` | | 参考情報。読み込み側は無視してよい |
| `defaults` | | 各要素で省略された値の既定 |
| `elements` | ○ | 描画要素の配列。**配列順が描画順**（後のものが上に重なる） |

## 3. 要素の共通キー

```json
{ "type": "text", "id": "price", "rect": [10, 10, 200, 40] }
```

| キー | 内容 |
| --- | --- |
| `type` | `text` / `row` / `image` / `barcode` / `rect` / `line` / `space` / `rule` |
| `id` | 任意。ツール上の識別子。組み込み側は無視してよい |
| `rect` | `[x, y, w, h]`。**`kind` が `label` のとき必須**。`receipt` では無視される（縦積みのため） |

`kind` が `receipt` の場合、要素は配列順に上から積まれ、`rect` は使わない。

## 4. 要素ごとの定義

### 4.1 `text`

```json
{
  "type": "text",
  "rect": [10, 10, 380, 40],
  "text": "ご来店ありがとうございます",
  "font": "efontJA_16",
  "size": 1.0,
  "align": "center",
  "valign": "top",
  "lineSpacing": 0,
  "wrap": false,
  "invert": false
}
```

`align`: `left` / `center` / `right`。`valign`: `top` / `middle` / `bottom`。
`text` 中の `\n` は明示改行として保持する。

### 4.2 `row`（**未確定**）

> C++ 側の API が未確定（[CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) §4.2.1）。この形式もそれに合わせて確定させる。以下は方向性のイメージ。

```json
{
  "type": "row",
  "rect": [10, 60, 380, 20],
  "columns": [
    { "width": "55%", "align": "left" },
    { "width": "15%", "align": "center" },
    { "width": "rest", "align": "right", "leader": "." }
  ],
  "cells": ["コーヒー", "x2", "¥960"]
}
```

`columns[i].width` は `"120px"` / `"55%"` / `"rest"` のいずれか。`cells` は `columns` と同じ要素数。空文字列のセルは幅 0 として扱う。

1 列・2 列の簡略記法（`columns` を省略して `cells` だけ書く）を認めるかは、C++ 側の簡易ヘルパーの扱いと合わせて決める。

### 4.3 `image`

```json
{
  "type": "image",
  "rect": [0, 90, 400, 100],
  "src": "logo",
  "align": "center",
  "valign": "middle",
  "fit": "contain",
  "scale": 1.0,
  "mono": "bayer4x4",
  "threshold": 128,
  "invert": false
}
```

`fit`: `none` / `contain` / `cover` / `stretch` / `scale`。
`mono`: `threshold` / `bayer4x4` / `bayer8x8`。

**`src` は画像データそのものを持たない。** 名前による参照であり、解決は利用者の責務。JSON に画像を埋め込むと、組み込み側でデコードとメモリ確保が必要になり、v1.0 の範囲を大きく超えるため。

ツールがプレビュー用に画像を保持したい場合は `preview` キー（data URI）を足してよい。組み込み側は無視する。

### 4.4 `barcode`

```json
{
  "type": "barcode",
  "rect": [50, 200, 300, 60],
  "format": "code128",
  "data": "ABC-12345",
  "moduleWidth": 0,
  "barHeight": 60,
  "quietZone": true,
  "align": "center"
}
```

`format`: BarcodeKit の形式名を小文字で。`code39` / `code93` / `code128` / `ean8` / `ean13` / `upca` / `upce` / `itf` / `itf14` / `codabar` / `qrcode`。

`moduleWidth` が `0` なら矩形に収まる最大の**整数倍率**を自動で選ぶ。

QR の場合は `ecc`（`L` / `M` / `Q` / `H`）を追加できる。

### 4.5 `rect` / `line`

```json
{ "type": "rect", "rect": [0, 0, 400, 240], "fill": false, "thickness": 2 }
{ "type": "line", "from": [0, 100], "to": [400, 100], "thickness": 1 }
```

### 4.6 `space` / `rule`（`receipt` のみ）

```json
{ "type": "space", "height": 8 }
{ "type": "rule", "char": "-" }
```

## 5. 完全な例（ラベル）

```json
{
  "papercanvas": 1,
  "kind": "label",
  "canvas": { "width": 400, "height": 240, "dpi": 203,
              "mm": { "width": 50.0, "height": 30.0 } },
  "defaults": { "font": "efontJA_16", "size": 1.0, "mono": "threshold" },
  "elements": [
    { "type": "rect", "rect": [0, 0, 400, 240], "fill": false, "thickness": 2 },
    { "type": "text", "rect": [12, 12, 376, 24],
      "text": "産地直送", "align": "center" },
    { "type": "row",  "rect": [12, 44, 376, 20],
      "cells": ["トマト", "1kg", "¥580"] },
    { "type": "image", "rect": [12, 70, 120, 100],
      "src": "farm_logo", "fit": "contain" },
    { "type": "barcode", "rect": [140, 70, 248, 100],
      "format": "ean13", "data": "4901234567894", "barHeight": 80 },
    { "type": "text", "rect": [12, 180, 376, 48],
      "text": "賞味期限 2026-09-01\n要冷蔵 10℃以下",
      "size": 0.75, "align": "left" }
  ]
}
```

## 6. バージョニング

`papercanvas` の値を形式バージョンとする。

- **後方互換な追加**（新しい任意キー、新しい `type`）ではバージョンを上げない。読み込み側は未知のキー／`type` を無視する
- **既存キーの意味を変える**、または必須キーを追加する場合にバージョンを上げる
- 読み込み側は、自分が知っているバージョンより大きい値を見たら**警告して読み込みを続ける**（既知のキーだけ解釈する）。拒否はしない

## 7. v1.1 以降で決めること

- 組み込み側パーサの依存ライブラリ（ArduinoJson か自前の最小パーサか）
- 画像・フォント参照の解決方法（名前 → ポインタの登録テーブル API）
- ツールからの直接印刷（Web Bluetooth / Web Serial）
- レシート用のツール対応（v1.0 のツールはラベル専用）
