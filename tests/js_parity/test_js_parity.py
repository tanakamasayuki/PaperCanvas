"""Does the browser tool's preview match what the device prints?

The tool's whole value is that what you see is what gets printed. That claim is
checked here, in bytes: the same cases are rendered by PaperCanvas (C++, through
the real LovyanGFX) and by lgfx-font-tool (JS), and the 1bpp pages must be
identical. 1bpp with no antialiasing means there is no "close enough" to hide
behind — a single differing pixel fails.

What this does *not* re-test is glyph shapes; lgfx-font-tool already verifies
those against the real LovyanGFX across all 186 fonts. What is unverified until
here is everything around them: where PaperCanvas puts the text, how it applies
a size multiplier, and how it packs the page.

Both sides read cases.json, so they cannot drift onto different content. The
C++ side reads it through the generated cases.h; see gen_cases().
"""

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
import gen_cases  # noqa: E402

SKETCH_DIR = Path(__file__).parent
CASES = json.loads((SKETCH_DIR / "cases.json").read_text(encoding="utf-8"))

CPP_CASE = re.compile(r"#CASE name=(\S+) ok=(\d) warn=0x([0-9a-f]+) data=([0-9a-f]+)")
JS_CASE = re.compile(r"#CASE name=(\S+) data=([0-9a-f]+)")


def _render_js():
    """Run the JS side. Skips rather than fails when the toolchain is absent —
    a developer without Node should still be able to run the rest of the suite."""
    if shutil.which("node") is None:
        pytest.skip("node not installed; the JS side of the parity check cannot run")
    if not (SKETCH_DIR / "node_modules" / "lgfx-font-tool").exists():
        pytest.skip("lgfx-font-tool not installed; run `npm install` in tests/js_parity")

    proc = subprocess.run(
        ["node", "render.mjs"],
        cwd=SKETCH_DIR,
        capture_output=True,
        text=True,
        timeout=300,
    )
    assert proc.returncode == 0, f"render.mjs failed:\n{proc.stdout}\n{proc.stderr}"
    assert "#DONE" in proc.stdout, f"render.mjs did not finish:\n{proc.stdout}"
    return {m.group(1): m.group(2) for m in JS_CASE.finditer(proc.stdout)}


def _describe(name, cpp_hex, js_hex, width, height):
    """Where the two pages differ, in terms someone can act on."""
    row_bytes = (width + 7) // 8
    cpp = bytes.fromhex(cpp_hex)
    js = bytes.fromhex(js_hex)
    if len(cpp) != len(js):
        return f"{name}: sizes differ, C++ {len(cpp)} bytes vs JS {len(js)}"

    rows = sorted({i // row_bytes for i in range(len(cpp)) if cpp[i] != js[i]})
    cols = set()
    for i in range(len(cpp)):
        if cpp[i] == js[i]:
            continue
        base = (i % row_bytes) * 8
        diff = cpp[i] ^ js[i]
        cols.update(base + b for b in range(8) if diff & (0x80 >> b))

    ink_cpp = sum(bin(b).count("1") for b in cpp)
    ink_js = sum(bin(b).count("1") for b in js)
    detail = (
        f"{name}: differs on rows {rows[0]}-{rows[-1]} ({len(rows)} rows), "
        f"columns {min(cols)}-{max(cols)}, ink C++ {ink_cpp} vs JS {ink_js}"
    )
    # A pure horizontal or vertical offset is by far the most likely cause, and
    # naming it saves the reader from decoding a hex dump to find out.
    if ink_cpp == ink_js:
        detail += " — same ink count, so this looks like an offset, not different glyphs"
    return detail


def test_cases_header_is_current():
    """cases.h is generated from cases.json; a stale one would silently make the
    two sides compare different text, which is the one failure this test cannot
    otherwise see."""
    header = SKETCH_DIR / "cases.h"
    assert header.exists(), "cases.h is missing; run gen_cases.py"
    assert header.read_text(encoding="utf-8") == gen_cases.expected(), (
        "cases.h does not match cases.json; run tests/js_parity/gen_cases.py"
    )


def test_js_parity(dut):
    dut.expect("TEST start js_parity", timeout=30)
    dut.expect("#DONE", timeout=120)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    canvas = re.search(r"#CANVAS w=(\d+) h=(\d+) rowBytes=(\d+) cases=(\d+)", output)
    assert canvas, f"no canvas report\n{output[:2000]}"
    width, height, row_bytes, count = (int(canvas.group(i)) for i in (1, 2, 3, 4))
    assert width == CASES["canvasWidth"] and height == CASES["canvasHeight"], (
        "the sketch's canvas does not match cases.json; regenerate cases.h"
    )
    assert count == len(CASES["cases"]), (
        f"the sketch has {count} cases, cases.json has {len(CASES['cases'])}; "
        "regenerate cases.h"
    )

    cpp = {m.group(1): (m.group(2) == "1", int(m.group(3), 16), m.group(4))
           for m in CPP_CASE.finditer(output)}
    missing = [c["name"] for c in CASES["cases"] if c["name"] not in cpp]
    assert not missing, f"the sketch did not report: {missing}"

    failed_build = [n for n, (ok, _, _) in cpp.items() if not ok]
    assert not failed_build, f"these cases failed to build: {failed_build}"

    js = _render_js()
    missing_js = [c["name"] for c in CASES["cases"] if c["name"] not in js]
    assert not missing_js, f"render.mjs did not report: {missing_js}"

    expected_len = row_bytes * height * 2  # hex characters
    mismatches = []
    for case in CASES["cases"]:
        name = case["name"]
        _, warn, cpp_hex = cpp[name]
        js_hex = js[name]
        assert len(cpp_hex) == expected_len, f"{name}: C++ page is the wrong size"
        # Clipping on the C++ side would mean the two rendered different text,
        # which makes a byte comparison meaningless rather than merely failing.
        assert not warn & 0x0001, f"{name}: C++ clipped the text (warn 0x{warn:04x})"
        if cpp_hex != js_hex:
            mismatches.append(_describe(name, cpp_hex, js_hex, width, height))

    assert not mismatches, (
        "the JS preview does not match what PaperCanvas prints:\n  "
        + "\n  ".join(mismatches)
    )
