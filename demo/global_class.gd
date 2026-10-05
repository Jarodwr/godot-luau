extends SceneTree
# A Luau class_name used by name from GDScript (ADR 0032, 0039). Needs the
# editor's global class cache, so run the editor's scan first:
#
#     godot --headless --editor --quit --path demo
#     godot --headless --path demo --script global_class.gd

var failures := 0

func check(ok: bool, what: String) -> void:
	print(("ok   " if ok else "FAIL ") + what)
	if not ok:
		failures += 1

func _initialize() -> void:
	var n = LuauFeatures.new()
	check(n.get_script().resource_path == "res://features/derived.luau", "ClassName.new()")
	check(LuauFeatures.make(4) == 12, "ClassName.static_function()")
	check(LuauFeatures.GREETING == "hi", "ClassName.CONSTANT")
	check(n is LuauFeatures, "is ClassName")
	var typed: LuauFeatures = n
	check(typed.describe() == "base+derived", "a variable typed with the class")
	n.free()
	print("failures: ", failures)
	quit(failures)
