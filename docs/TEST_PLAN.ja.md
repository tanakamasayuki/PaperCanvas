# テスト計画

内部の記録。日本語のみ。テストの走らせ方は [../tests/README.ja.md](../tests/README.ja.md)。

## 1. 方針

**自動テストはホスト実行のみ。** 実機での確認は examples を使った手動確認とし、手順は `MANUAL_TEST.ja.md`（未作成） に置く。

兄弟プロジェクト [LGFXVirtualCanvas](https://github.com/tanakamasayuki/LGFXVirtualCanvas) と [BarcodeKit](https://github.com/tanakamasayuki/BarcodeKit) の `tests/` の作りをそのまま踏襲する。

- pytest-embedded + arduino-cli バックエンド
- `lang-ship:host` コア（`mode=lgfx`、SDL2）上でヘッドレス実行
- テストごとのサブディレクトリに `<name>.ino` / `sketch.yaml` / `test_<name>.py`
- `uv run pytest` で実行。`conftest.py` が各テスト前に `output/` を消す

**このライブラリの中核テストは「分割数不変性」である。**

PaperCanvas の出力はタイル分割数に依存してはならない（[REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) §12）。この不変条件は 1 ビットの差でも検出できるうえ、レイアウト計算・座標変換・ディザ・パックのすべてを同時に通る。LGFXVirtualCanvas が `split=1` と `split=N` の PNG 比較で分割エンジンを固めたのと同じ手法を、1bpp 出力に対して適用する。

正しさの担保は 3 本立てにする。

1. **分割数不変性** — メモリ上限を変えて分割数を振り、出力バイト列が完全一致すること
2. **既知の期待画像との比較** — レイアウト計算そのものが正しいことを、固定した参照画像で押さえる
3. **バーコードの往復デコード** — 生成したビットマップを PNG 化し、zxing-cpp が読めること。実スキャナに最も近い検証

1 だけでは「一貫して間違っている」を見逃す。2 だけでは分割の壊れ方を捕まえられない。

## 2. スケッチ ↔ pytest の出力プロトコル

ホスト側スケッチは生成結果を行指向テキストで Serial に出力し、必要なら PNG を `output/` に書く。Python 側が解析して検証する。

```text
#BEGIN name=receipt_basic kind=receipt
#PAGE w=384 h=612 rowBytes=48 warn=0x0000
#SPLIT limit=19456 tiles=13 md5=3f2a...  file=output/receipt_basic_l19456.png
#SPLIT limit=4096  tiles=61 md5=3f2a...  file=output/receipt_basic_l4096.png
#ELEM i=0 type=text  y=0   h=18 w=384 warn=0x0000
#ELEM i=1 type=image y=18  h=64 w=200 warn=0x0004
#CHECK name=build_equals_stream ok=1 note=byte-identical
#END
#DONE
```

| 行 | 内容 |
| --- | --- |
| `#BEGIN` | ケース名、種別（`receipt` / `label`） |
| `#PAGE` | 確定したページサイズ、`rowBytes`、警告ビットフラグ |
| `#SPLIT` | メモリ上限を変えて生成した結果。タイル数と出力の md5、書き出した PNG のパス。**md5 が全行で一致することが分割数不変性の合格条件** |
| `#ELEM` | 要素ごとの確定位置・サイズ・警告。レイアウト計算の検証用 |
| `#CHECK` | スケッチ自身が行った真偽判定。`name=<名前> ok=<0\|1> note=<説明>`。ビットマップからは観測できないことに使う |
| `#END` | ケース終端 |
| `#DONE` | スケッチ全体の終端。これが無ければ途中で落ちたと判断する |

**テスト用の特別な API をライブラリ本体に足さない**ことを条件とする（公開 API だけで出力できる）。共通のレポートヘルパーは `tests/common_libs/pc_report/` に置き、リリースには含めない。

1bpp ビットマップの PNG 化は Python 側（Pillow の `mode="1"`）で行う。スケッチ側は 1bpp バイト列を base64 か hex で吐くだけにする。

## 3. テストディレクトリ

### Tier 1 — 出力の正しさ

| ディレクトリ | 内容 |
| --- | --- |
| `split_invariance/` | **中核テスト。** レシート・ラベルの代表ケースを、メモリ上限を変えて 5 通り生成し、出力バイト列が完全一致すること。不一致時は各 PNG と差分 PNG を artifacts に残す |
| `build_stream/` | `build()`（全ページ）と `stream()`（帯）が同一バイト列を生成すること。帯の連結が正しいこと、最終帯の端数行が正しいこと |
| `bitformat/` | `rowBytes` の正しさ、bit=1 が黒であること、MSB first であること、行末余りビットが 0 であること。幅 1 / 7 / 8 / 9 / 383 / 384 px の境界 |
| `receipt_layout/` | 縦積みの正しさ。要素の高さ、行間、余白、寄せ方、`height()` の値、`addSpace` / `addLine` / `addRule` |
| `row/` | `addRow`。**列が行をまたいで縦に揃うこと**が中心（セルの文字長を大きく変えても、描かれた列の位置が動かないことを実際のインクの左右端で確認する）。暗黙レイアウトと明示グリッドの両方、リーダー文字、セル内折り返しで行が高くなること、固定幅列が溢れても押し広がらずクリップされること、空セルが幅 0 であること、分割数不変性 |
| `label_layout/` | 矩形配置の正しさ。`Align` / `VAlign` / `Fit` の全組み合わせ、矩形外へのクリップ、キャンバス外配置での `OutOfBounds` |
| `text/` | フォント・サイズ・明示改行・折り返し。折り返し位置が幅に対して正しいこと、レシート既定 ON / ラベル既定 OFF、`TextClipped` が立つ条件 |
| `image/` | スケール・縦横比維持・クリップ。`ImageScaled` / `ImageClipped` が立つ条件 |
| `dither/` | 閾値と Bayer の出力が固定した期待画像と一致すること。**グレー勾配画像を分割数を変えて処理し完全一致すること**（順序ディザがタイル境界に依存しないことの証明） |
| `determinism/` | 同じ入力を 2 つのオブジェクトで生成して一致すること、10 回繰り返しても変わらないこと、`clear()` 後に前回の内容が残らないこと |
| `warnings/` | 各 `Warning` が意図した条件で立ち、意図しない条件では立たないこと。警告が出ても生成が続行されること |
| `failure/` | バッファ不足・サイズ 0・確保失敗で `false` を返すこと。**フォールバックせずに失敗すること**。失敗時に出力バッファが書き換わらないこと |

### Tier 2 — 連携

| ディレクトリ | 内容 |
| --- | --- |
| `barcode/` | `PaperCanvasBarcode.h` 経由。倍率が整数倍であること、クワイエットゾーンが確保されていること、ガードバーが延長されていること、収まらない場合に**描かずに** `BarcodeTooSmall` が立つこと |
| `barcode_decode/` | 生成した 1bpp ページを PNG 化し、zxing-cpp でデコードして入力文字列に戻ること。形式の誤認も検出する。**実スキャナに最も近い検証** |
| `build_lovyangfx/` | LovyanGFX 単体環境でのビルド確認 |
| `build_m5unified/` | M5Unified 環境でのビルド確認 |

### Tier 0 — スパイク（実装の前提確認）

| ディレクトリ | 内容 |
| --- | --- |
| `monopanel/` | **最初に書く。** `grayscale_8bit` のタイル sprite を `MonoPanel` へ `pushSprite` したとき、`pixelcopy_t::fp_copy` が期待どおりグレー値を供給するか。既知のグレーパターンを流し込み、二値化前のグレー行バッファが一致することを確認する。ここが崩れたら [DECISIONS.ja.md](DECISIONS.ja.md) D3 の退路へ切り替える |

`monopanel/` は**フック自体が効いているかを最初に確認する**。`fp_copy` が呼ばれていなければ行バッファはゼロのままで、以降のすべてが誤った理由で成功してしまう。意図的に非ゼロのグレー値を流し、それが観測できることを確かめてから本題に入る。

## 4. 分割数不変性テストの具体的な作り

```cpp
// split_invariance/split_invariance.ino （骨子）
static const size_t LIMITS[] = { 0, 64*1024, 19*1024, 8*1024, 4*1024 };

for (size_t i = 0; i < 5; ++i) {
  r.clear();
  buildReceipt(r);                 // 毎回同じ内容を積む
  r.setMemoryLimit(LIMITS[i]);
  r.build(page, sizeof(page));
  pc_report::emitSplit(LIMITS[i], r, page);   // md5 と hex を吐く
}
```

Python 側は `#SPLIT` 行の md5 が全行一致することを検証し、不一致なら各出力を PNG 化して差分画像を `output/` に残す。差分の形（横帯状なら分割ロジック、格子状ならディザ、全面なら座標変換）から原因を切り分けられる。

**`limit=0`（LGFXVirtualCanvas 既定）を必ず含める。** 実利用で最も使われる設定が参照系から外れないようにするため。

## 5. 実機での手動確認

自動化しないもの。`MANUAL_TEST.ja.md`（未作成） に手順と記録を置く。

- 実際の感熱プリンターで印字し、**バーコードが実スキャナで読めるか**
- ディザをかけた写真が実用的な見た目になるか
- 203dpi / 300dpi で推奨フォントサイズが読めるか（GUIDE の記述の裏付け）
- ラベルプリンターで矩形位置が狙いどおりか

## 6. CI

`.github/workflows/tests.yml` で `uv run pytest` を回す。BarcodeKit / LGFXVirtualCanvas のワークフローをそのまま流用する。

BarcodeKit はリリース前なので、`sketch.yaml` では `dir: ../../BarcodeKit` で参照する。**BarcodeKit のリリース後にバージョン指定へ切り替える。**
