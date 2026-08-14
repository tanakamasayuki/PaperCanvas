# ブラウザツール

内部の設計記録。日本語のみ。ラベル／レシートのレイアウトをブラウザで組み、PaperCanvas が生成するのと同じ 1bpp ビットマップを確認するためのツール。

**このリポジトリに含める。** GitHub Pages で公開する。

公開 URL: **https://tanakamasayuki.github.io/PaperCanvas/**

参考にする兄弟プロジェクト:

- [phomemo-d30-web-print](https://github.com/tanakamasayuki/phomemo-d30-web-print) — 想定する UI と規模。`docs/` に素の HTML/JS/CSS
- [LGFXScreenBuilder](https://github.com/tanakamasayuki/LGFXScreenBuilder) — **同じフォント問題を先に踏んでいる**。§3 で扱う
- [esp-flashjs](https://github.com/tanakamasayuki/esp-flashjs) — Pages のビルドと公開の作法

## 1. 置き場所と公開方法

```text
docs/
  index.html          ツール本体（Pages のトップ）
  index.css
  src/*.js
  assets/             フォントデータ等
  *.ja.md / *.md      設計文書（このファイルを含む）
```

**`docs/` に置き、GitHub Pages の「Deploy from a branch: main / `docs`」で公開する。Action は使わない。**

公開先は **https://tanakamasayuki.github.io/PaperCanvas/** （`docs/index.html` がトップ）。パス参照はすべて `docs/` からの相対にする。リポジトリ名がパスに入るため、絶対パス（`/src/...`）を書くとローカルでは動いて公開先で 404 になる。

- 兄弟 2 つ（LGFXScreenBuilder / phomemo-d30-web-print）がこの形。esp-flashjs が Action を使っているのは npm ビルド（`web/` → `site/`）があるからで、ビルドレスなら設定 1 箇所で済む
- ビルド工程を持たないので、Arduino ライブラリのリポジトリに Node プロジェクトを併設せずに済む
- 設計文書（`.md`）も同じ `docs/` に同居する。Pages はそれらもそのまま配信するが実害はない

**リリース ZIP からの除外。** リリースワークフローは `release` ブランチで `tests/` を消して ZIP 化する。ツール本体は Arduino 利用者には不要なので、`tools/release_hooks/pre_release_commit.py` で `docs/index.html` / `docs/index.css` / `docs/src/` / `docs/assets/` を `git rm` する。設計文書の `.md` は残す。

## 2. 機能

memo §10 から引き継ぐもの。

- ラベル幅・高さ・解像度（dpi）の設定。レシートは幅のみ
- テキスト矩形・画像矩形・バーコード矩形の追加
- ドラッグ移動とリサイズ
- 寄せ方、スケール、フォント、モノクロ化方式の設定
- **モノクロプレビュー**（§3）
- [LAYOUT_FORMAT.ja.md](LAYOUT_FORMAT.ja.md) の JSON 入出力、および C++ コード例の生成
- プリンターへの直接印刷は補助機能。PaperCanvas 本体の要件には含めない（Web Bluetooth / Web Serial）

## 3. フォント問題

> **方針は §3.4 で確定した。実装は別リポジトリ**（[FONT_LIBRARY.ja.md](FONT_LIBRARY.ja.md)）。

### 3.1 問題

ツールのプレビューは「PaperCanvas が生成するビットマップと同じもの」でなければ意味がない。しかし素直に作ると、**レイアウト計算とフォント描画を C++ と JS で二重実装**することになる。フォントメトリクスがわずかに違うだけで文字位置がずれ、**「ツールで見たとおりに刷れない」という一番困る壊れ方**をする。

### 3.2 LGFXScreenBuilder の到達点

同じ問題を先に踏んでいる。現在の実装は次のとおり。

- `tests/manual/font_introspect` というホストハーネスが、**実際の LovyanGFX からフォント情報を吸い出して** `docs/src/font-metrics.json`（46KB）と `docs/src/font-atlas.png` を生成する
- ブラウザ（`docs/src/fonts.js`）はそれを遅延読み込みし、フォント選択 UI とプレビューに使う
- ただし中身は**フォント単位の集約値**（`height` / `baseline` / `xAdvance` / ASCII・CJK のカバレッジ / サンプル画像の box / フラッシュ消費量）であり、**グリフごとの送り幅ではない**
- 別途 `docs/fontgen.html` が TTF から u8g2 フォント（`lgfx::U8g2font`）を生成する。その設計判断は [LGFXScreenBuilder の FONT_FORMATS.ja.md](https://github.com/tanakamasayuki/LGFXScreenBuilder/blob/main/docs/FONT_FORMATS.ja.md) に実測込みでまとまっている

つまり「フォント選択と概観」までは解決済み、「文字位置の厳密一致」は未解決、という状態。

### 3.3 PaperCanvas の事情は少し違う

**厳密さの要求が高い**

画面プレビューのずれは「書き込んで見る」で確認できるが、印刷は**紙を消費する**。しかも主用途がラベルで、位置がずれたら成果物として使えない。

**一方で、完全一致が原理的に到達可能**

出力が **1bpp でアンチエイリアスなし**。LGFXScreenBuilder が妥協せざるを得なかった理由の一つ（AA のブレンド差という逃げ場のない誤差源）が、こちらには無い。画素は黒か白しかないので、「完全一致」は達成でき、しかも**達成できたかどうかを 1 ビット単位で検証できる**。

これは [TEST_PLAN.ja.md](TEST_PLAN.ja.md) の分割数不変性テストと同じ構造で、同じ道具立てが使える。

### 3.4 方針 — 共有ライブラリに切り出す（**別リポジトリ**）

同じ問題を LGFXScreenBuilder も抱えており、今後のツールも抱える。各プロジェクトで解けば同じ間違いを 3 回することになるので、**「LovyanGFX が描くのと同じ文字をブラウザで描く」部分を別リポジトリのライブラリに切り出す。**

要求仕様は [FONT_LIBRARY.ja.md](FONT_LIBRARY.ja.md)。要点だけここに再掲する。

**Web フォント（TTF / WOFF）を使う案は採らない。** ブラウザのラスタライザは LovyanGFX のそれとは別物で、同じ書体を渡してもグリフのビットマップは一致しない。1bpp でアンチエイリアスが無い印刷用途では、その差がそのまま「線が 1px ずれる」として出る。そもそも LovyanGFX の内蔵フォントは TTF ではない。

**採るのは「同じフォントデータを、同じ規則でデコードする」案。** `U8g2font` は生の u8g2 バイト列を保持しているだけで、**efont / lgfxJapan* 系の CJK フォントはすべてこれ**である。JS 側の u8g2 デコーダは同じバイト列を読むので、グリフのビットマップは定義上一致する。近似ではなく同一になる。

fontgen（LGFXScreenBuilder）が出力するのも u8g2 なので、利用者が作ったフォントも同じ経路に乗る。「全部 u8g2 に寄せる」ことの本当の利点は変換ではなく、**経路が 1 本になる**ことにある。

**PaperCanvas 側への影響**: ブラウザツールの完成はこのライブラリの完成待ちになる。ライブラリ本体（C++）はこの話に一切影響されない。

### 3.5 以前検討した案（記録）

アトラス吸い出し（ホストで 1bpp グリフアトラスを焼いてブラウザが貼る）を有力と見ていたが、§3.4 で不要になった。アトラスを焼く代わりに同じフォントデータを読めばよく、そのほうがフォントの追加・サブセット・fontgen 出力に対して素直である。

### 3.6 クロス検証（どの段階でも必要）

選んだ段階に関係なく、**同じ JSON を C++ と JS の双方に食わせて 1bpp 出力を比較する**テストを置く。

```text
tests/js_parity/
  cases/*.json  →  C++ host テスト  →  out_cpp.png
                →  node docs/src/   →  out_js.png
  assert バイト列が完全一致
```

字形はフォント描画ライブラリが保証するので、ここで押さえるのは**レイアウト**（列幅の解決、折り返し位置、矩形配置）が C++ と JS で一致することになる。ツールとライブラリが別々に育ってもずれた瞬間に落ちる。

**ただしビルドレス方針と衝突する。** このテストだけは Node が要る。テストは `release` ブランチから除外されるので配布には影響しないが、CI に Node のセットアップが増える。ここも決める必要がある。

## 4. 未確定のまま残すこと

- フォント描画ライブラリの着手と完成（[FONT_LIBRARY.ja.md](FONT_LIBRARY.ja.md) §7）。**v1.0 までの経路で最も長い依存**
- レシート対応をツールに含めるか（memo §10 はラベル専用と書いている。矩形配置とは UI が別物になる）
- プリンターへの直接印刷（Web Bluetooth / Web Serial）を v1.0 に含めるか
- i18n（LGFXScreenBuilder は日英対応の検査スクリプトまで持っている）
