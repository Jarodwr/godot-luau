#!/bin/sh
# Editor integration checks (ADR 0032, 0039): the editor registers a Luau
# class_name, GDScript uses it by name, tool scripts run in the editor while
# other scripts get inspector placeholders, and both run in the game.
#
#     GODOT_BIN=/path/to/godot tools/check_editor.sh
set -u
G="${GODOT_BIN:-godot}"
cd "$(dirname "$0")/../demo"
failures=0
expect() {  # expect OUTPUT TEXT
	if printf '%s\n' "$1" | grep -qF -- "$2"; then
		echo "ok   $2"
	else
		echo "FAIL $2"
		failures=$((failures + 1))
	fi
}
reject() {  # reject OUTPUT TEXT
	if printf '%s\n' "$1" | grep -qF -- "$2"; then
		echo "FAIL unexpected: $2"
		failures=$((failures + 1))
	else
		echo "ok   not printed: $2"
	fi
}

# The editor's scan registers the global class
"$G" --headless --editor --quit --path . >/dev/null 2>&1
cache=$(cat .godot/global_script_class_cache.cfg 2>/dev/null)
expect "$cache" '"class": &"LuauFeatures"'
expect "$cache" '"language": &"Luau"'
expect "$cache" '"base": &"LuauFeatures"'  # LuauByName extends it by name (ADR 0049)

# GDScript uses it by name
out=$("$G" --headless --path . --script global_class.gd 2>&1)
expect "$out" "failures: 0"

# In the editor: the tool script runs, the other gets a placeholder
out=$("$G" --headless --editor --path . res://tool_test/tool.tscn --quit-after 120 2>&1)
expect "$out" "LUAU TOOL READY editor=true size=5"
expect "$out" "PROBE tool.size=5 plain.speed=7.5"
reject "$out" "LUAU PLAIN READY"

# In the game: both run
out=$("$G" --headless --path . res://tool_test/tool.tscn --quit-after 5 2>&1)
expect "$out" "LUAU TOOL READY editor=false size=5"
expect "$out" "LUAU PLAIN READY"

echo "failures: $failures"
exit $failures
