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

	# Indexed and keyed access (ADR 0020)
	var d := {}
	check(a.dict_write(d) == 5 and d["x"] == 5 and d[1] == "y", "Dictionary writes and reads by key")
	var arr := [1, 2]
	check(a.array_write(arr) == 12 and arr[0] == 10, "Array writes and reads by index")
	check(a.array_oob(arr) == null, "Array index out of range reads as nil")

	# Strings (ADR 0023): ASCII, non-ASCII, empty, longer than the stack buffer
	check(a.echo("Player") == "Player", "ASCII string round trip")
	check(a.echo("héllo ✓ 日本") == "héllo ✓ 日本", "non-ASCII string round trip")
	check(a.echo("") == "", "empty string round trip")
	var long_text := "ab✓".repeat(700)
	check(a.echo(long_text) == long_text, "string longer than the stack buffer")
	check(a.concat("héllo", " ✓") == "héllo ✓", "Lua concatenation of converted strings")
	check(a.lua_length("✓") == 3, "Lua sees UTF-8 bytes")
	check(a.echo(&"sname") == "sname", "StringName argument arrives as a string")

	# API surface (ADRs 0024-0027)
	var api = a.api_checks()
	if not api is Dictionary:
		api = {}
	if api.is_empty():
		check(false, "api_checks ran")
	for key in api:
		check(api[key], "API: " + str(key))

	# Direct UTF-8 string methods give Godot's results
	for sample in ["hello", "  héllo ✓  ", "", "HeLLo World", "ÄÖÜ äöü", "\tlol\n"]:
		var expected := [sample.begins_with("he"), sample.ends_with("lo"), sample.contains("ll"), sample.is_empty(),
			sample.to_upper(), sample.to_lower(), sample.strip_edges(), sample.strip_edges(false, true), sample.replace("l", "L")]
		var got = a.string_ops(sample)
		check(got == expected, "string methods match Godot for %s (got %s)" % [JSON.stringify(sample), got])

	# Packed Vector2i crossing into and out of GDScript
	check(a.echo(Vector2i(9, -9)) == Vector2i(9, -9), "Vector2i round trip through a script")
	check(typeof(a.echo(Vector2i(1, 1))) == TYPE_VECTOR2I, "Vector2i stays a Vector2i")

	# Elementwise operators: the direct C path must give Godot's exact results
	var c1 := Color(0.1, 0.2, 0.3, 0.4)
	var c2 := Color(0.7, 0.11, 0.13, 0.17)
	var v1 := Vector4(1.1, 2.2, 3.3, 4.4)
	var v2 := Vector4(0.3, 0.7, 1.9, 2.3)
	var q1 := Quaternion(0.1, 0.2, 0.3, 0.9)
	var q2 := Quaternion(0.4, 0.3, 0.2, 0.1)
	var i1 := Vector3i(3, -4, 5)
	var i2 := Vector3i(7, 8, -9)
	var j1 := Vector4i(1, 2, 3, 4)
	var j2 := Vector4i(-5, 6, 7, 8)
	var expected_ops := [
		c1 + c2, c1 - c2, c1 * c2, c1 / c2, c1 * 0.3, 0.3 * c1, c1 / 0.3, c1 * 2,
		v1 + v2, v1 - v2, v1 * v2, v1 / v2, v1 * 0.3, 0.3 * v1, v1 / 0.3,
		q1 + q2, q1 - q2, q1 * 0.3, 0.3 * q1, q1 / 0.3, q1 * q2,
		i1 + i2, i1 - i2, i1 * i2, i1 * 3, 3 * i1, i1 / Vector3i(2, 2, 2), i1 * 1.5,
		j1 + j2, j1 - j2, j1 * j2, j1 * -2,
	]
	var got_ops = a.elementwise_ops()
	for k in expected_ops.size():
		var got_value = got_ops[k] if got_ops is Array and k < got_ops.size() else null
		check(typeof(got_value) == typeof(expected_ops[k]) and got_value == expected_ops[k],
			"elementwise op %d: %s (got %s)" % [k, expected_ops[k], got_value])

	a.free()
	print("failures: ", failures)
	quit(failures)
