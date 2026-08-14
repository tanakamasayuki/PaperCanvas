"""Fit modes are easy to get right for one example and wrong in general.

So the source is 4:1 and the box is 1:1, and the drawn extent is measured from
the page. With a square source in a square box, Contain, Cover and Stretch all
produce the same rectangle and the test would prove nothing; with mismatched
ratios each mode has to land somewhere different, and the aspect ratio is
checkable.
"""

import re

CHECK = re.compile(r"#CHECK name=(\S+) ok=(\d) note=(.*)")
FIT = re.compile(r"#FIT name=(\S+) w=(\d+) h=(\d+) x=(-?\d+) y=(-?\d+) warn=0x([0-9a-f]+)")

EXPECTED = [
    "formats_agree",
    "invert_blanks_solid_black",
    "reduce_warns",
    "enlarge_does_not_warn",
    "receipt_image_height",
    "split_invariant",
]

SRC_W, SRC_H = 40, 10
BOX_W, BOX_H = 100, 100


def test_image(dut):
    dut.expect("TEST start image", timeout=30)
    dut.expect("#DONE", timeout=120)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    checks = {m.group(1): (m.group(2) == "1", m.group(3)) for m in CHECK.finditer(output)}
    missing = [n for n in EXPECTED if n not in checks]
    assert not missing, f"sketch did not report: {missing}\n{output}"
    failed = [f"{n}: {checks[n][1]}" for n in EXPECTED if not checks[n][0]]
    assert not failed, "image handling broken:\n  " + "\n  ".join(failed) + f"\n\n{output}"

    fits = {m.group(1): tuple(int(m.group(i)) for i in (2, 3, 4, 5)) for m in FIT.finditer(output)}
    for name in ("none", "scale2", "contain", "cover", "stretch"):
        assert name in fits, f"{name} not reported\n{output}"

    w, h, _, _ = fits["none"]
    assert (w, h) == (SRC_W, SRC_H), f"Fit::None drew {w}x{h}, expected {SRC_W}x{SRC_H}"

    w, h, _, _ = fits["scale2"]
    assert (w, h) == (SRC_W * 2, SRC_H * 2), f"Fit::Scale 2x drew {w}x{h}"

    # Contain: fits inside on both axes, touches the box on the limiting one
    # (width here, since the source is wider than the box's ratio), and keeps
    # the 4:1 ratio.
    w, h, _, _ = fits["contain"]
    assert w <= BOX_W and h <= BOX_H, f"Contain drew {w}x{h}, outside a {BOX_W}x{BOX_H} box"
    assert w == BOX_W, f"Contain drew {w} wide; the width is the limiting axis here"
    assert abs(w / h - SRC_W / SRC_H) < 0.15, f"Contain distorted the ratio: {w}x{h}"

    # Cover: covers on both axes, overflows on the non-limiting one, ratio kept.
    w, h, _, _ = fits["cover"]
    assert w >= BOX_W and h >= BOX_H, f"Cover drew {w}x{h}, not covering {BOX_W}x{BOX_H}"
    assert abs(w / h - SRC_W / SRC_H) < 0.15, f"Cover distorted the ratio: {w}x{h}"

    # Stretch: exactly the box, ratio deliberately not kept.
    w, h, _, _ = fits["stretch"]
    assert (w, h) == (BOX_W, BOX_H), f"Stretch drew {w}x{h}, expected {BOX_W}x{BOX_H}"

    # Contain and Cover must differ; if they agree, one of them is not doing its
    # job on a mismatched aspect ratio.
    assert fits["contain"][:2] != fits["cover"][:2], (
        f"Contain and Cover both drew {fits['contain'][:2]}\n{output}"
    )

    splits = re.findall(r"#SPLIT limit=(\d+) ok=(\d)", output)
    assert len(splits) == 4, f"expected 4 split renders, got {len(splits)}\n{output}"
    assert all(ok == "1" for _, ok in splits), f"a split render failed\n{output}"
