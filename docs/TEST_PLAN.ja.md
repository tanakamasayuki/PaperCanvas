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

**分割数不変性・`build()` と `stream()` の一致・決定性は、専用ディレクトリを作らず各テストの中で確認する。** 当初は `split_invariance/` `build_stream/` `determinism/` を独立させる計画だったが、そうすると「代表ケースだけが不変」という保証になる。機能ごとのテストが自分の機能について不変性を確かめるほうが、網羅が広く、壊れたときに原因の場所も分かる。

実際に `receipt_layout/` `row/` `label_layout/` `image/` `dither/` `barcode/` `monopanel/` がそれぞれメモリ上限を振って比較しており、`receipt_layout/` と `label_layout/` は `stream()` との一致と決定性も見ている。

| ディレクトリ | 内容 |
| --- | --- |
| `bitformat/` | `rowBytes` の正しさ、bit=1 が黒であること、MSB first であること、行末余りビットが 0 であること。幅 1 / 7 / 8 / 9 / 383 / 384 px の境界 |
| `receipt_layout/` | 縦積みの正しさ。**`height()` が「余白 + 各 add() の戻り値の総和」と一致すること**（別経路で同じ数に到達するので、ずれるとページが黙って切れるか余る）、バッファ不足の拒否、要素の高さ・行間・余白・寄せ方、`addSpace` / `addLine` / `addRule`、**分割数不変性・`build()` と `stream()` の一致・決定性・`clear()`・設定が以後の要素にのみ効くこと** |
| `row/` | `addRow`。**列が行をまたいで縦に揃うこと**が中心（セルの文字長を大きく変えても、描かれた列の位置が動かないことを実際のインクの左右端で確認する）。暗黙レイアウトと明示グリッドの両方、リーダー文字、セル内折り返しで行が高くなること、固定幅列が溢れても押し広がらずクリップされること、空セルが幅 0 であること、分割数不変性 |
| `label_layout/` | 矩形配置の正しさ。`Align` / `VAlign` を**描かれたインクの位置から**確認、矩形外への横・縦クリップ、キャンバス外配置での `OutOfBounds` とそれでも生成されること、分割数不変性、`build()` と `stream()` の一致 |
| `text/` | フォント・サイズ・行間・明示改行・UTF-8。**折り返し位置が両方向から正しいこと** — どの行も幅を超えないこと、かつ**必要より早く折り返していないこと**（1 文字早く折る実装は「収まるか」の検査を通り抜けながら紙を 3 割無駄にする）。折り返し OFF ではクリップされ 1 行に留まること |
| `image/` | `Fit` 5 種を**縦横比の合わない**ソース（4:1）とボックス（1:1）で確認する（正方形同士だと Contain / Cover / Stretch が同じ矩形になり素通りする）。gray8 と 1bpp のソースが同じ絵を出すこと、反転、`ImageScaled` は**縮小時のみ**立つこと、分割数不変性 |
| `dither/` | 閾値と Bayer の出力が固定した期待画像と一致すること。**グレー勾配画像を分割数を変えて処理し完全一致すること**（順序ディザがタイル境界に依存しないことの証明） |
| `warnings/` | 各 `Warning` が意図した条件で立ち、意図しない条件では立たないこと。警告が出ても生成が続行されること |
| `failure/` | バッファ不足・null・サイズ 0・要素ゼロで `false` を返すこと。**ちょうど `bufferSize()` なら成功すること**（境界の反対側）。**フォールバックしないこと** — 小さいページや途中まで出したページを作らない。失敗しても文書の状態が壊れず、バッファを直せば再実行できること。**失敗する呼び出しの前にバッファを毒で埋め、「何も書いていない」を検査する** |

### Tier 2 — 連携

| ディレクトリ | 内容 |
| --- | --- |
| `barcode/` | 配置。倍率が整数倍であること（**倍率 2 以上の短いシンボルで確認する。倍率 1 では「実行長が倍率の倍数」は自明に成立して意味がない**）、クワイエットゾーンにインクが無いこと、ガードバーがデータバーより下へ伸びていること、収まらない場合に**描かずに** `BarcodeTooSmall` が立つこと、分割数不変性。**同じテストが生成ページを PBM で書き出し、Python 側が zxing-cpp でデコードして入力文字列と形式に戻ることまで確認する**（実スキャナに最も近い検証。幾何が正しくてもシンボルが誤っていることはあり、デコーダが寛容で通ることもあるので、両方を見る） |
| `build_lovyangfx/` | **`<PaperCanvas.h>` を単独で include する。** 他のテストはすべて `<LovyanGFX.hpp>` を先に書いており、それは利用者のヘッダの触り方ではない。ライブラリ内の include 漏れがここで出る。各要素型を 1 つずつ実行もする（コンパイルは通るが呼べない API を素通りさせないため） |
| `build_m5unified/` | **`<M5Unified.h>` を先に include する。** M5Unified は LovyanGFX のヘッダを M5GFX として自前で持ち込むので、include 順を仮定したヘッダオンリーライブラリはここで壊れる |

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
