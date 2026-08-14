"""Columns must stay put across rows.

That is the entire reason `addRow` exists instead of leaving callers to pad a
string with spaces (docs/DECISIONS.ja.md D13): column boxes come from the
layout, not from the length of this row's text. So the central check feeds rows
whose cell lengths differ wildly and requires the drawn columns to land in the
same place.
"""

import re

CHECK = re.compile(r"#CHECK name=(\S+) ok=(\d) note=(.*)")
ROW = re.compile(r"#ROW i=(\d+) first=(-?\d+) last=(-?\d+)")

EXPECTED = [
    "right_column_aligned",
    "right_column_at_edge",
    "grid_first_column_at_zero",
    "leader_adds_ink",
    "row_wrap_grows",
    "row_wrap_warned",
    "row_clip_same_height",
    "row_clip_keeps_box",
    "row_clip_warned",
    "split_invariant",
    "empty_cell_no_width",
]


def test_row(dut):
    dut.expect("TEST start row", timeout=30)
    dut.expect("#DONE", timeout=120)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    checks = {m.group(1): (m.group(2) == "1", m.group(3)) for m in CHECK.finditer(output)}
    missing = [n for n in EXPECTED if n not in checks]
    assert not missing, f"sketch did not report: {missing}\n{output}"
    failed = [f"{n}: {checks[n][1]}" for n in EXPECTED if not checks[n][0]]
    assert not failed, "row layout broken:\n  " + "\n  ".join(failed) + f"\n\n{output}"

    rows = ROW.findall(output)
    assert len(rows) == 3, f"expected 3 measured rows, got {len(rows)}\n{output}"
    firsts = [int(f) for _, f, _ in rows]
    lasts = [int(l) for _, _, l in rows]
    # Names of very different lengths: the left ink must start at the same place
    # and the right ink must end at the same place. If either drifted with the
    # text length, the layout is coming from the content and not the columns.
    assert len(set(lasts)) == 1, f"right column ends at {lasts}\n{output}"
    assert len(set(firsts)) == 1, f"left column starts at {firsts}\n{output}"

    splits = re.findall(r"#SPLIT limit=(\d+) ok=(\d)", output)
    assert len(splits) == 4, f"expected 4 split renders, got {len(splits)}\n{output}"
    assert all(ok == "1" for _, ok in splits), f"a split render failed\n{output}"

    wrap = re.search(r"#WRAP one=(\d+) wrapped=(\d+)", output)
    assert wrap, f"no wrap report\n{output}"
    one, wrapped = int(wrap.group(1)), int(wrap.group(2))
    # Two lines, so roughly double — a wrapped row that grew by a pixel would
    # mean the height came from somewhere other than the line count.
    assert wrapped >= one * 2, f"wrapped row {wrapped}px vs single {one}px\n{output}"
