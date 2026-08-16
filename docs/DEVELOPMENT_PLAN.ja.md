# 開発計画

内部の記録。日本語のみ。現在地と残作業。

## 1. 現在地

**C++ ライブラリ側は完了。** 仕様どおり動き、テスト 13 本が通り、examples 5 本と利用者向けドキュメントが揃っている。

**残るのはフェーズ 5（ブラウザツール）と実機確認。** 待ちだったフォント部品ライブラリは [lgfx-font-tool](https://www.npmjs.com/package/lgfx-font-tool) として完成したので、**ブラウザツールに着手できる。**

| 項目 | 状況 |
| --- | --- |
| 概念・要件のたたき台（`memo.ja.md`） | 完了。**リリース前に削除する**（内容は本 docs へ移した） |
| 要件・設計・決定の文書化 | 完了 |
| レイアウト JSON の形式定義 | 完了 |
| リリースツールの導入 | 完了（`tools/bump_version.py` / `.github/workflows/release.yml`） |
| ブラウザツールの方針決め | 完了。置き場所・公開方法・フォント方針すべて確定（[WEB_TOOL.ja.md](WEB_TOOL.ja.md)） |
| **フェーズ 0 スパイク** | **完了。`tests/monopanel/` が通る**（[DECISIONS.ja.md](DECISIONS.ja.md) D3 に結果） |
| **フェーズ 1（出力の芯）** | **完了。** `Common.h` / `Dither.h` / `MonoPanel.h` + `tests/bitformat/` / `tests/dither/` |
| **フェーズ 2（レシート）** | **完了。** `Element.h` / `PageBase.h` / `Receipt.h` + `tests/receipt_layout/` / `tests/row/` |
| `addRow` | グリッドで実装済み。**API はレビュー待ち**（[CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) §4.2.1 末尾） |
| **フェーズ 3（ラベル）** | **完了。** `Label.h` + `tests/label_layout/` |
| **フェーズ 4（バーコード連携）** | **完了。** `PaperCanvas/Barcode.h` / `PaperCanvasBarcode.h` + `tests/barcode/`（zxing-cpp で往復デコード） |
| **テスト一式** | **完了。13 本すべて通る** |
| `docs/` のブラウザツール | **未着手。着手できる状態** |
| examples | **完了。5 本、すべてビルド確認済み** |
| 利用者向けドキュメント（README / GUIDE / API） | **完了。日英そろい** |

**途中リリースはしない。** ライブラリとブラウザツールが揃った時点で 1.0.0 として初回リリースする。

## 2. 実装順序

**スパイクを最初に置く。** ここが崩れると設計の土台（[DECISIONS.ja.md](DECISIONS.ja.md) D3）が変わるため、他の作業を積む前に確かめる。

### フェーズ 0 — スパイク（**完了**）

`tests/monopanel/` が 8 項目すべて通る。結果と、そこで見つかった実装上の落とし穴は [DECISIONS.ja.md](DECISIONS.ja.md) D3 にある。

分岐の判断は「期待どおり動く → フェーズ 1 へ」で確定した。退路（1bpp スプライト案）は使わない。

**このテストは以後も回し続ける。** `MonoPanel` は LovyanGFX の内部挙動に依存しているので、LovyanGFX や LGFXVirtualCanvas を上げたときに最初に壊れるのはここになる。

### フェーズ 1 — 出力の芯

3. `Common.h` — `Bitmap` / `Rect` / enum 群 / `mmToPx` / `rowBytes`
4. `Dither.h` — 閾値、Bayer 4x4 / 8x8。ページ絶対座標で引く
5. `MonoPanel.h` / `MonoSink.h` — 全ページ出力と帯コールバック出力
6. `tests/bitformat/`、`tests/dither/` — ビット並びとディザを固定する

この時点で「LovyanGFX で何か描いたら 1bpp が出てくる」ところまで通る。

### フェーズ 2 — レシート

7. `Element.h` / `PageBase.h` — 要素レコードと共通エンジン
8. `Receipt.h` — `addText` / `addSpace` / `addLine` / `addRule` / `addImage`、`height()` の逐次確定
9. `tests/receipt_layout/` — 分割数不変性・`build()` と `stream()` の一致・決定性もここに含める

**分割数不変性はこの段階で必ず通す。** 後回しにすると、分割起因の不具合がレイアウト実装の中に埋もれる。独立ディレクトリにしなかった理由は [TEST_PLAN.ja.md](TEST_PLAN.ja.md) §3。

10. `addRow`（グリッド）と `tests/row/`
11. 折り返しと `tests/text/`
12. 画像のスケール・フィットと `tests/image/`

### フェーズ 3 — ラベル

13. `Label.h` — 矩形配置、`Align` / `VAlign` / `Fit`
14. `tests/label_layout/`

内部エンジンは Receipt と共有するので、ここは薄い。

### フェーズ 4 — 連携

15. `PaperCanvas/Barcode.h` / `PaperCanvasBarcode.h` と `tests/barcode/`（zxing-cpp のデコードも同テスト内）
16. `tests/warnings/` / `tests/failure/`
17. `tests/build_lovyangfx/` / `tests/build_m5unified/`

### フェーズ 5 — ブラウザツール（[WEB_TOOL.ja.md](WEB_TOOL.ja.md)）

**依存していたフォント部品ライブラリは完成した** — [lgfx-font-tool](https://www.npmjs.com/package/lgfx-font-tool)（[FONT_LIBRARY.ja.md](FONT_LIBRARY.ja.md)）。**v1.0 までの経路で最も長い依存が解消したので、このフェーズは着手できる。**

18. ~~フォント描画ライブラリを別リポジトリで作る~~ **完了**（[lgfx-font-tool](https://www.npmjs.com/package/lgfx-font-tool)）
19. lgfx-font-tool を `docs/` から使えるようにする（[FONT_LIBRARY.ja.md](FONT_LIBRARY.ja.md) §5 の 4 点を決める）
20. `docs/index.html` — キャンバス設定、矩形の追加・ドラッグ・リサイズ、各種設定 UI
21. モノクロプレビュー
22. [LAYOUT_FORMAT.ja.md](LAYOUT_FORMAT.ja.md) の JSON 入出力と C++ コード生成
23. `tests/js_parity/` — **同じ JSON を C++ と JS に食わせて 1bpp 出力を比較する**（§3.6）
24. GitHub Pages を有効化（main / `docs`）

字形と送り幅は lgfx-font-tool が出すので、ツールが実装するのは**レイアウトだけ**になる。23 のクロス検証もその範囲を見ることになる。

### フェーズ 6 — 仕上げとリリース

**25〜27 はフォント部品ライブラリに依存しないので、フェーズ 5 を待たずに進める。25 と 26 は完了。**

25. ~~examples~~ **完了**（HelloReceipt / HelloLabel / ReceiptWithBarcode / StreamBands / PrinterWidths）
26. ~~利用者向けドキュメント~~ **完了**（README.ja/en、GUIDE.ja/en、API.ja/en、examples/README.ja/en）
27. `MANUAL_TEST.ja.md` と実機確認 — **次にやれること**
28. `tools/release_hooks/pre_release_commit.py` — ZIP からツール本体を除外
29. BarcodeKit を `dir:` からバージョン指定へ切り替え
30. **`CHANGELOG.md` の初回リリース項目を書く** — BarcodeKit の書式（`(EN)` / `(JA)` の対で、何をどういう理由でそうしたかを書く）。**リリース前は細かい作業ログを積まない。初回リリースの項目だけでよい**
31. **`memo.ja.md` を削除**
32. **v1.0.0 リリース**（初回リリース）

## 3. v1.0.0 のゴール

[REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) §13 の 11 項目がすべて動き、§15 の成功条件を満たすこと。特に、

- **メモリ上限を変えても出力が 1 ビットも変わらない**ことが CI で保証されている
- `build()` と `stream()` が同一バイト列を生成する
- 生成した 1bpp ページ上のバーコードが zxing-cpp でデコードできる
- **ブラウザツールのプレビューと C++ の出力の一致が CI で保証されている**
- 実機の感熱プリンターで印字し、実スキャナで読めることを手動確認済み

## 4. 依存の扱い

| ライブラリ | v1.0 での指定 | 備考 |
| --- | --- | --- |
| LovyanGFX | バージョン指定（`1.2.26`） | 必須 |
| LGFXVirtualCanvas | バージョン指定（`1.4.0`） | 必須。リリース済み |
| BarcodeKit | **`dir: ../../BarcodeKit`** | リリース前のため。**BarcodeKit のリリース後にバージョン指定へ切り替える**（フェーズ 4 の作業項目） |

## 5. 残っている検討事項

| 論点 | いつ決めるか |
| --- | --- |
| `addRow` の API 細部（1 列版の要否、行ごとの列指定、`Auto` の扱い、最大 8 列） | レビュー時。[CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) §4.2.1 末尾 |
| 要素リストを固定長にできるようにするか（`PAPERCANVAS_MAX_ELEMENTS`） | 実装せず。要求が出てから。[CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) §10 |
| 帯コールバックの帯高さを利用者が指定できるようにするか | フェーズ 1。プリンターの転送単位に合わせたい需要が出るかもしれない |
| `addRow` の 4 列以上（セル配列 API） | v1.1。用途が出てから |
| 列分割（縦帯）の露出 | v1.1 以降。用途が出てから |
| レイアウト JSON のパーサ | v1.1。依存ライブラリの選定から |
| lgfx-font-tool の取り込み方（4 点） | フェーズ 5 の着手時。[FONT_LIBRARY.ja.md](FONT_LIBRARY.ja.md) §5 |
| ツールのレシート対応 | フェーズ 5。ラベル専用で出すか |
| クロス検証テストに Node を入れるか | フェーズ 5。ビルドレス方針と衝突する唯一の箇所 |
