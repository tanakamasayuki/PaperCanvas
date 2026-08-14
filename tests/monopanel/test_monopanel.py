"""Phase 0 spike: does the MonoPanel design hold up against real LovyanGFX?

Everything else in PaperCanvas is built on the assumption checked here, so this
test is the one that decides whether the design stands (docs/DECISIONS.ja.md D3).
The sketch reports each assumption as a `#CHECK` line; a failure here means the
design changes, not that a detail needs fixing.
"""

import re

CHECK = re.compile(r"#CHECK name=(\S+) ok=(\d) note=(.*)")

# Ordered by dependency: a dead hook makes every later result meaningless.
EXPECTED = [
    "render_ok",
    "hook_writeImage_called",
    "hook_gray_nonzero",
    "depth_is_grayscale8",
    "tiles_cover_page",
    "gray_ramp_exact",
    "split_invariant",
    "bands_equal_page",
]


def test_monopanel(dut):
    dut.expect("TEST start monopanel", timeout=30)
    dut.expect("#DONE", timeout=60)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    checks = {m.group(1): (m.group(2) == "1", m.group(3)) for m in CHECK.finditer(output)}

    missing = [name for name in EXPECTED if name not in checks]
    assert not missing, f"sketch did not report: {missing}\n{output}"

    failed = [f"{name}: {checks[name][1]}" for name in EXPECTED if not checks[name][0]]
    assert not failed, "spike assumptions broken:\n  " + "\n  ".join(failed) + f"\n\n{output}"

    # The split lines carry the evidence for split_invariant; keep them visible
    # in the report so a regression can be read without rerunning.
    splits = re.findall(r"#SPLIT limit=(\d+) tiles=(\d+) ok=(\d) row0=(\S+)", output)
    assert len(splits) == 5, f"expected 5 split renders, got {len(splits)}\n{output}"
    assert all(ok == "1" for _, _, ok, _ in splits), f"a split render failed\n{output}"
    rows = {row for _, _, _, row in splits}
    assert len(rows) == 1, f"row 0 differs across split counts: {rows}"
    # A page split into one tile at every limit would make the test vacuous.
    tile_counts = {int(t) for _, t, _, _ in splits}
    assert max(tile_counts) > 1, f"no limit produced more than one tile: {tile_counts}"
