"""Labels: does the element land where the rectangle says, and stay inside it?

A label is a fixed canvas, so unlike a receipt there is no "grow to fit" escape
— an element that overflows either gets cut or ruins whatever is next to it.
The canvas exposes no clip rectangle, so PaperCanvas cuts text itself when it
stores it, and these checks are what prove that is still happening.

The other half is survivability: a mistyped rectangle must cost that element,
not the label (docs/DECISIONS.ja.md D11).
"""

import re

CHECK = re.compile(r"#CHECK name=(\S+) ok=(\d) note=(.*)")

EXPECTED = [
    "label_builds",
    "label_height_fixed",
    "label_buffer_size",
    "label_no_warnings",
    "align_left_at_box",
    "align_right_at_box",
    "align_center_between",
    "valign_top_at_box",
    "valign_ordered",
    "valign_bottom_inside",
    "text_clipped_to_box",
    "text_clip_warned",
    "text_rows_clipped",
    "text_rows_warned",
    "oob_still_builds",
    "oob_warned",
    "split_invariant",
    "stream_equals_build",
]


def test_label_layout(dut):
    dut.expect("TEST start label_layout", timeout=30)
    dut.expect("#DONE", timeout=120)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    checks = {m.group(1): (m.group(2) == "1", m.group(3)) for m in CHECK.finditer(output)}
    missing = [n for n in EXPECTED if n not in checks]
    assert not missing, f"sketch did not report: {missing}\n{output}"
    failed = [f"{n}: {checks[n][1]}" for n in EXPECTED if not checks[n][0]]
    assert not failed, "label layout broken:\n  " + "\n  ".join(failed) + f"\n\n{output}"

    aligns = re.findall(r"#ALIGN i=(\d) first=(-?\d+) last=(-?\d+)", output)
    assert len(aligns) == 3, f"expected 3 alignments, got {len(aligns)}\n{output}"
    firsts = [int(f) for _, f, _ in aligns]
    # Left / centre / right must be three distinct positions; two that coincide
    # would mean an alignment silently did nothing.
    assert len(set(firsts)) == 3, f"alignments coincide at {firsts}\n{output}"

    valigns = re.findall(r"#VALIGN i=(\d) top=(-?\d+) bottom=(-?\d+)", output)
    assert len(valigns) == 3, f"expected 3 vertical alignments, got {len(valigns)}\n{output}"
    tops = [int(t) for _, t, _ in valigns]
    assert len(set(tops)) == 3, f"vertical alignments coincide at {tops}\n{output}"

    splits = re.findall(r"#SPLIT limit=(\d+) ok=(\d)", output)
    assert len(splits) == 4, f"expected 4 split renders, got {len(splits)}\n{output}"
    assert all(ok == "1" for _, ok in splits), f"a split render failed\n{output}"

    stream = re.search(r"#STREAM ok=(\d) rows=(\d+)", output)
    assert stream and stream.group(1) == "1", f"stream() failed\n{output}"
    assert stream.group(2) == "240", f"stream emitted {stream.group(2)} rows\n{output}"
