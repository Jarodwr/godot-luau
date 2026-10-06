@tool
extends Node
# The script editor's Luau highlighter (ADR 0053): chosen for Luau scripts, and
# its colours for sample Luau and Fennel
func _ready() -> void:
	if not Engine.is_editor_hint():
		return
	await get_tree().process_frame
	for path in ["res://tool_test/tool_node.luau", "res://features/by_name.luau", "res://errors/thrower.fnl"]:
		EditorInterface.edit_script(load(path))
		await get_tree().process_frame
		var editor := EditorInterface.get_script_editor().get_current_editor()
		var highlighter = editor.get_base_editor().syntax_highlighter if editor else null
		print("HIGHLIGHT default ", path.get_file(), " ", highlighter.get_class() if highlighter else "none")
		if highlighter and path.ends_with(".fnl"):
			# The live editor knows it's Fennel: in (local T {:extends "Node"}),
			# :extends (column 10) is a string, not ":" and a name as in Luau
			var string_color: Color = EditorInterface.get_editor_settings().get_setting("text_editor/theme/highlighting/string_color")
			var line0: Dictionary = highlighter.get_line_syntax_highlighting(0)
			print("HIGHLIGHT live fennel keyword ", line0.has(10) and line0[10]["color"] == string_color)
	var h = ClassDB.instantiate("LuauHighlighter")
	var luau := "local x = 1 -- hi\nif x then print(x.y, v:len()) end\n--[[ a\nb ]] s = [==[\nlong]==] .. \"q\"\nlocal n: Node = Node.new(Vector2(1, 0x1F))"
	print("HIGHLIGHT luau ", JSON.stringify(h.highlight_text(luau, false)))
	var fennel := "; c\n(fn M.f [self] (if x :key \"s\n t\" 12))\n(Node.new)"
	print("HIGHLIGHT fennel ", JSON.stringify(h.highlight_text(fennel, true)))
