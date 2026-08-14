"""Every warning under the condition it names, and under nothing else.

A diagnostic that fires spuriously is worse than none, because callers stop
reading it. So the sketch reports the exact flag word for each case and a stray
bit fails exactly like a missing one — checking only "the expected bit is set"
would let the flags drift into meaning nothing.
"""

import re

CHECK = re.compile(r"#CHECK name=(\S+) ok=(\d) note=(.*)")
WARN = re.compile(r"#WARN case=(\S+) got=0x([0-9a-f]+) want=0x([0-9a-f]+)")

CASES = [
    "quiet_receipt",
    "quiet_label",
    "quiet_image",
    "text_wrapped",
    "text_clipped",
    "image_scaled",
    "image_clipped",
    "out_of_bounds",
    "out_of_bounds_negative",
    "barcode_too_small",
    "barcode_fits_quiet",
]

EXPECTED = CASES + [
    "clear_warnings",
    "warnings_do_not_stop_build",
    "warnings_accumulate",
    "warned_elements_kept",
]


def test_warnings(dut):
    dut.expect("TEST start warnings", timeout=30)
    dut.expect("#DONE", timeout=120)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    checks = {m.group(1): (m.group(2) == "1", m.group(3)) for m in CHECK.finditer(output)}
    missing = [n for n in EXPECTED if n not in checks]
    assert not missing, f"sketch did not report: {missing}\n{output}"

    warns = {m.group(1): (int(m.group(2), 16), int(m.group(3), 16)) for m in WARN.finditer(output)}
    wrong = [
        f"{name}: raised 0x{got:04x}, expected 0x{want:04x}"
        for name, (got, want) in warns.items()
        if got != want
    ]
    assert not wrong, "warning flags wrong:\n  " + "\n  ".join(wrong) + f"\n\n{output}"

    missing_cases = [c for c in CASES if c not in warns]
    assert not missing_cases, f"cases not exercised: {missing_cases}\n{output}"

    failed = [f"{n}: {checks[n][1]}" for n in EXPECTED if not checks[n][0]]
    assert not failed, "warnings broken:\n  " + "\n  ".join(failed) + f"\n\n{output}"

    # Every distinct warning bit the library defines should appear somewhere, or
    # a flag exists that nothing can raise.
    raised = 0
    for got, _ in warns.values():
        raised |= got
    assert raised == 0x3F, f"bits exercised: 0x{raised:02x}, expected all six (0x3f)\n{output}"
