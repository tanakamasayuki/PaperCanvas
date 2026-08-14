# ドキュメント案内

どの文書を、どの順で読むかの案内です。

**言語方針は 3 段に分けています。正本は日本語版です。** BarcodeKit と同じ方針。

| 区分 | 言語 | 対象 |
| --- | --- | --- |
| 使う人が読むもの | 日英 | `../README.ja.md`、`GUIDE.ja.md`、`API.ja.md`、`../examples/README.ja.md`、`../tests/README.ja.md` |
| 確定した仕様 | 日英 | [LAYOUT_FORMAT.ja.md](LAYOUT_FORMAT.ja.md) |
| 内部の記録・作業メモ | 日本語のみ | [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md)、[CORE_DESIGN.ja.md](CORE_DESIGN.ja.md)、[DECISIONS.ja.md](DECISIONS.ja.md)、[TEST_PLAN.ja.md](TEST_PLAN.ja.md)、[DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md)、`MANUAL_TEST.ja.md` |

> **現在は実装前の段階です。** 利用者向けドキュメント（README / GUIDE / API）と examples はまだありません。現在地は [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) を参照してください。

## まずここから

| やりたいこと | 読む文書 |
| --- | --- |
| **何を作るライブラリで、どこまでが責務なのか知る** | **[REQUIREMENTS.ja.md](REQUIREMENTS.ja.md)** |
| **API の形と内部構造を知る** | **[CORE_DESIGN.ja.md](CORE_DESIGN.ja.md)** |
| **なぜそう設計したのかを知る** | **[DECISIONS.ja.md](DECISIONS.ja.md)** |
| 現在地と残作業、実装の順序を知る | [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) |
| ブラウザツール向けのレイアウト JSON を作る | [LAYOUT_FORMAT.ja.md](LAYOUT_FORMAT.ja.md) |
| テストの方針とケース一覧を知る | [TEST_PLAN.ja.md](TEST_PLAN.ja.md) |

## 文書一覧

**設計（全体像を掴むならこの順）**

1. [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) — 何を作るライブラリで、どこまでを責務にするか。対象環境・対象利用者・非目標・出力形式。
2. [CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) — API の形、内部構造、モノクロ化とタイル分割、BarcodeKit 連携。
3. [DECISIONS.ja.md](DECISIONS.ja.md) — 確定した設計決定の台帳。**理由と、採らなかった選択肢**を記録している。たたき台から変えた点も §3 にある。

**仕様**

- [LAYOUT_FORMAT.ja.md](LAYOUT_FORMAT.ja.md) — ブラウザツールと共有する JSON 形式。v1.0 では形式定義のみ（パーサは v1.1）。
- [WEB_TOOL.ja.md](WEB_TOOL.ja.md) — ブラウザツール。置き場所と公開方法、プレビュー忠実性。
- [FONT_LIBRARY.ja.md](FONT_LIBRARY.ja.md) — **別リポジトリで作るフォント描画ライブラリへの要求仕様。** PaperCanvas の実装対象ではないが、ブラウザツールがこれに依存する。

**プロセス**

- [TEST_PLAN.ja.md](TEST_PLAN.ja.md) — テスト方針、ディレクトリ構成、スケッチと pytest の出力プロトコル。中核は**分割数不変性**。
- [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) — 現在地、v1.0.0 のゴール、実装の順序、残りの検討事項。

## 未確定の論点

実装に入る前に決める必要があるもの。

| 論点 | 場所 |
| --- | --- |
| **フォント描画ライブラリの要求仕様と着手** | [FONT_LIBRARY.ja.md](FONT_LIBRARY.ja.md) §7。別リポジトリ。ブラウザツールはこれ待ち |
| `addRow` の API 細部 | [CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) §4.2.1 末尾。実装済みでレビュー待ち |

決着したもの: `MonoPanel` が成立するか（[DECISIONS.ja.md](DECISIONS.ja.md) D3、`tests/monopanel/` で実測確認済み）。

そのほかの保留事項は [DECISIONS.ja.md](DECISIONS.ja.md) §2 にまとまっています。
