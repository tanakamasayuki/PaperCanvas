#!/usr/bin/env python3
"""Generate cases.h from cases.json.

The .ino cannot parse JSON, and hand-keeping two copies of the case list is
exactly how the two sides of a parity test end up testing different things. So
cases.json is the single source and this writes the C++ view of it.

Run after editing cases.json. test_js_parity.py checks the header is current,
so a forgotten regeneration fails the test rather than silently comparing the
wrong content.
"""

import json
import pathlib
import sys

HERE = pathlib.Path(__file__).parent


def render(spec: dict) -> str:
    out = [
        "// GENERATED from cases.json by gen_cases.py. Do not edit by hand.",
        "#pragma once",
        "",
        "#include <LovyanGFX.hpp>",
        "",
        "struct ParityCase {",
        "  const char* name;",
        "  const lgfx::IFont* font;",
        "  const char* text;",
        "  float size;",
        "};",
        "",
        f"#define PARITY_CANVAS_W {spec['canvasWidth']}",
        f"#define PARITY_CANVAS_H {spec['canvasHeight']}",
        "",
        "static const ParityCase PARITY_CASES[] = {",
    ]
    for c in spec["cases"]:
        text = c["text"].replace("\\", "\\\\").replace('"', '\\"')
        out.append(f'    {{"{c["name"]}", &fonts::{c["font"]}, u8"{text}", {c["size"]:.6f}f}},')
    out += [
        "};",
        "",
        "static const size_t PARITY_CASE_COUNT = sizeof(PARITY_CASES) / sizeof(PARITY_CASES[0]);",
        "",
    ]
    return "\n".join(out)


def expected() -> str:
    return render(json.loads((HERE / "cases.json").read_text(encoding="utf-8")))


if __name__ == "__main__":
    text = expected()
    path = HERE / "cases.h"
    if "--check" in sys.argv:
        current = path.read_text(encoding="utf-8") if path.exists() else ""
        if current != text:
            print("cases.h is out of date; run gen_cases.py", file=sys.stderr)
            sys.exit(1)
        print("cases.h is current")
    else:
        path.write_text(text, encoding="utf-8")
        print(f"wrote {path}")
