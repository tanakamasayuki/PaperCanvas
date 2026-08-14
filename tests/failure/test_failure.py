"""Failures must fail, not degrade.

Warnings deliberately never stop a build, which makes it tempting to treat real
failures the same way — shrink the page, emit what fits, return true. A caller
that gets `true` and a short page has no way to notice, and prints it. So the
rule is the opposite one: a build that cannot produce the requested page returns
false and writes nothing.

Every failing call is made against a poisoned buffer so "wrote nothing" is
verified rather than assumed.
"""

import re

CHECK = re.compile(r"#CHECK name=(\S+) ok=(\d) note=(.*)")

EXPECTED = [
    "short_buffer_refused",
    "short_buffer_untouched",
    "exact_buffer_accepted",
    "null_buffer_refused",
    "empty_receipt_refused",
    "empty_receipt_untouched",
    "zero_width_refused",
    "zero_height_refused",
    "null_stream_refused",
    "refusal_keeps_state",
    "retry_after_refusal",
    "tiny_limit_no_partial",
    "tiny_limit_same_output",
]


def test_failure(dut):
    dut.expect("TEST start failure", timeout=30)
    dut.expect("#DONE", timeout=120)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    checks = {m.group(1): (m.group(2) == "1", m.group(3)) for m in CHECK.finditer(output)}
    missing = [n for n in EXPECTED if n not in checks]
    assert not missing, f"sketch did not report: {missing}\n{output}"
    failed = [f"{n}: {checks[n][1]}" for n in EXPECTED if not checks[n][0]]
    assert not failed, "failure handling broken:\n  " + "\n  ".join(failed) + f"\n\n{output}"

    short = re.search(r"#SHORT need=(\d+) ok=(\d) untouched=(\d)", output)
    assert short, f"no short-buffer report\n{output}"
    assert short.group(2) == "0" and short.group(3) == "1", (
        f"one byte short was not cleanly refused\n{output}"
    )

    exact = re.search(r"#EXACT need=(\d+) ok=(\d)", output)
    assert exact and exact.group(1) == short.group(1), (
        f"bufferSize() changed between the two attempts\n{output}"
    )
    assert exact.group(2) == "1", f"exactly bufferSize() was refused\n{output}"
