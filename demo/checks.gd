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

	# Vector2 and Vector3 as separate Luau types (ADR 0044)
	var vk = a.vector_kind_checks()
	check(typeof(a.kind_mat.get_shader_parameter("dir")) == TYPE_VECTOR3, "Vector3.UP into a vec3 shader parameter stays a Vector3")
	check(typeof(a.get_meta("v3")) == TYPE_VECTOR3 and typeof(a.get_meta("v2")) == TYPE_VECTOR2, "metadata keeps Vector2 and Vector3")
	check(typeof(vk.roundtrip[0]) == TYPE_VECTOR3 and vk.roundtrip[0] == Vector3(4, 5, 0), "a Vector3 with z = 0 survives a round trip")
	check(typeof(vk.v2_math) == TYPE_VECTOR2 and vk.v2_math == Vector2(2.5, 4), "Vector2 arithmetic stays Vector2")
	check(typeof(vk.v3_math) == TYPE_VECTOR3 and vk.v3_math == Vector3(2, 4, 0), "Vector3 arithmetic stays Vector3")
	check(vk.mixed_eq and vk.keys, "Vector2 and Vector3 differ in == and as table keys")
	check(typeof(vk.cross2) in [TYPE_INT, TYPE_FLOAT] and vk.cross2 == 1, "Vector2.cross is a number (whole numbers arrive as int, ADR 0043)")
	check(typeof(vk.cross3) == TYPE_VECTOR3 and vk.cross3 == Vector3(0, 0, 1), "Vector3.cross is a vector")
	check(typeof(vk.rotated2) == TYPE_VECTOR2 and vk.rotated2.is_equal_approx(Vector2(0, 1)), "Vector2.rotated(angle)")
	check(typeof(vk.rotated3) == TYPE_VECTOR3 and vk.rotated3.is_equal_approx(Vector3(0, 0, -1)), "Vector3.rotated(axis, angle)")
	var xf := Transform3D(Basis(Vector3.UP, 0.5), Vector3(1, 2, 3)) * Vector3(1, 0, 0)
	check(typeof(vk.xform) == TYPE_VECTOR3 and vk.xform.is_equal_approx(xf), "Transform3D * Vector3 with z = 0, no retry")
	check(vk.basis_ok, "Basis(Vector3.UP, angle)")
	check(typeof(vk.normalized2) == TYPE_VECTOR2 and vk.normalized2.is_equal_approx(Vector2(0.6, 0.8)), "Vector2 methods return Vector2")
	check(vk.get("v2_z") == null, "a Vector2 has no z")

	# Editor validation (ADR 0047): errors with line and column, functions for
	# the members panel, for Luau and Fennel
	var lang: Object = null
	for i in Engine.get_script_language_count():
		if Engine.get_script_language(i).get_class() == "LuauLanguage":
			lang = Engine.get_script_language(i)
	var v_ok = lang.validate_script("local T = {}\nfunction T:_ready()\nend\nT.f = function() end\nreturn T\n", "res://v.luau")
	check(v_ok.valid and v_ok.errors.is_empty() and v_ok.functions == PackedStringArray(["_ready:2", "f:4"]), "a valid script validates, with its functions (%s)" % v_ok)
	var v_syntax = lang.validate_script("local T = {}\nfunction T:f()\n  local x = = 1\nend\nlocal y = (\nreturn T\n", "res://v.luau")
	check(not v_syntax.valid and v_syntax.errors.size() == 2 and v_syntax.errors[0].line == 3 and v_syntax.errors[0].column == 13 and v_syntax.errors[1].line == 6, "every syntax error, with line and column (%s)" % [v_syntax.errors])
	var many := "local T = {}\nfunction T:f()\n"
	for i in 210:
		many += "  local a%d = tostring(%d)\n" % [i, i]
	many += "end\nreturn T\n"
	var v_compile = lang.validate_script(many, "res://v.luau")
	check(not v_compile.valid and v_compile.errors.size() == 1 and v_compile.errors[0].line > 2, "errors only the compiler finds (too many locals) (%s)" % [v_compile.errors])
	var v_fnl = lang.validate_script("(local M {})\n(fn M._ready [self] nil)\n\n(fn M.go [self] 1)\nM\n", "res://v.fnl")
	check(v_fnl.valid and v_fnl.functions == PackedStringArray(["_ready:2", "go:4"]), "Fennel validates, with functions at Fennel lines (%s)" % v_fnl)
	var v_fnl_parse = lang.validate_script("(local M {})\n(fn M.f [self]\n  (print \"x\"\nM\n", "res://v.fnl")
	check(not v_fnl_parse.valid and "Parse error" in v_fnl_parse.errors[0].message, "Fennel parse errors (%s)" % [v_fnl_parse.errors])
	var v_fnl_compile = lang.validate_script("(local M {})\n(fn M.f [self]\n  (let [x] x))\nM\n", "res://v.fnl")
	check(not v_fnl_compile.valid and v_fnl_compile.errors[0].line == 3 and v_fnl_compile.errors[0].column == 8, "Fennel compile errors, with line and column (%s)" % [v_fnl_compile.errors])

	# Shared tables are frozen (ADR 0046)
	var fz = a.frozen_checks()
	check(fz.blocked == fz.writes, "writes to libraries, type tables and shared metatables fail (%d of %d)" % [fz.blocked, fz.writes])
	check(fz.own_global and fz.package_loaded, "scripts' own globals and package.loaded stay writable")
	check(fz.cached, "lookups that cache into frozen tables still work, twice")

	# Strict vector dimensions (ADR 0045)
	var sv = a.strict_vector_checks()
	check(sv.ctor2_3 != "no error" and sv.ctor3_2 != "no error", "Vector2 with three numbers and Vector3 with two are errors (%s / %s)" % [sv.ctor2_3, sv.ctor3_2])
	check(sv.ctor2_call != "no error" and sv.ctor2_varargs != "no error", "Vector2(1, f()) and Vector2(1, ...) giving three numbers are errors (%s / %s)" % [sv.ctor2_call, sv.ctor2_varargs])
	check(typeof(sv.ctor2_empty) == TYPE_VECTOR2 and sv.ctor2_from_i == Vector2(1, 2) and typeof(sv.ctor2_from_i) == TYPE_VECTOR2, "Vector2() and Vector2(Vector2i)")
	check(sv.ctor3_from_i == Vector3(1, 2, 3) and typeof(sv.ctor3_from_i) == TYPE_VECTOR3, "Vector3(Vector3i)")
	check("Vector2 where a Vector3 is expected" in sv.prop3, "a Vector2 into a Vector3 property is an error (%s)" % sv.prop3)
	check("Vector3 where a Vector2 is expected" in sv.prop2, "a Vector3 into a Vector2 property is an error (%s)" % sv.prop2)
	check("Vector2 where a Vector3 is expected" in sv.arg3 and sv.arg2_ok == "no error", "engine arguments check dimensions (%s)" % sv.arg3)
	check("should be Vector2i" in sv.builtin_arg and sv.builtin_arg_ok == true, "builtin method arguments check dimensions (Godot's check); a Vector2 still converts to Vector2i (%s)" % sv.builtin_arg)

	# Opaque 64-bit ints (ADR 0043)
	var big = a.int64_checks()
	var id1: int = big.refs[0].get_instance_id()
	var id2: int = big.refs[1].get_instance_id()
	for key in ["opaque", "roundtrip", "equal", "key", "order", "arith_error", "mixed_compare_error",
			"small_is_number", "rng_state", "packed_array", "dictionary"]:
		check(big.get(key) == true, "64-bit ints: " + key)
	check(big.less == (id1 < id2), "64-bit ints order as Godot's ints do")
	check(big.text == str(id1) and big.concat == "id " + str(id1), "64-bit ints print all their digits (%s)" % big.text)
	check(big.format == "%s|   ab|(1, 2)|7%%" % str(id1), "string.format's %%s takes any value (%s)" % big.format)
	check(a.get_meta("big_id") == id1, "a 64-bit int stored from Lua is exact in Godot")

	# Table conversions: arrays sized and written in place (ADR 0041)
	var tables = a.tables()
	var expected_tables := [
		[1, 2.5, "s", true, Vector2(1, 2), Vector3(1, 2, 3)],
		[[1, 2], {"a": 1}],
		[10, 20, 30, 40, 50],
		{1: 1, 2: 2, 4: 4},
		[],
		{1: 1, 2: 2, "x": 3},
		["héllo ✓", {"nested": [1, [2]]}],
	]
	for k in expected_tables.size():
		var got_table = tables[k] if tables is Array and k < tables.size() else null
		check(typeof(got_table) == typeof(expected_tables[k]) and got_table == expected_tables[k],
			"table conversion %d: %s (got %s)" % [k, expected_tables[k], got_table])
	check(typeof(tables[0][0]) == TYPE_INT and typeof(tables[0][1]) == TYPE_FLOAT and typeof(tables[0][4]) == TYPE_VECTOR2,
		"converted element types")

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

	feature_checks()
	await frame_checks()

	a.free()
	print("failures: ", failures)
	quit(failures)


