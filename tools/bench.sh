#!/usr/bin/env bash
# Builds the dev library and runs the benchmark.
#
#   GODOT_BIN=/path/to/godot tools/bench.sh OUT.json [runner options]
#
# Runner options: --only=TEXT, --repeats=N, --scale=X, --mover=res://cases/mover.fnl
# Compare two runs with fennel-gdextension's benchmark/tools/compare.py (same
# JSON format).
set -euo pipefail
cd "$(dirname "$0")/.."
out="$(cd "$(dirname "${1:?usage: bench.sh OUT.json [options]}")" && pwd)/$(basename "$1")"
shift
GODOT="${GODOT_BIN:-godot}"
mise exec -- cmake --build --preset dev > /dev/null
cd demo/benchmark
"$GODOT" --headless --quit --path . --editor > /dev/null 2>&1 || true
"$GODOT" --headless --path . --script runner.gd -- --out="$out" "$@" | grep -v "^Godot Engine\|^$"
