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

**Tier 1 — 出力の正しさ**

- `bitformat/` — 出力バイト並びの固定。`rowBytes = (w+7)/8`、bit=1 が黒、MSB first、行末余りビットが 0、ページ外を書かないこと。幅 1 / 7 / 8 / 9 / 63 / 64 / 65 / 383 / 384 の境界で確認する
- `dither/` — 閾値と Bayer。**0..255 の階調ランプをメモリ上限 5 通りで生成してバイト一致すること**（順序ディザがタイル境界に依存しないことの証明）、閾値の境界（`gray < threshold` が黒）、Bayer が平坦な閾値でないこと、4 行周期であること
- `receipt_layout/` — 縦積み。**`height()` が「余白 + 各 add() の戻り値の総和」と一致すること**（この 2 つは別経路で同じ数に到達するので、ずれるとページが黙って切れるか余る）、バッファ不足の拒否、分割数不変性、`build()` と `stream()` の一致、決定性、`clear()`、設定が以後の要素にのみ効くこと

**Tier 2 — 連携**

- `barcode/` — バーコード配置。倍率が整数倍であること、クワイエットゾーンが空いていること、ガードバーが伸びていること、収まらないなら描かないこと。**同じテストが生成ページを zxing-cpp でデコードして入力に戻ることまで確認する**
- `js_parity/` — **ブラウザツールのプレビューが実際の印字と一致するか。** 同じテキストを PaperCanvas（C++）と [lgfx-font-tool](https://www.npmjs.com/package/lgfx-font-tool)（JS）で描き、1bpp ページがバイト単位で一致すること。**Node が必要な唯一のテスト**
- `build_lovyangfx/` / `build_m5unified/` — include 順とライブラリ組み合わせ

## js_parity だけ Node が要る

```sh
cd js_parity && npm install
```

入っていなければテストは **fail ではなく skip** する。Node の無い環境でも残りを走らせられるようにするためだが、**CI では必ず入れる**（skip したままだとツールがライブラリから離れていっても気づけない）。

ケースは `cases.json` が単一の出所で、C++ 側は生成された `cases.h` 経由で読む。`cases.json` を編集したら:

```sh
python3 js_parity/gen_cases.py
```

忘れてもテストが鮮度を見て落とす。

## 出力物を見る

`dither/output/*.pbm` と `receipt_layout/output/receipt.pbm` は 1bpp の PBM（P4）で、**PaperCanvas の 1bpp と PBM はどちらも 1 = 黒**なのでページのバイト列がそのまま入っている。画像ビューアで開けるほか、次のようにも読める。

```sh
python3 -c "
d=open('output/receipt.pbm','rb').read()
i=d.index(b'\n',d.index(b'\n')+1)+1
w,h=[int(x) for x in d[:i].decode().strip().split('\n')[1].split()]
body=d[i:]; rb=(w+7)//8
for y in range(0,h,4):
    print(''.join('#' if (body[y*rb+(x>>3)]>>(7-(x&7)))&1 else '.' for x in range(0,w,3)))
"
```

## スケッチを書くときの注意

- **`printf` ではなく `Serial.printf` を使う。** 素の `printf` は stdout へ出るが、pytest が読むのは TCP 経由の Serial であり、**出力が一切届かないまま**テストがタイムアウトする。スケッチがクラッシュしたのか出力先を間違えたのかが見分けにくいので、最初から `Serial.printf` で書く。
- **クラッシュすると出力ごと消える。** どこまで進んだかを知りたいときは、`Serial.printf` のマーカーを挟んで二分する。ホストコアのログ（`build/host/<name>.ino.out.host-arduino.log`）に `lgfx_thunk_enter` はあるが `lgfx_setup_returned` が無ければ、`setup()` の中で落ちている。
- **`.ino` にテンプレート関数を書かない。** Arduino のプリプロセッサが生成するプロトタイプがテンプレートと関数の間に挿入され、コンパイルが通らなくなる。
- **描画色は `lgfx::color888(v, v, v)` で渡す。** `lgfx::grayscale_t` を直接渡すと `color_conv_t::convert` の 1 バイト型オーバーロードに落ちてコンパイルエラーになる。`uint32_t` なら `convert_rgb888` を通り、`grayscale_t` の変換係数が `(r + 2g + b) / 4` なので等値 3 チャンネルはそのままの値でタイルに届く。
- **テスト専用の API をライブラリ本体に足さない。** 観測は公開 API か、テスト側の派生クラス（`monopanel/` の `ProbePanel` のように）で行う。
