# フォント部品ライブラリ — 完成した

**[lgfx-font-tool](https://www.npmjs.com/package/lgfx-font-tool) として公開された。** この文書はその要求仕様だったもので、実物との対応と、PaperCanvas 側で決まったことを記録する。

| | |
| --- | --- |
| npm | [`lgfx-font-tool`](https://www.npmjs.com/package/lgfx-font-tool) v0.1.0、MIT、**依存ゼロ** |
| リポジトリ | https://github.com/tanakamasayuki/LGFXFontToolJs |
| Web アプリ | https://tanakamasayuki.github.io/LGFXFontToolJs/ （Viewer / Generator / Converter / Inspector） |

## 1. 要求と実物の対応

要求はすべて満たされている。**超えている点のほうが多い。**

| 要求（旧 §5.1） | 実物 |
| --- | --- |
| 各形式のデコーダ | `decodeU8g2` / `decodeGfx` / `decodeBdf` / `decodeVlw` / `decodeBff` / `decodeFontx2` / `decodeGlcd` / `decodeFixedBmp` / `decodeBmpFont` / `decodeRleFont` / `decodeCSource`、および**形式自動判別**の `decode` / `detect` |
| 各形式のエンコーダ | `encodeU8g2` / `encodeGfx` / `encodeBdf` / `encodeVlw` / `encodeBff` / `encodeFontx2` / `encodeCSource`、汎用 `encode` |
| 「入らない」を報告する | `canEncode` / `canEncodeU8g2` ほか。`encode` は収まらなければ `EncodeConstraintError` で**投げる**（切り詰めない） |
| サブセット | `subset` / `merge` |
| 描画 | `drawString` / `drawChar` |
| テキスト計測 | `textWidth` / `fontHeight` / `measureText` |
| datum | `DATUM` / `resolveDatum` |
| 検査 | `inspect` / `coverage` / `codepointRanges` / `estimateSize` / `estimateSizes` |
| TTF からの生成 | `loadTtf` / `rasterizeSet` / `measureTtf` / `generateFont` |
| 中立モデル | `createFont` / `getGlyph` / `createBitmap` / `serializeFont` / `deserializeFont` |

**要求に無かったもの**

- **FONTX2** の読み書き（Shift_JIS 対応つき）
- **C ソースの入出力** — GitHub で拾った `.h` をそのまま食える。`encodeCSource` は Arduino に貼れる形で出す
- **文字集合ユーティリティ** — `resolveCharset` / `TEMPLATES` / `splitBmp` ほか
- **内蔵 186 フォントのカタログ同梱** — `fontCatalog` / `loadFont`。抽出スクリプトを自分で書く必要が無くなった
- **4 つの Web アプリ**（Viewer / Generator / Converter / Inspector）

## 2. 正しさの担保 — 要求よりも強い

旧 §6 で 3 種類の検証を求めていた。実物はそれを**実測でやっている**。

- **LovyanGFX との一致**: `lang-ship:host` コアでネイティブビルドした**本物の LovyanGFX** に対して、**186 フォント全部・1,860 ケース**でバイト単位一致
- **エンコーダの実効性**: このライブラリが**書いた**フォントを本物の LovyanGFX に読ませて描かせるケースが 36 件
- フィクスチャがコミットされているので、`npm test` にネイティブビルドが要らない

旧 §6.3 で「往復だけでは仕様の解釈ズレが実機で化ける」と書いた懸念は、36 件の実物ロードで潰されている。

## 3. PaperCanvas から見て決まったこと

### 3.1 CJK フォントは自動取得される

npm パッケージには軽い 70 本（約 320KB）だけが入り、**CJK の大きいもの（合計 42MB）は初回 `loadFont` で GitHub Pages から取得される**。

PaperCanvas のブラウザツールは**日本語が主用途**なので、ここは必ず通る経路になる。

- 既定のまま使えば取得は自動。**オフラインでは動かない**
- 自前でホストするなら `configureFontData({ baseUrl: ... })`

**決めるべきこと**: PaperCanvas の Pages（`docs/`）に置くフォントを絞って同梱するか、既定の自動取得に任せるか。ラベル用途なら常用漢字のサブセットで足りる可能性が高い。

### 3.2 PaperCanvas 側に残る二重実装は「レイアウト」だけ

字形と送り幅はライブラリが出す。ツールが持つのは列幅の解決・折り返し位置・矩形配置だけになる。

これは [WEB_TOOL.ja.md](WEB_TOOL.ja.md) §3.6 のクロス検証（同じ JSON を C++ と JS に食わせて 1bpp を比較）で押さえる。**そのテストの守備範囲が狭まった**ぶん、確実性が上がっている。

### 3.3 v1.0 の見通し

[FONT_LIBRARY 旧 §8](#) で「このライブラリが v1.0 までの経路で最も長い依存」としていたが、**それが解消した。** 残るのは PaperCanvas のブラウザツール本体（[DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) フェーズ 5）と実機確認だけ。

## 4. 使い方の起点

```js
import { loadFont, createBitmap, drawString, textWidth, fontHeight }
  from 'lgfx-font-tool';

const font = await loadFont('lgfxJapanGothic_16');
const bmp  = createBitmap(textWidth(font, 'ご来店ありがとうございます'), fontHeight(font), 1);
drawString(bmp, font, 'ご来店ありがとうございます', 0, 0);
// bmp.data は 1bpp。PaperCanvas の Bitmap と同じ考え方
```

**ビット並びは PaperCanvas と完全に一致する。確認済み。**

```js
// lgfx-font-tool src/model/bitmap.js
const stride = bpp === 1 ? (width + 7) >> 3 : width;
return (bmp.data[y * bmp.stride + (x >> 3)] >> (7 - (x & 7))) & 1;   // getPixel
const mask = 0x80 >> (x & 7);                                        // setPixel
```

`stride = (width + 7) >> 3`、MSB first、行はバイト境界パディング。PaperCanvas の `Bitmap`（`rowBytes = (width + 7) / 8`、bit7 が左端）と同一なので、**`bmp.data` をそのままページへ合成できる。** 変換もコピーも要らない。

詳細は本家のドキュメント（Beginner / Use-case / Advanced / Specification、いずれも日英）。

## 5. 残る確認事項

`bmp.data` のビット並びは §4 のとおり**一致を確認済み**。残りは 3 点。

| 論点 | 内容 |
| --- | --- |
| CJK フォントの配布 | 自動取得に任せるか、`docs/` にサブセットを置くか（§3.1） |
| `setTextSize` 相当 | PaperCanvas は文字倍率を持つ。ライブラリ側の倍率規則と一致するか |
| ビルドレス方針との整合 | [WEB_TOOL.ja.md](WEB_TOOL.ja.md) §1 は npm を使わない前提。ESM を CDN から読むか、`docs/` に置くか |
