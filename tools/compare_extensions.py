"""Side-by-side table of two benchmark result files (same runner, same cases).

    python3 tools/compare_extensions.py A.json "Name A" B.json "Name B"

Prints a Markdown table: GDScript (mean of both runs, which also shows how
noisy the machine was), each extension's ns per op, and its ratio to
GDScript. Cases an extension can't run show as "unsupported".
"""

import json
import sys


def load(path):
    with open(path) as f:
        return json.load(f)["cases"]


def cell(case):
    if case is None or str(case.get("check", "")).startswith("UNSUPPORTED"):
        return None
    return case["fennel_ns"]


def fmt(ns):
    return "unsupported" if ns is None else f"{ns:.1f}"


def ratio(ns, gd):
    return "" if ns is None or not gd else f"{ns / gd:.2f}×"


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    a, a_name, b, b_name = load(argv[0]), argv[1], load(argv[2]), argv[3]
    print(f"| Case | GDScript | {a_name} | × | {b_name} | × |")
    print("|---|---:|---:|---:|---:|---:|")
    for name in a:
        gd_values = [c["gdscript_ns"] for c in (a.get(name), b.get(name)) if c and c.get("gdscript_ns")]
        gd = sum(gd_values) / len(gd_values) if gd_values else None
        va, vb = cell(a.get(name)), cell(b.get(name))
        print(f"| `{name}` | {fmt(gd)} | {fmt(va)} | {ratio(va, gd)} | {fmt(vb)} | {ratio(vb, gd)} |")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
