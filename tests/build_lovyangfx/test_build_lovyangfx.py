"""PaperCanvas.h stands on its own with LovyanGFX.

Every other sketch includes <LovyanGFX.hpp> before PaperCanvas, which is not how
a caller reaches for a header. This one includes PaperCanvas alone, so a missing
include inside the library shows up here rather than in someone's project.

It also builds one of each element type: a header that compiles but whose API
cannot actually be called would otherwise pass a pure compile check.
"""


def test_build_lovyangfx(dut):
    dut.expect("TEST start build_lovyangfx", timeout=30)
    dut.expect("RECEIPT ", timeout=30)
    dut.expect("LABEL ", timeout=30)
    dut.expect("STREAM ", timeout=30)
    dut.expect("RESULT ok", timeout=30)
    dut.expect("TEST done", timeout=10)
