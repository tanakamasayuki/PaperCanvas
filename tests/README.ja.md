# Tests

> English: [README.md](README.md)

PaperCanvas の自動テストスイート。

- [pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/en/latest/) + Arduino CLI バックエンド
- `lang-ship:host` コア（`mode=lgfx`、SDL2）上でヘッドレス実行する。**実機は使わない**
- テストごとのサブディレクトリに `<name>.ino` / `sketch.yaml` / `test_<name>.py`（`dut` フィクスチャを使用）
- 成果物を出すスケッチは `output/<name>.png` を書く。`conftest.py` が各テスト前に `output/` を消す

テスト方針・ケース一覧は [../docs/TEST_PLAN.ja.md](../docs/TEST_PLAN.ja.md) を参照。

## 実行

```sh
# 全テスト
uv run pytest -v

# 単一テスト
uv run pytest monopanel -v
```

初回実行では arduino-cli 環境へコアとライブラリをダウンロードするため、2回目以降より時間がかかる。

## ディレクトリ構成

**Tier 0 — 設計の前提**

- `monopanel/` — **最初に通すべきテスト。** `MonoPanel`（`lgfx::Panel_Device` 派生）が `LGFXVirtualScreen` のシンクとして成立するかを確認する。タイルが `grayscale_8bit` で届くこと、`fp_copy` が描いたグレー値をそのまま供給すること、タイルがページ全域を覆うこと、メモリ上限を変えても出力が一致すること、帯出力が全ページ出力と一致すること。

  ここが落ちたら設計が変わる（[../docs/DECISIONS.ja.md](../docs/DECISIONS.ja.md) D3）。`MonoPanel` は LovyanGFX の内部挙動に依存しているので、**LovyanGFX / LGFXVirtualCanvas を上げたときに最初に壊れるのはここ**。

残りのテストは未実装。計画は [../docs/TEST_PLAN.ja.md](../docs/TEST_PLAN.ja.md) §3。

## スケッチを書くときの注意

- **`printf` ではなく `Serial.printf` を使う。** 素の `printf` は stdout へ出るが、pytest が読むのは TCP 経由の Serial であり、**出力が一切届かないまま**テストがタイムアウトする。スケッチがクラッシュしたのか出力先を間違えたのかが見分けにくいので、最初から `Serial.printf` で書く。
- **クラッシュすると出力ごと消える。** どこまで進んだかを知りたいときは、`Serial.printf` のマーカーを挟んで二分する。ホストコアのログ（`build/host/<name>.ino.out.host-arduino.log`）に `lgfx_thunk_enter` はあるが `lgfx_setup_returned` が無ければ、`setup()` の中で落ちている。
- **`.ino` にテンプレート関数を書かない。** Arduino のプリプロセッサが生成するプロトタイプがテンプレートと関数の間に挿入され、コンパイルが通らなくなる。
- **描画色は `lgfx::color888(v, v, v)` で渡す。** `lgfx::grayscale_t` を直接渡すと `color_conv_t::convert` の 1 バイト型オーバーロードに落ちてコンパイルエラーになる。`uint32_t` なら `convert_rgb888` を通り、`grayscale_t` の変換係数が `(r + 2g + b) / 4` なので等値 3 チャンネルはそのままの値でタイルに届く。
- **テスト専用の API をライブラリ本体に足さない。** 観測は公開 API か、テスト側の派生クラス（`monopanel/` の `ProbePanel` のように）で行う。
