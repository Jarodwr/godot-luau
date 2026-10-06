#!/bin/sh
# Runs demo/errors.gd and checks that each script error is reported with the
# right message, location and backtrace (docs/adr/0037).
#
#     GODOT_BIN=/path/to/godot tools/check_errors.sh
set -u
cd "$(dirname "$0")/../demo"
out=$("${GODOT_BIN:-godot}" --headless --path . --script errors.gd 2>&1)
failures=0
expect() {
	if printf '%s\n' "$out" | grep -qF -- "$1"; then
		echo "ok   $1"
	else
		echo "FAIL $1"
		failures=$((failures + 1))
	fi
}
expect "at: nested (res://errors/thrower.luau:8)"
expect "Luau backtrace (most recent call first):"
expect "[0] level3 (res://errors/thrower.luau:19)"
expect "[1] level2 (res://errors/thrower.luau:20)"
expect "[2] chain (res://errors/thrower.luau:21)"
expect "at: get_broken (res://errors/thrower.luau:24)"
expect "SCRIPT ERROR: Node.add_child: argument 1 should be Object"
expect "at: after_await (res://errors/thrower.luau:33)"
expect "at: function at line 37 (res://errors/thrower.luau:37)"
expect "at: load (res://errors/syntax.luau:3)"
expect "at: function at line 3 (res://errors/thrower.fnl:5)"
expect "'extends': no native class or class_name 'NoSuchClass': res://errors/extends_unknown.luau"
expect "'extends': GdNamed is a GDScript class; a Luau script extends native classes and Luau scripts: res://errors/extends_gdscript.luau"
expect "--- end"
echo "failures: $failures"
exit $failures
