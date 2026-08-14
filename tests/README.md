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

The remaining tests are not written yet; the plan is in [../docs/TEST_PLAN.ja.md](../docs/TEST_PLAN.ja.md) §3.

## Writing sketches

- **Use `Serial.printf`, not `printf`.** Plain `printf` goes to stdout, but pytest reads Serial over TCP, so **nothing arrives at all** and the test times out. That failure looks the same as a crash, so write `Serial.printf` from the start.
- **A crash takes the output with it.** To find how far a sketch got, bisect with `Serial.printf` markers. In the host core log (`build/host/<name>.ino.out.host-arduino.log`), `lgfx_thunk_enter` without a following `lgfx_setup_returned` means it died inside `setup()`.
- **No template functions in `.ino` files.** The Arduino preprocessor inserts generated prototypes between the template and the function, which will not compile.
- **Pass colours as `lgfx::color888(v, v, v)`.** Passing `lgfx::grayscale_t` directly resolves to the one-byte overload of `color_conv_t::convert` and fails to compile. A `uint32_t` goes through `convert_rgb888`, and since `grayscale_t`'s conversion is `(r + 2g + b) / 4`, an equal-channel value reaches the tile unchanged.
- **Do not add test-only API to the library.** Observe through the public API, or through a test-side subclass (as `monopanel/` does with `ProbePanel`).
