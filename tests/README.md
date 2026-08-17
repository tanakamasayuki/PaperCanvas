# Tests

> 日本語: [README.ja.md](README.ja.md)

The PaperCanvas test suite.

- [pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/en/latest/) with the Arduino CLI backend
- Runs headless on the `lang-ship:host` core (`mode=lgfx`, SDL2). **No hardware.**
- One subdirectory per test holding `<name>.ino` / `sketch.yaml` / `test_<name>.py` (using the `dut` fixture)
- Sketches that produce artifacts write `output/<name>.png`; `conftest.py` wipes `output/` before each test

The test strategy and case list are in [../docs/TEST_PLAN.ja.md](../docs/TEST_PLAN.ja.md) (Japanese).

## Running

```sh
# everything
uv run pytest -v

# one test
uv run pytest monopanel -v
```

The first run downloads the core and libraries into the arduino-cli environment, so it takes longer than later runs.

## Layout

**Tier 0 — design premise**

- `monopanel/` — **the test to get passing first.** Checks that `MonoPanel` (a `lgfx::Panel_Device` subclass) works as a sink for `LGFXVirtualScreen`: that tiles arrive as `grayscale_8bit`, that `fp_copy` hands over the gray values that were drawn, that tiles cover the page exactly, that changing the memory limit does not change a single output byte, and that band output matches the full page.

  If this fails, the design changes ([../docs/DECISIONS.ja.md](../docs/DECISIONS.ja.md) D3). `MonoPanel` depends on LovyanGFX internals, so **this is the first thing that breaks when LovyanGFX or LGFXVirtualCanvas is upgraded.**

**Tier 1 — output correctness**

- `bitformat/` — pins the output byte layout: `rowBytes = (w+7)/8`, bit=1 is black, MSB first, spare bits at the end of a row are 0, and nothing is written past the page. Checked at widths 1 / 7 / 8 / 9 / 63 / 64 / 65 / 383 / 384.
- `dither/` — threshold and Bayer. **A 0..255 gray ramp rendered at five memory limits must produce identical bytes**, which is the proof that ordered dithering does not depend on tile boundaries. Also the threshold boundary (`gray < threshold` is black), that Bayer is not a flat threshold, and that Bayer 4x4 repeats every 4 rows.
- `receipt_layout/` — stacking. **`height()` must equal the margins plus every `add()`'s reported height** (two separate paths reaching the same number; drift means a silently clipped or padded page), short-buffer refusal, split invariance, `build()` == `stream()`, determinism, `clear()`, and that a setting change affects only later elements.

**Tier 2 — integration**

- `barcode/` — barcode placement: whole-number scale, a blank quiet zone, extended guard bars, and nothing drawn when it will not fit. **The same test decodes the generated page with zxing-cpp and requires the input back.**
- `js_parity/` — **does the browser tool's preview match what the device prints?** The same text is rendered by PaperCanvas (C++) and by [lgfx-font-tool](https://www.npmjs.com/package/lgfx-font-tool) (JS), and the 1bpp pages must be byte-identical. **The only test that needs Node.**
- `build_lovyangfx/` / `build_m5unified/` — include order and library combinations

## Only js_parity needs Node

```sh
cd js_parity && npm install
```

Without it the test **skips rather than fails**, so the rest of the suite still runs on a machine without Node — but **CI always installs it**, because a permanently skipped test would let the tool drift away from the library unnoticed.

`cases.json` is the single source; the C++ side reads it through the generated `cases.h`. After editing it:

```sh
python3 js_parity/gen_cases.py
```

Forgetting is caught: the test checks the header is current.

## Looking at the output

`dither/output/*.pbm` and `receipt_layout/output/receipt.pbm` are 1bpp PBM (P4). **PaperCanvas's 1bpp and PBM both use 1 = black**, so the page bytes go in verbatim. Any image viewer opens them, or:

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

## Writing sketches

- **Use `Serial.printf`, not `printf`.** Plain `printf` goes to stdout, but pytest reads Serial over TCP, so **nothing arrives at all** and the test times out. That failure looks the same as a crash, so write `Serial.printf` from the start.
- **A crash takes the output with it.** To find how far a sketch got, bisect with `Serial.printf` markers. In the host core log (`build/host/<name>.ino.out.host-arduino.log`), `lgfx_thunk_enter` without a following `lgfx_setup_returned` means it died inside `setup()`.
- **No template functions in `.ino` files.** The Arduino preprocessor inserts generated prototypes between the template and the function, which will not compile.
- **Pass colours as `lgfx::color888(v, v, v)`.** Passing `lgfx::grayscale_t` directly resolves to the one-byte overload of `color_conv_t::convert` and fails to compile. A `uint32_t` goes through `convert_rgb888`, and since `grayscale_t`'s conversion is `(r + 2g + b) / 4`, an equal-channel value reaches the tile unchanged.
- **Do not add test-only API to the library.** Observe through the public API, or through a test-side subclass (as `monopanel/` does with `ProbePanel`).