# Script declarations, inheritance, overrides and Lua Callables (ADRs 0032-0034)
func feature_checks() -> void:
	var script: Script = load("res://features/derived.luau")
	var f = Node.new()
	f.set_script(script)
	root.add_child(f)

	# Exports and properties
	var props := {}
	for p in f.get_property_list():
		props[p.name] = p
	check(props.has("speed") and props.speed.type == TYPE_FLOAT and props.speed.usage & PROPERTY_USAGE_EDITOR, "export inferred from a default is a float in the editor")
	check(props.has("label") and props.label.type == TYPE_STRING, "export declared by type name")
	check(props.has("target") and props.target.hint == PROPERTY_HINT_NODE_TYPE, "Node export gets a node hint")
	check(props.has("health") and props.health.hint == PROPERTY_HINT_RANGE, "inherited export with a range")
	check(props.has("armor") and not (props.armor.usage & PROPERTY_USAGE_EDITOR), "plain property isn't shown in the editor")
	check(f.speed == 2.5 and f.label == "" and f.target == null and f.health == 10, "defaults and zero values")
	f.speed = 4
	check(typeof(f.speed) == TYPE_FLOAT and f.speed == 4.0, "typed property coerces int to float")
	check(typeof(f.ratio) == TYPE_FLOAT, "int default of a float property is a float")
	f.armor = 3
	check(f.armor == 6, "setter and getter")
	check(f.readonly == 42, "getter-only property")
	check(script.get_property_default_value("speed") == 2.5, "script reports property defaults")

	# Constants
	var constants := script.get_script_constant_map()
	check(constants.get("GREETING") == "hi" and constants.get("MAX_HEALTH") == 100, "constants, own and inherited")
	check(f.constant() == "hi 100", "constants through self")

	# Static functions and constants on the script (ADR 0039)
	var luau_script = script  # untyped: GDScript checks Script's own methods otherwise
	check(luau_script.make(4) == 12, "static function called on the script")
	check(typeof(luau_script.scale(2)) == TYPE_FLOAT and luau_script.scale(2) == 3.0, "typed static function")
	check(f.make(2) == 6, "static function called through an instance")
	check(luau_script.GREETING == "hi" and luau_script.MAX_HEALTH == 100, "constants read on the script, own and inherited")
	check(luau_script.has_method("make") and script.resource_path.ends_with("derived.luau"), "the script's own methods and properties still work")

	# More overrides
	props = {}
	for p in f.get_property_list():
		props[p.name] = p
	check(props.has("dynamic") and props.dynamic.type == TYPE_INT, "_get_property_list adds properties")
	check(props.speed.hint == PROPERTY_HINT_RANGE and props.speed.hint_string == "0,10", "_validate_property changes a property's info")
	check(str(f) == "Features<hi>", "_to_string (got %s)" % str(f))
	check(f.property_can_revert("dynamic") and f.property_get_revert("dynamic") == 5, "_property_can_revert and _property_get_revert")

	# Inheritance
	check(script.get_base_script() != null and script.get_base_script().resource_path == "res://features/base.luau", "base script")
	check(f.hurt(3) == 7, "inherited method using an inherited property")
	check(f.describe() == "base+derived", "override calling the base version")
	check(f.has_method("hurt") and f.has_method("describe"), "has_method sees own and inherited methods")
	check(script.get_global_name() == &"LuauFeatures", "class_name")

	# Default arguments and typed members
	check(f.greet() == "hello world!", "both arguments defaulted")
	check(f.greet("you") == "hello you!", "rightmost argument defaulted")
	check(f.greet("you", "?") == "hello you?", "no argument defaulted")
	var sum = f.typed_add(2.9, 1)
	check(typeof(sum) == TYPE_FLOAT and sum == 3.0, "typed arguments and return are coerced (got %s)" % sum)

	# Overrides
	check(f.get("virtual") == 7, "_get answers an unknown property")
	f.set("virtual", 9)
	check(f.get_virtual_written() == 9, "_set receives an unknown property")
	f.notification(12345)
	check(f.get_notified() == 12345, "_notification")

	# Signals
	check(f.has_signal("scored") and f.has_signal("died"), "own and inherited signals")
	var got := []
	f.scored.connect(func(points, by): got.append([points, by]))
	f.emit_scored(5, "me")
	check(got == [[5, "me"]], "signal declared in Luau reaches GDScript")
	var died := [false]
	f.died.connect(func(): died[0] = true)
	f.hurt(100)
	check(died[0], "inherited signal emitted by an inherited method")

	# Lua functions as Callables
	var adder = f.make_adder(10)
	check(adder is Callable and adder.call(5) == 15, "Lua closure called from GDScript")
	check(f.take_callable(func(x): return x * 3) == 15, "GDScript Callable called from Lua")
	check(f.take_callable(adder) == 15, "Lua Callable comes back as the Lua function")
	check(f.connect_lua(), "Lua function connected, equal Callable found")
	f.emit_scored(4, "lua")
	check(f.get_lua_hits() == 4, "Lua function receives the signal")
	check(not f.disconnect_lua(), "Lua function disconnected")
	f.emit_scored(4, "lua")
	check(f.get_lua_hits() == 4, "disconnected function no longer called")

	# await (ADR 0035)
	check(f.wait_scored() is Signal, "a method that awaits returns a Signal to its caller")
	check(f.state().get("log") == "before", "the method stopped at await")
	f.scored.emit(3, "x")
	check(f.state().get("log") == "before,x3", "resumed with the signal's arguments")
	f.wait_loop(3)
	for i in 3:
		f.go.emit()
	check(f.state().get("loops") == 3, "await in a loop, resumed three times")
	f.wait_in_helper()
	check(f.state().get("helped") == null, "await in a helper suspends the method")
	f.go.emit()
	check(f.state().get("helped") == true, "helper resumed")
	f.connect_waiting_closure()
	f.scored.emit(1, "y")
	check(f.state().get("closure_done") == false, "signal handler closure waiting")
	f.go.emit()
	check(f.state().get("closure_done") == true, "signal handler closure resumed")
	check(f.await_plain() == 5, "await of a plain value returns it at once")
	check(f.get("awaiting") == null, "await in a getter is an error (reads null)")
	f.wait_loop(2)
	f.go.emit()
	f.go.emit()
	f.go.emit()
	check(f.state().get("loops") == 2, "a finished coroutine isn't resumed again")

	f.spawn_two()
	check(f.state().get("spawned") == 10, "spawn returns at the first await")
	f.go.emit()
	check(f.state().get("spawned") == 12, "both spawned functions resumed")

	# No leaks: threads go back to the pool, resumers are freed, memory returns
	var base = f.thread_stats()
	check(base.waiting == 0 and base.resumers == 0, "nothing waiting before the leak checks (got %s)" % base)
	f.churn(10000)
	var after_churn = f.thread_stats()
	check(after_churn.threads == base.threads and after_churn.waiting == 0 and after_churn.resumers == 0,
		"10,000 await cycles reuse the same threads (before %s, after %s)" % [base, after_churn])
	f.churn(10000)
	var after_second = f.thread_stats()
	check(after_second.kb - after_churn.kb <= 4, "another 10,000 await cycles leave no Lua memory behind (%d KB -> %d KB -> %d KB)" % [base.kb, after_churn.kb, after_second.kb])
	f.burst(1000)
	var during_burst = f.thread_stats()
	check(during_burst.waiting == 1000 and during_burst.resumers == 1000, "1,000 coroutines waiting at once (got %s)" % during_burst)
	f.go.emit()
	var after_burst = f.thread_stats()
	check(after_burst.waiting == 0 and after_burst.resumers == 0 and after_burst.idle <= 32 and after_burst.threads <= base.threads + 32,
		"after a burst, idle threads are capped at 32 (got %s)" % after_burst)
	f.yield_wrongly()
	var after_yield = f.thread_stats()
	check(after_yield.threads == after_burst.threads and after_yield.idle == after_burst.idle,
		"coroutine.yield in a method is an error and doesn't strand the thread (got %s)" % after_yield)
	var keeper := Node.new()
	keeper.add_user_signal("rare")
	var w = Node.new()
	w.set_script(script)
	root.add_child(w)
	w.wait_forever(Signal(keeper, "rare"))
	check(w.thread_stats().waiting == 1, "a method waits on a long-lived signal")
	w.free()
	var after_free = f.thread_stats()
	check(after_free.waiting == 0 and after_free.idle == after_yield.idle,
		"freeing the object drops its waiting coroutine at once (got %s)" % after_free)
	Signal(keeper, "rare").emit()
	check(f.thread_stats().resumers == 0, "the stale resumer does nothing and is freed when it fires")
	keeper.free()

	# A coroutine of a freed object doesn't continue
	var g = Node.new()
	g.set_script(script)
	root.add_child(g)
	g.wait_on(f.go)
	g.free()
	f.go.emit()
	check(true, "emitting after the waiting object was freed doesn't crash")
	check(f.wait_on_result(f.go) is Signal and f.go.get_connections().size() == 1, "waiting on another object's signal")
	f.go.emit()
	check(f.go.get_connections().size() == 0, "one-shot connection removed after resuming")

	f.free()


