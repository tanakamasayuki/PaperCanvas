# ブラウザツール

内部の設計記録。日本語のみ。レシート／ラベルのレイアウトをブラウザで組み、**PaperCanvas が生成するのと同じ 1bpp を確認し、C++ のソーステンプレートを出力する**ためのツール。

公開 URL: **https://tanakamasayuki.github.io/PaperCanvas/**

## 1. 何を作るか

**レイアウトを組む道具ではなく、コードを生む道具である。** 出力は画像ではなく **C++ のソース**で、利用者はそれをスケッチに貼り、データを構造体に詰めて呼ぶ。

```text
ブラウザでレイアウトを組む
  → プレビュー（1bpp。実際の印字と一致）
  → C++ ソースを出力
       ├ データ構造体の定義
       ├ レイアウトを組み立てる関数
       └ 画像は 1bpp で埋め込み済み
  → スケッチに貼り、データを入れて呼ぶ → 印刷される
```

### 1.1 ページ構成

**ラベルとレシートは別ページにする。** 利用モデルが違う（矩形配置 vs 縦積みと繰り返し）ので、同じ UI に押し込むと両方が使いにくくなる。

```text
docs/
  index.html      入口。どちらを作るか選ぶ
  label.html      ラベル：固定キャンバスに矩形を置く
  receipt.html    レシート：上から積む。明細は表として繰り返す
  css/app.css
  src/            共有のモデル・レンダラ・コード生成
  vendor/         lgfx-font-tool の dist
```

## 2. 出力する C++ の形

**表示するデータを構造体にし、そこへ値を入れて呼べば印刷される。** これが出力の中心。

### 2.1 ラベル

```cpp
// 生成: MyLabel.h
struct MyLabelData {
  const char* title;
  const char* itemName;
  const char* weight;
  const char* price;
  const char* bestBefore;
};

inline bool buildMyLabel(PaperCanvas::Label& lb, const MyLabelData& d);
```

```cpp
MyLabelData d = {"産地直送", "トマト", "1kg", "580", "2026-09-01"};
PaperCanvas::Label lb(400, 240);
buildMyLabel(lb, d);
lb.build(page, sizeof(page));
```

### 2.2 レシート — 明細は表として繰り返す

レシートの本質は**件数が実行時に決まる明細**である。ツールでは**列を定義し、その列に対する 1 行**をデザインする。生成コードは配列を受け取り、**件数ぶん繰り返して出力する**。

```cpp
// 生成: MyReceipt.h
struct MyReceiptItem {      // 明細 1 行ぶん。列の数だけフィールドが出る
  const char* name;
  const char* qty;
  const char* price;
};

struct MyReceiptData {
  const char* shopName;
  const char* datetime;
  const MyReceiptItem* items;   // 明細
  size_t itemCount;
  const char* total;
};

inline bool buildMyReceipt(PaperCanvas::Receipt& r, const MyReceiptData& d);
```

```cpp
MyReceiptItem items[] = {
  {"コーヒー",     "x2", "960"},
  {"サンドイッチ", "x1", "620"},
};
MyReceiptData d = {"PaperCanvas Cafe", "2026-08-17", items, 2, "1580"};

PaperCanvas::Receipt r(384);
buildMyReceipt(r, d);
r.build(page, sizeof(page));
```

**表としての設定項目**

| 設定 | 内容 |
| --- | --- |
| 列 | 何列に分けるか。各列の幅（px / % / 残り / 内容）と寄せ。`Column` にそのまま対応する |
| ヘッダ行 | **表示するかどうか。** する場合の各列の見出し文字列 |
| 明細行 | 各列に入るフィールド名（`Item` 構造体のフィールドになる） |
| 区切り | ヘッダの下、明細の後に罫線を入れるか |

ヘッダ行を出さない選択ができることが要る。レシートの明細に見出しを付けない運用は普通にある。

### 2.3 静的なテキストと差し込むテキスト

要素ごとに選ぶ。

- **静的** — 生成コードに文字列リテラルとして埋まる（「合計」「ご来店ありがとうございます」）
- **差し込み** — 構造体のフィールドになる（店名、金額、日時）

**この区別がツールの中心的な操作**であり、構造体の形はここから決まる。

### 2.4 画像は 1bpp で埋め込む

**ブラウザ側で減色まで済ませ、1bpp のバイト配列としてソースに埋め込む。**

