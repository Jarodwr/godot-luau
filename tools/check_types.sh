#!/bin/sh
# Type checks with luau-lsp and the generated Godot definitions (ADR 0048):
# demo/types/typed_mover.luau has no errors, and each mistake in
# demo/types/mistakes.luau is reported.
#
#     LUAU_LSP=/path/to/luau-lsp tools/check_types.sh
set -u
cd "$(dirname "$0")/.."
lsp="${LUAU_LSP:-luau-lsp}"
defs=addons/godot_luau/types/godot.d.luau
[ -f "$defs" ] || { echo "build first: $defs is missing"; exit 1; }
cd demo
analyze() {
	"$lsp" analyze --platform standard --flag:LuauTarjanChildLimit=100000 --definitions "@godot=../$defs" "$@" 2>&1 | grep -v '^\[INFO\]'
}
failures=0
expect() {  # expect OUTPUT TEXT
	if printf '%s\n' "$1" | grep -qF -- "$2"; then
		echo "ok   $2"
	else
		echo "FAIL $2"
		failures=$((failures + 1))
	fi
}

out=$(analyze types/typed_mover.luau types/util.luau)
if [ -z "$out" ]; then
	echo "ok   typed_mover.luau has no errors"
else
	echo "FAIL typed_mover.luau:"
	printf '%s\n' "$out"
	failures=$((failures + 1))
fi

out=$(analyze types/mistakes.luau)
expect "$out" "mistakes.luau(4,1): TypeError: Key 'add_chld' not found in external type 'Node2D'"
expect "$out" "mistakes.luau(5,17): TypeError: Expected this to be 'Vector2', but got 'string'"
expect "$out" "mistakes.luau(6,13): TypeError: Expected this to be 'number', but got 'string'"
expect "$out" "mistakes.luau(7,1): TypeError: Expected this to be 'number', but got 'string'"
expect "$out" "mistakes.luau(8,11): TypeError: Unknown global 'Tmer'"
expect "$out" "mistakes.luau(9,20): TypeError: Expected this to be"
count=$(printf '%s\n' "$out" | grep -c 'TypeError')
expect "errors: $count" "errors: 6"

echo "failures: $failures"
exit $failures