# GDScript coroutine awaited from Luau
func gd_wait() -> int:
	await process_frame
	return 7


# await across frames: a timer, and a GDScript coroutine (ADR 0035)
func frame_checks() -> void:
	await process_frame  # the root enters the tree after _initialize
	var t = Node.new()
	t.set_script(load("res://features/derived.luau"))
	root.add_child(t)
	t.wait_timer()
	t.wait_gdscript(self)
	check(t.state().get("timer_done") == null, "timer not fired yet")
	await create_timer(0.1).timeout
	check(t.state().get("timer_done") == true, "await on a SceneTreeTimer")
	check(t.state().get("gd_result") == 7, "await on a GDScript coroutine gives its result")

	# GDScript awaiting Luau methods (ADR 0038)
	t.go.emit.call_deferred()
	var doubled = await t.wait_then_return(21)
	check(doubled == 42, "GDScript awaits a Luau method and gets its result (got %s)" % doubled)
	check(await t.return_at_once(1) == 2, "awaiting a method that doesn't suspend returns at once")
	var pending: Signal = t.wait_then_return(1)
	var helper = pending.get_object()
	t.go.emit()
	check(not is_instance_valid(helper), "the completion object is freed once emitted")
	pending = t.wait_then_fail()
	helper = pending.get_object()
	t.go.emit()
	check(not is_instance_valid(helper), "a coroutine that fails frees its completion object without emitting")
	var doomed = Node.new()
	doomed.set_script(t.get_script())
	root.add_child(doomed)
	pending = doomed.wait_then_return(1)
	helper = pending.get_object()
	doomed.free()
	check(not is_instance_valid(helper), "freeing the object frees its waiting call's completion object")
	check(t.thread_stats().waiting == 0, "nothing left waiting")
	# This code runs inside t.go's deferred emission (it resumed from an await
	# that emission finished): free t once that emission is over
	await process_frame
	t.free()
