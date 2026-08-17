"""The tool's output has to compile and run, not just read well.

The browser tool emits a C++ header. Nothing in the tool exercises that header —
the preview is rendered by JavaScript and never touches the generated code — so
a generator that emits something plausible but wrong would look fine right up
until someone pasted it into a sketch.

Representative headers for both editors are committed under this directory and
built here. They must compile, produce a page, and — for the receipt — grow by
exactly one row per item, which is what the generated table promises.
"""

import re


def test_gencheck(dut):
    dut.expect("TEST start gencheck", timeout=30)
    dut.expect("#DONE", timeout=60)
    output = dut.pexpect_proc.before
    if isinstance(output, bytes):
        output = output.decode("utf-8", "replace")

    m = re.search(r"#GEN items=(\d+) height=(\d+) count=(\d+) ok=(\d) warn=0x([0-9a-f]+)", output)
    assert m, f"no report from the generated code\n{output}"
    items, height, count, ok = (int(m.group(i)) for i in (1, 2, 3, 4))
    assert ok, f"the generated layout failed to build\n{output}"
    assert height > 0, "the generated layout produced no page"

    # Header, its rule, three item rows, the closing rule: the table alone is
    # six elements, so a generator that emitted the loop body once would show up
    # as a much smaller count.
    assert count >= items + 6, f"only {count} elements for {items} items\n{output}"

    lab = re.search(r"#GENLABEL h=(\d+) count=(\d+) ok=(\d) warn=0x([0-9a-f]+)", output)
    assert lab, f"no report from the generated label\n{output}"
    assert lab.group(3) == "1", f"the generated label failed to build\n{output}"
    assert int(lab.group(1)) == 240, f"label height {lab.group(1)}, expected 240"
    assert int(lab.group(2)) == 5, f"label has {lab.group(2)} elements, expected 5"
    assert int(lab.group(4), 16) == 0, (
        f"the generated label raised warnings 0x{lab.group(4)}; the tool should "
        "not emit a layout that does not fit"
    )

    m2 = re.search(r"#GEN2 items=2 height=(\d+)", output)
    assert m2, f"no second report\n{output}"
    shorter = int(m2.group(1))
    assert shorter < height, (
        f"dropping an item did not shorten the page ({shorter} vs {height}); "
        "the table is not repeating per item"
    )
