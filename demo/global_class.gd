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

	# A Luau class extending another by its class_name (ADR 0049)
	var b = LuauByName.new()
	check(b is LuauFeatures and b is LuauByName, "extends = \"LuauFeatures\": is both classes")
	check(b.describe() == "by name+base+derived", "an override calls the named base's method (%s)" % b.describe())
	check(b.speed == 2.5 and LuauByName.make(2) == 6, "inherits the named base's exports and statics")
	# Reloading the named base (in memory, the file stays) updates the derived class
	var base: Script = load("res://features/derived.luau")
	var original: String = base.source_code
	base.source_code = original.replace('function Features:describe() return Base.describe(self) .. "+derived" end',
			'function Features:describe() return "reloaded" end').replace("function Features.make(n) return n * 3 end",
			"function Features.make(n) return n * 5 end")
	check(base.reload(true) == OK and b.describe() == "by name+reloaded", "reloading the named base updates the derived script (%s)" % b.describe())
	check(LuauByName.make(2) == 10, "and its inherited methods (%s)" % LuauByName.make(2))
	base.source_code = original
	base.reload(true)
	b.free()
	print("failures: ", failures)
	quit(failures)
