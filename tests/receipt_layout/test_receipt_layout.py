"""Receipt stacking, and the two things that must agree about it.

A receipt resolves each element's height as it is added, so `height()` has to
equal the margins plus every `add()`'s reported height. Those are two separate
code paths reaching the same number, and if they drift the page is silently
clipped or padded.

The split-invariance check is repeated here rather than left to `monopanel/`
because layout is where it is most likely to break: an element positioned
relative to anything tile-local passes every lower-level test and fails this.
"""

import re

CHECK = re.compile(r"#CHECK name=(\S+) ok=(\d) note=(.*)")

EXPECTED = [
    "empty_height_zero",
    "height_matches_adds",
    "height_nonzero",
    "buffer_size",
    "wrap_warned",
    "short_buffer_refused",
    "short_buffer_untouched",
    "split_invariant",
    "stream_equals_build",
    "deterministic",
    "clear_resets",
    "reuse_after_clear",
    "setting_applies_forwards",
]


def test_receipt_layout(dut):
    dut.expect("TEST start receipt_layout", timeout=30)
    dut.expect("#DONE", timeout=120)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    checks = {m.group(1): (m.group(2) == "1", m.group(3)) for m in CHECK.finditer(output)}
    missing = [n for n in EXPECTED if n not in checks]
    assert not missing, f"sketch did not report: {missing}\n{output}"
    failed = [f"{n}: {checks[n][1]}" for n in EXPECTED if not checks[n][0]]
    assert not failed, "receipt layout broken:\n  " + "\n  ".join(failed) + f"\n\n{output}"

    h = re.search(r"#HEIGHT height=(\d+) sumAdds=(\d+) expect=(\d+) count=(\d+) warn=0x([0-9a-f]+)",
                  output)
    assert h, f"no height report\n{output}"
    height, sum_adds, expect, count = (int(h.group(i)) for i in (1, 2, 3, 4))
    assert height == expect == sum_adds + 16, f"height {height} vs adds {sum_adds}\n{output}"
    # compose() adds nine elements. A dropped add returns 0 and would keep the
    # height arithmetic self-consistent, so the count is checked separately.
    assert count == 9, f"expected 9 elements, stored {count}\n{output}"

    # Scaling the image 2x and wrapping the closing line are both expected, and
    # nothing else should have been flagged.
    warn = int(h.group(5), 16)
    assert warn == 0x0006, f"warnings 0x{warn:04x}, expected ImageScaled|TextWrapped\n{output}"

    splits = re.findall(r"#SPLIT limit=(\d+) ok=(\d)", output)
    assert len(splits) == 5, f"expected 5 split renders, got {len(splits)}\n{output}"
    assert all(ok == "1" for _, ok in splits), f"a split render failed\n{output}"

    stream = re.search(r"#STREAM ok=(\d) rows=(\d+) pageH=(\d+)", output)
    assert stream and stream.group(1) == "1", f"stream() failed\n{output}"
    assert stream.group(2) == stream.group(3), (
        f"stream emitted {stream.group(2)} rows for a {stream.group(3)}px page\n{output}"
    )
