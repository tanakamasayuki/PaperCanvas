"""Placement is what decides whether a printed barcode scans.

Encoding is BarcodeKit's job and is tested there. What PaperCanvas owns is the
scale, the quiet zone and the guard bars — and each of those, done wrong,
produces something that looks like a barcode and cannot be read.

So the sketch checks the geometry on the rendered page, and this module decodes
those pages with zxing-cpp, which is the closest a host test gets to holding a
scanner over the paper. Geometry alone can be right while the symbol is wrong,
and a decoder can be lenient about a symbol a real scanner would reject; the two
together are worth more than either.
"""

import re
from pathlib import Path

import zxingcpp
from PIL import Image

SKETCH_DIR = Path(__file__).parent
CHECK = re.compile(r"#CHECK name=(\S+) ok=(\d) note=(.*)")

EXPECTED = [
    "encode_ok",
    "layout_fits",
    "layout_whole_scale",
    "layout_largest_scale",
    "layout_quiet_zone",
    "short_scale_above_one",
    "short_runs_whole",
    "page_builds",
    "page_height_is_layout",
    "quiet_zone_left_blank",
    "quiet_zone_right_blank",
    "module_runs_whole",
    "ean_encode_ok",
    "guard_bars_extend",
    "qr_encode_ok",
    "qr_placed",
    "qr_square",
    "too_small_not_drawn",
    "too_small_warned",
    "split_invariant",
]

# file, expected text, expected format
DECODES = [
    ("code128.pbm", "PAPERCANVAS-1", "Code128"),
    ("ean13.pbm", "4901234567894", "EAN13"),
    ("qr.pbm", "https://example.com/", "QRCode"),
    ("code128_short.pbm", "PC-1", "Code128"),
]


def _load(path):
    # PBM P4 and PaperCanvas both use 1 = black, so the page bytes go in as they
    # are; Pillow maps them to 0 = black in "L", which is what zxing expects.
    return Image.open(path).convert("L")


def _norm(name):
    """zxing spells formats "Code 128" / "EAN-13" / "QR Code"; compare on letters."""
    return "".join(c for c in str(name).lower() if c.isalnum())


def test_barcode(dut):
    dut.expect("TEST start barcode", timeout=30)
    dut.expect("#DONE", timeout=120)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    checks = {m.group(1): (m.group(2) == "1", m.group(3)) for m in CHECK.finditer(output)}
    missing = [n for n in EXPECTED if n not in checks]
    assert not missing, f"sketch did not report: {missing}\n{output}"
    failed = [f"{n}: {checks[n][1]}" for n in EXPECTED if not checks[n][0]]
    assert not failed, "barcode placement broken:\n  " + "\n  ".join(failed) + f"\n\n{output}"

    splits = re.findall(r"#SPLIT limit=(\d+) ok=(\d)", output)
    assert len(splits) == 4, f"expected 4 split renders, got {len(splits)}\n{output}"
    assert all(ok == "1" for _, ok in splits), f"a split render failed\n{output}"

    out = SKETCH_DIR / "output"
    failures = []
    for name, want_text, want_format in DECODES:
        path = out / name
        assert path.exists(), f"{name} was not written\n{output}"
        img = _load(path)
        results = zxingcpp.read_barcodes(img)
        if not results:
            failures.append(f"{name}: nothing decoded")
            continue
        got = results[0]
        if got.text != want_text:
            failures.append(f"{name}: decoded {got.text!r}, expected {want_text!r}")
        # The format matters as much as the payload: the right digits read under
        # the wrong symbology means the symbol is not what was asked for.
        if _norm(want_format) != _norm(got.format):
            failures.append(f"{name}: format {got.format}, expected {want_format}")

    assert not failures, "decode failed:\n  " + "\n  ".join(failures) + f"\n\n{output}"
