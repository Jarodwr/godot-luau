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
expect "$out" "PROBE save=0 before=1 after=2 kept=7 disk=true"  # saving reloads in place (ADR 0050)
# The script editor's highlighter (ADR 0053): the default for .luau and .fnl,
# and its colours for sample Luau and Fennel (tool_test/highlight_probe.gd)
expect "$out" 'HIGHLIGHT default tool_node.luau LuauHighlighter'
expect "$out" 'HIGHLIGHT default by_name.luau LuauHighlighter'
expect "$out" 'HIGHLIGHT default thrower.fnl LuauHighlighter'
expect "$out" 'HIGHLIGHT live fennel keyword true'
expect "$out" 'HIGHLIGHT luau {"0":{"0":"keyword","6":"text","8":"symbol","10":"number","12":"comment"},"1":{"0":"control_flow","3":"text","5":"control_flow","10":"function","15":"symbol","16":"text","17":"symbol","18":"member","19":"symbol","21":"text","22":"symbol","23":"function","26":"symbol","30":"control_flow"},"2":{"0":"comment"},"3":{"0":"comment","5":"text","7":"symbol","9":"string"},"4":{"0":"string","9":"symbol","12":"string"},"5":{"0":"keyword","6":"text","7":"symbol","9":"engine_type","14":"symbol","16":"engine_type","20":"symbol","21":"function","24":"symbol","25":"base_type","32":"symbol","33":"number","34":"symbol","36":"number","40":"symbol"}}'
expect "$out" 'HIGHLIGHT fennel {"0":{"0":"comment"},"1":{"0":"symbol","1":"keyword","4":"text","8":"symbol","9":"text","13":"symbol","16":"control_flow","19":"text","21":"string"},"2":{"0":"string","4":"number","6":"symbol"},"3":{"0":"symbol","1":"function","9":"symbol"}}'
reject "$out" "SCRIPT ERROR"

# In the game: both run
out=$("$G" --headless --path . res://tool_test/tool.tscn --quit-after 5 2>&1)
expect "$out" "LUAU TOOL READY editor=false size=5"
expect "$out" "LUAU PLAIN READY"

echo "failures: $failures"
exit $failures
