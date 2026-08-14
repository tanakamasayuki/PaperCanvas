"""Wrapping decides once, so it has to decide correctly.

Breaks are resolved when the text is added and stored with the newlines baked
in, which is what keeps the tiled render from re-deciding them per tile. The
cost is that a wrong decision is permanent, so both directions are checked: no
produced line may exceed the width, and no line may break earlier than it had
to. A wrapper that broke one character early would satisfy "does it fit" while
wasting a third of the paper.
"""

import re

CHECK = re.compile(r"#CHECK name=(\S+) ok=(\d) note=(.*)")

EXPECTED = [
    "newlines_stack",
    "newlines_drawn",
    "size_scales_height",
    "line_spacing_adds",
    "wrap_lines_fit",
    "wrap_uses_width",
    "wrap_multiple_lines",
    "wrap_warned",
    "wrap_keeps_newlines",
    "nowrap_single_line",
    "nowrap_within_width",
    "nowrap_warned",
    "utf8_wraps",
    "utf8_lines_fit",
    "empty_text_one_line",
]


def test_text(dut):
    dut.expect("TEST start text", timeout=30)
    dut.expect("#DONE", timeout=120)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    checks = {m.group(1): (m.group(2) == "1", m.group(3)) for m in CHECK.finditer(output)}
    missing = [n for n in EXPECTED if n not in checks]
    assert not missing, f"sketch did not report: {missing}\n{output}"
    failed = [f"{n}: {checks[n][1]}" for n in EXPECTED if not checks[n][0]]
    assert not failed, "text handling broken:\n  " + "\n  ".join(failed) + f"\n\n{output}"

    wrap = re.search(r"#WRAP h=(\d+) lines=(\d+) widest=(-?\d+) pageW=(\d+)", output)
    assert wrap, f"no wrap report\n{output}"
    lines, widest, page_w = (int(wrap.group(i)) for i in (2, 3, 4))
    assert widest < page_w, f"a wrapped line reached {widest} on a {page_w}px page\n{output}"
    # The sample is long enough that a correct wrapper needs several lines; far
    # more than that would mean it is breaking much earlier than necessary.
    assert 3 <= lines <= 8, f"wrapped into {lines} lines, expected 3-8\n{output}"

    sizes = re.search(r"#SIZE h1=(\d+) h2=(\d+) h3=(\d+)", output)
    assert sizes, f"no size report\n{output}"
    h1, h2, h3 = (int(sizes.group(i)) for i in (1, 2, 3))
    assert h1 > 0 and h2 == h1 * 2 and h3 == h1 * 3, f"sizes {h1}/{h2}/{h3}\n{output}"
