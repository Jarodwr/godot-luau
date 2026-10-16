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
	file.store_string('local T = { extends = gd.Node, tool = true, exports = { kept = { default = 0 } } }\nfunction T:value() return 1 end\nreturn T\n')
	file.close()
	var script: Script = load(path)
	var node := Node.new()
	node.set_script(script)
	add_child(node)
	node.set("kept", 7)
	var before = node.value()
	script.source_code = 'local T = { extends = gd.Node, tool = true, exports = { kept = { default = 0 } } }\nfunction T:value() return 2 end\nreturn T\n'
	var saved := ResourceSaver.save(script)
	var on_disk := FileAccess.get_file_as_string(path)
	print("PROBE save=", saved, " before=", before, " after=", node.value(), " kept=", node.get("kept"), " disk=", "return 2" in on_disk)
	node.free()
	for leftover in [path, path + ".uid"]:
		if FileAccess.file_exists(leftover):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(leftover))

	# The New Script dialog (ADR 0058): Fennel is a language there, and a
	# .fnl script it creates is a defnode named after the file
	var dialog := ScriptCreateDialog.new()
	EditorInterface.get_base_control().add_child(dialog)
	var languages: OptionButton = dialog.find_children("*", "OptionButton", true, false).filter(
			func(o): return range(o.item_count).any(func(i): return o.get_item_text(i) == "Fennel"))[0]
	for i in languages.item_count:
		if languages.get_item_text(i) == "Fennel":
			languages.select(i)
			languages.item_selected.emit(i)
	var created := "res://tool_test/new_tmp.fnl"
	dialog.config("Node2D", created, false, false)
	await get_tree().process_frame
	var ok_enabled := not dialog.get_ok_button().disabled
	dialog.get_ok_button().pressed.emit()
	var text := FileAccess.get_file_as_string(created)
	print("PROBE new fennel ok=", ok_enabled, " source=", text.c_escape())
	dialog.queue_free()
	for leftover in [created, created + ".uid"]:
		if FileAccess.file_exists(leftover):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(leftover))
