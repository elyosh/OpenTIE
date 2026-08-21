#!/usr/bin/env python3
"""
Parser for tools/film/lfd-palettes.yaml. Emits tab-separated rules to
stdout, one per line:

    <glob>\\t<palette-or-dash>\\t<extras-comma-separated-or-dash>

Used by tools/film/extract-all-lfds.sh. Stdlib-only — no PyYAML dependency
— because the YAML schema is small enough (a list of three-key
mappings) that a regex walker handles every legitimate file.

Lives in its own file rather than inline in the shell script because
bash 3.2 (macOS default) mis-parses quoted heredocs inside command
substitution.
"""

import re
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: lfd-palettes-parse.py <yaml-file>", file=sys.stderr)
        return 2
    try:
        with open(sys.argv[1]) as f:
            text = f.read()
    except FileNotFoundError:
        return 0          # missing file == no overrides

    # Strip comments + blank lines; we only care about the
    # `overrides:` list.
    lines = [ln.rstrip() for ln in text.splitlines()
             if ln.strip() and not ln.strip().startswith("#")]
    src = "\n".join(lines)

    m = re.search(r"^overrides:\s*\n((?:.*\n?)*)", src, re.MULTILINE)
    if not m:
        return 0
    body = m.group(1)

    # Split into per-rule blocks (each starts with `- glob:`).
    rules = re.split(r"^\s*-\s+glob:\s*", body, flags=re.MULTILINE)[1:]
    for r in rules:
        glob_m   = re.match(r'\s*["\']?([^"\'\n]+?)["\']?\s*\n', r)
        pal_m    = re.search(r'^\s*palette:\s*(.+?)\s*$', r, re.MULTILINE)
        extras_m = re.search(r'^\s*extras:\s*\[(.*?)\]\s*$', r, re.MULTILINE)
        if not glob_m:
            continue
        glob = glob_m.group(1).strip()

        pal = pal_m.group(1).strip() if pal_m else ""
        if pal in ("", "null", "~"):
            pal = "-"

        extras = ""
        if extras_m:
            items = [x.strip().strip('"\'')
                     for x in extras_m.group(1).split(",") if x.strip()]
            extras = ",".join(items)
        if not extras:
            extras = "-"

        print(f"{glob}\t{pal}\t{extras}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
