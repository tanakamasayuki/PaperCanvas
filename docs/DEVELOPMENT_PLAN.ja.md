# 開発計画

内部の記録。日本語のみ。現在地と残作業。

## 1. 現在地

**仕様策定が完了し、実装はこれから。** ソースコードはまだ 1 行も無い。

| 項目 | 状況 |
| --- | --- |
| 概念・要件のたたき台（`memo.ja.md`） | 完了。**リリース前に削除する**（内容は本 docs へ移した） |
| 要件・設計・決定の文書化 | 完了 |
| レイアウト JSON の形式定義 | 完了 |
| リリースツールの導入 | 完了（`tools/bump_version.py` / `.github/workflows/release.yml`） |
| ブラウザツールの方針決め | 置き場所と公開方法は確定。**フォント方針は未確定**（[WEB_TOOL.ja.md](WEB_TOOL.ja.md) §3） |
| `src/` の実装 | **未着手** |
| `tests/` の整備 | 未着手 |
| `docs/` のブラウザツール | 未着手 |
| examples | 未着手 |
| 利用者向けドキュメント（README / GUIDE / API） | 未着手 |

**途中リリースはしない。** ライブラリとブラウザツールが揃った時点で 1.0.0 として初回リリースする。

## 2. 実装順序

**スパイクを最初に置く。** ここが崩れると設計の土台（[DECISIONS.ja.md](DECISIONS.ja.md) D3）が変わるため、他の作業を積む前に確かめる。

### フェーズ 0 — スパイク（最優先）

1. `tests/monopanel/` を書く。`lgfx::Panel_Device` 派生の最小シンクを作り、`grayscale_8bit` のタイル sprite を `pushSprite` したときに `pixelcopy_t::fp_copy` が期待どおりグレー値を供給するか確認する
   - 既知のグレー勾配を流し込み、**二値化前**の行バッファが一致すること
   - フック自体が効いていることを先に確認する（非ゼロのグレー値が観測できること）
2. `LGFXVirtualScreen` にこのシンクを渡し、タイルが正しい `y` 位置で届くこと、端数タイルが正しいことを確認する

**結果によって分岐する。**

- 期待どおり動く → フェーズ 1 へ
- `fp_copy` がグレーを供給しない → `MonoPanel` 側で RGB から自前で輝度計算する（`grayscale_t` の係数 `(r + 2g + b) / 4` に合わせる）。設計は変わらない
- `Panel_Device` 派生が `LGFXVirtualScreen` から使えない → D3 の退路（1bpp スプライトをパネル役にする）へ切り替え、ディザと帯出力を v1.1 送りにする。**公開 API は変えない**

### フェーズ 1 — 出力の芯

3. `Common.h` — `Bitmap` / `Rect` / enum 群 / `mmToPx` / `rowBytes`
4. `Dither.h` — 閾値、Bayer 4x4 / 8x8。ページ絶対座標で引く
5. `MonoPanel.h` / `MonoSink.h` — 全ページ出力と帯コールバック出力
6. `tests/bitformat/`、`tests/dither/` — ビット並びとディザを固定する

この時点で「LovyanGFX で何か描いたら 1bpp が出てくる」ところまで通る。

### フェーズ 2 — レシート

7. `Element.h` / `PageBase.h` — 要素レコードと共通エンジン
8. `Receipt.h` — `addText` / `addSpace` / `addLine` / `addRule` / `addImage`、`height()` の逐次確定
9. `tests/split_invariance/`、`tests/build_stream/`、`tests/receipt_layout/`、`tests/determinism/`

**`split_invariance/` はこの段階で必ず通す。** ここを後回しにすると、分割起因の不具合がレイアウト実装の中に埋もれる。

10. **`addRow` の API を確定させる**（[CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) §4.2.1 の論点表を潰す）→ 実装 → `tests/row/`
11. 折り返しと `tests/text/`
12. 画像のスケール・フィットと `tests/image/`

### フェーズ 3 — ラベル

13. `Label.h` — 矩形配置、`Align` / `VAlign` / `Fit`
14. `tests/label_layout/`

内部エンジンは Receipt と共有するので、ここは薄い。

### フェーズ 4 — 連携

15. `PaperCanvasBarcode.h` と `tests/barcode/` / `tests/barcode_decode/`
16. `tests/warnings/` / `tests/failure/`
17. `tests/build_lovyangfx/` / `tests/build_m5unified/`

### フェーズ 5 — ブラウザツール（[WEB_TOOL.ja.md](WEB_TOOL.ja.md)）

18. **フォント方針を決める**（T1 / T2 / T3）。§3.4 の論点表を潰す
19. フォントメトリクス／グリフアトラスのホスト吸い出しハーネス（`tests/manual/font_introspect/` 相当）
20. `docs/index.html` — キャンバス設定、矩形の追加・ドラッグ・リサイズ、各種設定 UI
21. モノクロプレビュー
22. [LAYOUT_FORMAT.ja.md](LAYOUT_FORMAT.ja.md) の JSON 入出力と C++ コード生成
23. `tests/js_parity/` — **同じ JSON を C++ と JS に食わせて 1bpp 出力を比較する**（§3.6）
24. GitHub Pages を有効化（main / `docs`）

ライブラリ側が固まってから着手する。フォント吸い出しハーネスはライブラリの実装に依存しない（LovyanGFX だけを見る）ので、19 だけは前倒しできる。

### フェーズ 6 — 仕上げとリリース

25. examples（レシート最小・ラベル最小・バーコード付き・帯出力・M5Unified）
26. 利用者向けドキュメント（README.ja/en、GUIDE、API）
27. `MANUAL_TEST.ja.md` と実機確認
28. `tools/release_hooks/pre_release_commit.py` — ZIP からツール本体を除外
29. BarcodeKit を `dir:` からバージョン指定へ切り替え
30. **`memo.ja.md` を削除**
31. **v1.0.0 リリース**（初回リリース）

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
| **`addRow` の API 形（グリッド／簡易ヘルパーの切り分け）** | **フェーズ 2 の実装前。[CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) §4.2.1 に論点表がある** |
| `PAPERCANVAS_MAX_ELEMENTS` の既定（動的確保するか固定長か） | フェーズ 2。実装しながら決める |
| 帯コールバックの帯高さを利用者が指定できるようにするか | フェーズ 1。プリンターの転送単位に合わせたい需要が出るかもしれない |
| `addRow` の 4 列以上（セル配列 API） | v1.1。用途が出てから |
| 列分割（縦帯）の露出 | v1.1 以降。用途が出てから |
| レイアウト JSON のパーサ | v1.1。依存ライブラリの選定から |
| **ブラウザツールのフォント方針（T1 / T2 / T3）** | **フェーズ 5 の着手前。[WEB_TOOL.ja.md](WEB_TOOL.ja.md) §3.4** |
| ツールのレシート対応 | フェーズ 5。ラベル専用で出すか |
| クロス検証テストに Node を入れるか | フェーズ 5。ビルドレス方針と衝突する唯一の箇所 |
