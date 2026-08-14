"""Ordered dithering must not depend on how the page was tiled.

That property is the entire argument for choosing Bayer over error diffusion
(docs/DECISIONS.ja.md D4), so it is checked rather than assumed: a gray ramp
covering every level 0..255 is rendered at five memory limits and the output
bytes must be identical.

Invariance alone would also be satisfied by a dither that collapsed to a solid
page, so ink coverage and the shape of the pattern are checked too.
"""

import re

CHECK = re.compile(r"#CHECK name=(\S+) ok=(\d) note=(.*)")
SPLIT = re.compile(r"#SPLIT case=(\S+) limit=(\d+) tiles=(\d+) ok=(\d) match=(\d)")
COVER = re.compile(r"#COVER case=(\S+) black=(\d+) total=(\d+) pct=(\d+) refTiles=(\d+)")

EXPECTED = [
    "split_invariant_all_methods",
    "threshold_boundary",
    "bayer4_varies",
    "bayer4_period",
    "methods_differ",
]

CASES = {"threshold128", "threshold64", "bayer4x4", "bayer8x8"}


def test_dither(dut):
    dut.expect("TEST start dither", timeout=30)
    dut.expect("#DONE", timeout=90)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    checks = {m.group(1): (m.group(2) == "1", m.group(3)) for m in CHECK.finditer(output)}
    missing = [n for n in EXPECTED if n not in checks]
    assert not missing, f"sketch did not report: {missing}\n{output}"
    failed = [f"{n}: {checks[n][1]}" for n in EXPECTED if not checks[n][0]]
    assert not failed, "dither broken:\n  " + "\n  ".join(failed) + f"\n\n{output}"

    splits = SPLIT.findall(output)
    assert splits, f"no split renders reported\n{output}"
    assert {c for c, *_ in splits} == CASES, f"cases run: {sorted({c for c, *_ in splits})}"
    for case, limit, tiles, ok, match in splits:
        assert ok == "1", f"{case} at limit {limit} failed to render\n{output}"
        assert match == "1", f"{case} at limit {limit} differs from the reference\n{output}"

    # A run where every limit collapsed to one tile would prove nothing.
    for case in CASES:
        counts = {int(t) for c, _, t, _, _ in splits if c == case}
        assert max(counts) > 1, f"{case}: never split ({counts})"

    covers = {m.group(1): int(m.group(4)) for m in COVER.finditer(output)}
    assert set(covers) == CASES, f"coverage missing for {CASES - set(covers)}"
    # A 0..255 ramp through Bayer should land near half ink. Wide bounds: this
    # is here to catch a collapse to solid black/white, not to pin the exact
    # value, which the split comparison already fixes.
    for case in ("bayer4x4", "bayer8x8"):
        assert 35 <= covers[case] <= 65, f"{case} ink coverage {covers[case]}% looks wrong"
    # Threshold at 128 over a 0..255 ramp is black for exactly half the columns.
    assert covers["threshold128"] == 50, f"threshold128 coverage {covers['threshold128']}%"
    assert covers["threshold64"] == 25, f"threshold64 coverage {covers['threshold64']}%"
