@tool
extends Node
# Reads the Luau nodes' properties in the editor: the tool node's real value,
# the plain node's placeholder value. Then saves a changed tool script the way
# the script editor does, and checks its node takes the new version (ADR 0050).
func _ready() -> void:
	if not Engine.is_editor_hint():
		return
	print("PROBE tool.size=", get_node("../Tool").get("size"), " plain.speed=", get_node("../Plain").get("speed"))

	var path := "res://tool_test/reload_tmp.luau"
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string('local T = { extends = "Node", tool = true, exports = { kept = 0 } }\nfunction T:value() return 1 end\nreturn T\n')
	file.close()
	var script: Script = load(path)
	var node := Node.new()
	node.set_script(script)
	add_child(node)
	node.set("kept", 7)
	var before = node.value()
	script.source_code = 'local T = { extends = "Node", tool = true, exports = { kept = 0 } }\nfunction T:value() return 2 end\nreturn T\n'
	var saved := ResourceSaver.save(script)
	var on_disk := FileAccess.get_file_as_string(path)
	print("PROBE save=", saved, " before=", before, " after=", node.value(), " kept=", node.get("kept"), " disk=", "return 2" in on_disk)
	node.free()
	for leftover in [path, path + ".uid"]:
		if FileAccess.file_exists(leftover):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(leftover))
