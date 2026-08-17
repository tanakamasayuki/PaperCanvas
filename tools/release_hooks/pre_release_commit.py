#!/usr/bin/env python3
"""Drop the browser tool from the distributed library.

Runs on the `release` branch after the shared steps (removing tests/, staging
sketch.yaml) and before the distribution commit. See the release toolkit's
README for the hook contract.

The tool lives in docs/ because that is what GitHub Pages serves from, but an
Arduino user installing the library has no use for an editor — and it is most
of the repository's weight. The design documents stay: they are the answer to
"why does it behave like this", and they are small.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Paths that make up the tool. Everything else under docs/ is documentation.
TOOL_PATHS = [
    "docs/index.html",
    "docs/receipt.html",
    "docs/label.html",
    "docs/css",
    "docs/src",
    "docs/vendor",
]


def main() -> int:
    removed = []
    for rel in TOOL_PATHS:
        path = ROOT / rel
        if not path.exists():
            continue
        # `git rm` rather than deleting: the workflow commits whatever is staged
        # and does no staging of its own after the hook.
        subprocess.run(
            ["git", "rm", "-r", "--quiet", rel],
            cwd=ROOT,
            check=True,
        )
        removed.append(rel)

    if removed:
        print(f"release: removed the browser tool from the package ({', '.join(removed)})")
    else:
        print("release: no browser tool present to remove")
    return 0


if __name__ == "__main__":
    sys.exit(main())
