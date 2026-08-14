"""The 1bpp byte layout is a published contract; pin it.

docs/REQUIREMENTS.ja.md §10 promises bit=1 is black, MSB first, rows on byte
boundaries at (w+7)/8, spare bits zero. A driver on the other side of the
library relies on all four, and none of them are visible from a page that
merely looks correct — so this checks the bytes directly, at widths that sit
either side of a byte boundary.
"""

import re

CHECK = re.compile(r"#CHECK name=(\S+) ok=(\d) note=(.*)")

EXPECTED = [
    "rowbytes_formula",
    "white_is_zero",
    "black_is_one",
    "msb_first",
    "padding_zero",
    "row_stride",
]

WIDTHS = [1, 7, 8, 9, 63, 64, 65, 383, 384]


def test_bitformat(dut):
    dut.expect("TEST start bitformat", timeout=30)
    dut.expect("#DONE", timeout=60)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    checks = {m.group(1): (m.group(2) == "1", m.group(3)) for m in CHECK.finditer(output)}
    missing = [n for n in EXPECTED if n not in checks]
    assert not missing, f"sketch did not report: {missing}\n{output}"

    failed = [f"{n}: {checks[n][1]}" for n in EXPECTED if not checks[n][0]]
    assert not failed, "byte layout broken:\n  " + "\n  ".join(failed) + f"\n\n{output}"

    # Every width must actually have been exercised; a loop that silently
    # skipped the awkward ones would leave the checks passing on nothing.
    widths = {int(w) for w, _, _ in re.findall(r"#WIDTH w=(\d+) rowBytes=(\d+) spare=(\d+)", output)}
    assert widths == set(WIDTHS), f"widths tested {sorted(widths)}, expected {WIDTHS}"

    for w, rb, spare in re.findall(r"#WIDTH w=(\d+) rowBytes=(\d+) spare=(\d+)", output):
        w, rb, spare = int(w), int(rb), int(spare)
        assert rb == (w + 7) // 8, f"width {w}: rowBytes {rb}"
        assert spare == w % 8, f"width {w}: spare {spare}"
