extends SceneTree

var failures := 0

func check(ok: bool, what: String) -> void:
	print(("ok   " if ok else "FAIL ") + what)
	if not ok:
		failures += 1

func _initialize() -> void:
	var script = load("res://checks.luau")
	var a = Node.new()
	a.set_script(script)
	root.add_child(a)

	# A plain node freed while Lua holds it: using it must raise, not crash
	var plain = Node.new()
	plain.name = "Plain"
	a.keep(plain)
	check(a.use_kept() == "Plain", "plain node usable while alive")
	plain.free()
	check(a.use_kept() == null, "freed plain node raises an error (call returns null)")

	# RefCounted held only by Lua stays alive and usable (no check needed)
	check(a.make_ref() >= 1, "RefCounted created from Lua")
	check(a.use_ref() >= 1, "RefCounted still alive through Lua's reference")

	# Singletons
	check(a.frames() == 60, "singleton call")

	# Another scripted node freed while Lua holds its self table
	var b = Node.new()
	b.set_script(script)
	b.name = "Other"
	root.add_child(b)
	a.keep_self_of(b)
	check(a.use_other() == "Other", "other script's self table usable while alive")
	b.free()
	check(a.use_other() == null, "freed scripted node raises an error (call returns null)")

	a.free()
	print("failures: ", failures)
	quit(failures)