```cpp
// 生成: 48x32、1bpp、bit=1 が黒、MSB first
static const uint8_t MyLabel_logo[] = { 0x00, 0x3c, ... };
static const PaperCanvas::Bitmap MyLabel_logoBmp = { MyLabel_logo, 48, 32, 6 };
```

デバイス側は**そのまま刷る**。ライブラリのディザは通らない。

これが成立するのは、`Fit::None`（1:1 サンプリング）と `Mono::Threshold` の組み合わせが 1bpp 入力に対して**ビット保存**だからである。`tests/image/` の `mono1bpp_passthrough` がそれを固定している。**この経路が壊れるとツールのプレビューが黙って紙と食い違う**ので、テストで押さえてある。

ライブラリ側のディザ（`Mono::Bayer*`）も残す。実行時に取得した画像を刷る用途にはそちらが要る。**ツールが出すのは常に加工済み**、という切り分け。

## 3. プレビュー — 実際の印字と一致させる

字形は [lgfx-font-tool](https://www.npmjs.com/package/lgfx-font-tool) が出す。**本物の LovyanGFX に対して 186 フォント・1,860 ケースでバイト単位検証済み**なので、ここは近似ではない。

PaperCanvas 側に残るのは**レイアウト**（列幅の解決、折り返し位置、矩形配置、積み上げ）だけで、それは `tests/js_parity/` が C++ と突き合わせる。

### 3.1 画像への書き出し

プレビューは PNG として保存できるようにする。**ホストの LGFX でレンダリングした結果と一致する**ことが要件で、それは上の 2 つ（字形はライブラリ、レイアウトは js_parity）で担保される。

### 3.2 到達した経緯

- *Web フォント（TTF / WOFF）* — ブラウザのラスタライザは LovyanGFX と別物で字形が一致しない
- *ホストで 1bpp グリフアトラスを焼く* — 方向は正しかったが、フォントを増やすたび焼き直しが要る
- *全フォントを u8g2 へ変換* — 75px の GFXfont / RLEfont が u8g2 の 7bit 送り幅に入らない
- *ホストで採取して JS は中間形式だけ読む* — ブラウザで作った u8g2 のプレビューに C++ の往復が要る。ただし**この「LovyanGFX をオラクルにする」考え方は lgfx-font-tool の検証手法として実現している**

## 4. 置き場所と公開方法

**`docs/` に置き、GitHub Pages の「Deploy from a branch: main / `docs`」で公開する。Action は使わない。**

- ビルド工程を持たない。Arduino ライブラリのリポジトリに Node プロジェクトを併設せずに済む
- パス参照はすべて相対。リポジトリ名がパスに入るので、絶対パス（`/src/...`）はローカルで動いて公開先で 404 になる
- lgfx-font-tool は `docs/vendor/` に `dist` を置く。**CDN から読まない** — オフラインで動き、バージョンが固定される
- CJK フォントは初回 `loadFont` で `tanakamasayuki.github.io` から取得される。**PaperCanvas の Pages と同一オリジン**なので同梱する意味がない（[FONT_LIBRARY.ja.md](FONT_LIBRARY.ja.md) §3.1）

**リリース ZIP からの除外。** `tools/release_hooks/pre_release_commit.py` で `docs/*.html` / `docs/css/` / `docs/src/` / `docs/vendor/` を `git rm` する。設計文書の `.md` は残す。

## 5. やらないこと

- **プリンターへの直接印刷。** 機種が多すぎる。出力は C++ ソースまでで、送信は利用者の責務（ライブラリの責務境界と同じ）
- レイアウト定義 JSON の組み込み側パース（v1.1。[LAYOUT_FORMAT.ja.md](LAYOUT_FORMAT.ja.md)）
- 自動レイアウト

## 6. 実装の順序

1. `vendor/` に lgfx-font-tool、共有のモデル・レンダラ
2. **レシート**（`receipt.html`）— 縦積みと表の繰り返し。用途として先に来る
3. **ラベル**（`label.html`）— 矩形配置
4. 画像の取り込みと 1bpp 化
5. C++ 出力
6. PNG 書き出し
7. JSON 入出力（[LAYOUT_FORMAT.ja.md](LAYOUT_FORMAT.ja.md)）
8. GitHub Pages を有効化

## 7. 未確定

- i18n（LGFXScreenBuilder は日英対応の検査スクリプトまで持っている）
- レイアウト JSON にデータ束縛と繰り返しをどう表現するか（[LAYOUT_FORMAT.ja.md](LAYOUT_FORMAT.ja.md) の拡張）
