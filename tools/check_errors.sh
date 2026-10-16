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
expect "'extends' must be an engine class (gd.Node2D) or a required script, gd.Node2D, not the string \"Node2D\": res://errors/extends_string.luau"
expect "'extends' must be an engine class (gd.Node2D) or a required script, require(\"res://...\"), not the string \"LuauFeatures\": res://errors/extends_class_name.luau"
expect "'extends' must be an engine class (gd.Node2D) or a required script: res://errors/extends_singleton.luau"
expect "SCRIPT ERROR: gd has no member 'Nod2D'"
expect "at: function at line 1 (res://errors/extends_typo.luau:1)"
expect "Compile error: defnode's body has prop, signal, alias, method and section forms, not (fn …)"
expect "at: load (res://errors/defnode_bad.fnl:4)"
# import-macros from res:// (ADR 0062)
expect "Compile error: macros.missing module not found."
expect "at: load (res://errors/macros_missing.fnl:3)"
# Declared types are gd values (ADR 0059)
expect "the type of 'health' must be gd.int, gd.Vector2, an engine class (gd.Node) or a required script, gd.int, not the string \"int\": res://errors/type_string.luau"
expect "signal scored's argument \"points: int\" is a name and a type: \"points\", gd.int: res://errors/signal_type_string.luau"
expect "the type of 'target' must be gd.int, gd.Vector2, an engine class (gd.Node) or a required script: res://errors/type_bad.luau"
expect "Compile error: a type is a gd value: gd.int, not :int"
expect "at: load (res://errors/defnode_type_string.fnl:4)"
expect "Compile error: a signal argument is a name or [name type]: (signal hit [damage gd.int] by)"
expect "at: load (res://errors/defnode_signal_flat.fnl:4)"
# Declarations are tables; a default isn't shared (ADR 0060)
expect "export 'speed' must be a table: { default = ... } or { type = ... }: res://errors/export_shorthand.luau"
expect "the default of export 'items' would be one value every instance shares: default = function() return ... end makes one per instance: res://errors/default_shared.luau"
expect "Compile error: the default of 'doubled' can't read self; set it in _ready"
expect "at: load (res://errors/defnode_default_self.fnl:4)"
expect "--- end"
echo "failures: $failures"
exit $failures
