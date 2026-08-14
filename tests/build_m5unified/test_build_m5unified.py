"""PaperCanvas.h compiles after M5Unified.

M5Unified brings its own copy of the LovyanGFX headers (as M5GFX), which is where
a header-only library that assumed one include order tends to break. Including
<M5Unified.h> first and PaperCanvas after is the order a caller actually writes.

It also builds one of each element type: a header that compiles but whose API
cannot actually be called would otherwise pass a pure compile check.
"""


def test_build_m5unified(dut):
    dut.expect("TEST start build_m5unified", timeout=30)
    dut.expect("RECEIPT ", timeout=30)
    dut.expect("LABEL ", timeout=30)
    dut.expect("STREAM ", timeout=30)
    dut.expect("RESULT ok", timeout=30)
    dut.expect("TEST done", timeout=10)
