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

> **解決した。** [lgfx-font-tool](https://www.npmjs.com/package/lgfx-font-tool) を使う（§3.4）。§3.1〜3.3 はそこに至った経緯。

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

### 3.4 解決した — [lgfx-font-tool](https://www.npmjs.com/package/lgfx-font-tool) を使う

**別リポジトリの部品ライブラリとして完成し、公開された。** 経緯と実物との対応は [FONT_LIBRARY.ja.md](FONT_LIBRARY.ja.md)。

要点だけ。

- **字形は本物の LovyanGFX に対してバイト単位で検証されている** — 186 フォント全部・1,860 ケース。「だいたい合う」ではない
- 内蔵 186 フォントのカタログを同梱。抽出スクリプトは要らない
- `drawString` / `textWidth` / `fontHeight` / `measureText` / `DATUM` — PaperCanvas のツールが必要とするものが揃っている
- MIT、依存ゼロ
- **1bpp のビット並びが PaperCanvas と同一**（`stride = (width+7)>>3`、MSB first）。`bmp.data` をそのままページへ合成できる

**したがってツールに残る二重実装はレイアウトだけになった。** 列幅の解決、折り返し位置、矩形配置。字形と送り幅はライブラリが出す。

### 3.5 以前検討した案（記録）

- *ブラウザで Web フォント（TTF / WOFF）を使う* — ラスタライザが別物で字形が一致しない
- *ホストで 1bpp グリフアトラスを焼いてブラウザが貼る* — 方向は正しかったが、フォントを増やすたび焼き直しが要る
- *全フォントを u8g2 へ変換して JS は u8g2 デコーダだけ持つ* — 75px の GFXfont / RLEfont が u8g2 の 7bit 送り幅に入らない
- *ホストで LovyanGFX に描かせて採取し、JS は中間形式だけ読む* — ブラウザで生成した u8g2 のプレビューに C++ の往復が要る。**ただしこの「LovyanGFX をオラクルにする」考え方は、lgfx-font-tool の検証手法として実現している**

### 3.6 クロス検証（どの段階でも必要）

選んだ段階に関係なく、**同じ JSON を C++ と JS の双方に食わせて 1bpp 出力を比較する**テストを置く。

```text
tests/js_parity/
  cases/*.json  →  C++ host テスト  →  out_cpp.png
                →  node docs/src/   →  out_js.png
  assert バイト列が完全一致
```

字形は lgfx-font-tool が保証するので、ここで押さえるのは**レイアウト**（列幅の解決、折り返し位置、矩形配置）が C++ と JS で一致することだけになる。**守備範囲が狭まったぶん確実性が上がっている。**

**ただしビルドレス方針と衝突する。** このテストだけは Node が要る。テストは `release` ブランチから除外されるので配布には影響しないが、CI に Node のセットアップが増える。ここも決める必要がある。

## 4. 未確定のまま残すこと

- lgfx-font-tool の取り込み方（[FONT_LIBRARY.ja.md](FONT_LIBRARY.ja.md) §5）— CJK フォントを自動取得に任せるか `docs/` に置くか、文字倍率の規則が一致するか、ビルドレス方針のもとで ESM をどう読むか
- レシート対応をツールに含めるか（memo §10 はラベル専用と書いている。矩形配置とは UI が別物になる）
- プリンターへの直接印刷（Web Bluetooth / Web Serial）を v1.0 に含めるか
- i18n（LGFXScreenBuilder は日英対応の検査スクリプトまで持っている）
