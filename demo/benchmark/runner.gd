extends SceneTree

# Runs every case for GDScript and Fennel and writes the timings as JSON.
#
#   godot --headless --path benchmark --script runner.gd -- [options]
#     --out=PATH       where to write results (default res://results/<runtime>.json)
#     --scale=X        multiply every case's iteration count (default 1.0)
#     --repeats=N      timed repeats per case after one warm-up (default 5)
#     --only=TEXT      run only cases whose name contains TEXT
#     --fennel-only    skip the GDScript side (for profiling the Fennel side;
#                      GDScript columns are 0 and checks "-")
#
# Kinds of case:
#   inner     the loop runs inside the script: obj.bench_<name>(n)
#   array     like inner, but gets a Godot Array of n ints to read
#   boundary  the loop runs here in GDScript and calls into the script object;
#             both languages pay the same harness cost, so the difference is
#             the cost of crossing into the script
#   frame     n nodes with a _process script, timed over a number of frames

signal bench_signal(value: int)

const CASES := [
	# VM: pure language work, no Godot API
	{"name": "vm_fib", "kind": "inner", "n": 22, "group": "vm", "desc": "recursive fib(n): function calls and integer math"},
	{"name": "vm_loop_arith", "kind": "inner", "n": 300000, "group": "vm", "desc": "integer multiply/add/modulo in a loop"},
	{"name": "vm_function_calls", "kind": "inner", "n": 300000, "group": "vm", "desc": "call a local two-argument function"},
	{"name": "vm_table", "kind": "inner", "n": 300000, "group": "vm", "desc": "append ints to a native list, then sum it"},
	{"name": "vm_map", "kind": "inner", "n": 100000, "group": "vm", "desc": "string-keyed native map write + read"},
	{"name": "vm_string", "kind": "inner", "n": 100000, "group": "vm", "desc": "concatenate a string with an int, take its length"},

	# Script → Godot: calling engine API from inside the script
	{"name": "api_vector2_new", "kind": "inner", "n": 100000, "group": "api", "desc": "construct a Vector2"},
	{"name": "api_vector2_math", "kind": "inner", "n": 100000, "group": "api", "desc": "v = v + step * 0.5 (two Variant operators)"},
	{"name": "api_vector2_field", "kind": "inner", "n": 100000, "group": "api", "desc": "read v.x"},
	{"name": "api_vector2_method", "kind": "inner", "n": 100000, "group": "api", "desc": "call v.length() on a builtin type"},
	{"name": "api_utility_fn", "kind": "inner", "n": 100000, "group": "api", "desc": "call the lerp utility function"},
	{"name": "api_object_method", "kind": "inner", "n": 100000, "group": "api", "desc": "call get_name() on self (engine method)"},
	{"name": "api_object_prop_get", "kind": "inner", "n": 100000, "group": "api", "desc": "read self.position.x (engine property)"},
	{"name": "api_object_prop_set", "kind": "inner", "n": 100000, "group": "api", "desc": "assign self.position (engine property)"},
	{"name": "api_export_get", "kind": "inner", "n": 100000, "group": "api", "desc": "read the script's own exported property"},
	{"name": "api_dynamic_field", "kind": "inner", "n": 100000, "group": "api", "desc": "self.counter += 1 (script instance field)"},
	{"name": "api_singleton_call", "kind": "inner", "n": 100000, "group": "api", "desc": "call a method on the Engine singleton"},
	{"name": "api_constant", "kind": "inner", "n": 100000, "group": "api", "desc": "read a class constant"},
	{"name": "api_new_object", "kind": "inner", "n": 30000, "group": "api", "desc": "RefCounted.new() and release it"},
	{"name": "api_array_build", "kind": "inner", "n": 100000, "group": "api", "desc": "append ints to a Godot Array"},
	{"name": "api_array_read", "kind": "array", "n": 100000, "group": "api", "desc": "index a Godot Array passed in"},
	{"name": "api_array_iterate", "kind": "array", "n": 100000, "group": "api", "desc": "iterate a Godot Array passed in"},
	{"name": "api_dict_rw", "kind": "inner", "n": 100000, "group": "api", "desc": "Godot Dictionary write + read, int keys"},
	{"name": "api_string_method", "kind": "inner", "n": 100000, "group": "api", "desc": "\"hello world\".to_upper().length()"},
	{"name": "api_signal_emit", "kind": "inner", "n": 50000, "group": "api", "desc": "emit own signal, connected to own method"},
	{"name": "api_self_method", "kind": "inner", "n": 100000, "group": "api", "desc": "call own exposed method via self (through Godot for Fennel)"},
	{"name": "api_callable_call", "kind": "inner", "n": 100000, "group": "api", "desc": "call a closure wrapped in a Callable"},
	{"name": "api_async_method_call", "kind": "inner", "n": 50000, "group": "api", "desc": "call an async-method that doesn't await (GDScript: plain call)"},

	# Godot → script: the harness loop calls into the script object
	{"name": "call_noop", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "obj.noop(): bare call into the script"},
	{"name": "call_add2", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "obj.add2(i, 1): two ints in, one out"},
	{"name": "echo_int", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "obj.echo(int) round trip"},
	{"name": "echo_float", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "obj.echo(float) round trip"},
	{"name": "echo_string", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "obj.echo(String) round trip"},
	{"name": "echo_vector2", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "obj.echo(Vector2) round trip"},
	{"name": "echo_array16", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "obj.echo(Array of 16) round trip"},
	{"name": "echo_dict16", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "obj.echo(Dictionary of 16) round trip"},
	{"name": "echo_object", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "obj.echo(Node) round trip"},
	{"name": "call_args6", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "obj.args6(...) with six mixed arguments"},
	{"name": "ret_vector2", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "obj.ret_vector2(): script constructs and returns"},
	{"name": "ret_array", "kind": "boundary", "n": 100000, "group": "boundary", "desc": "obj.ret_array(): script constructs and returns"},
	{"name": "prop_get_export", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "read obj.speed (exported property)"},
	{"name": "prop_set_export", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "write obj.speed (exported property)"},
	{"name": "callable_from_script", "kind": "boundary", "n": 200000, "group": "boundary", "desc": "call a Callable the script returned"},
	{"name": "signal_into_script", "kind": "boundary", "n": 100000, "group": "boundary", "desc": "emit a GDScript signal connected to a script method"},
	{"name": "new_instance", "kind": "boundary", "n": 20000, "group": "boundary", "desc": "script.new() + free() of a Node2D script"},

	# Per frame
	{"name": "process_nodes", "kind": "frame", "n": 20000, "group": "frame", "desc": "n nodes moving in _process; ns per node per frame"},
]

# process_nodes uses 20000 nodes: with 2000, frames are short enough that the
# CPU slows down between them, and results varied by 3x between runs.
const FRAME_COUNT := 60

# Cases godot-luau implements (cases/luau_bench.luau)
const SUPPORTED := ["vm_fib", "vm_loop_arith", "vm_function_calls", "api_vector2_math", "api_vector2_field",
	"api_object_method", "api_object_prop_get", "api_object_prop_set", "api_dynamic_field", "api_singleton_call",
	"api_self_method", "call_noop", "call_add2", "echo_int", "echo_float", "echo_vector2", "process_nodes"]

var scale := 1.0
var repeats := 5
var only := ""
var fennel_only := false
var mover_path := "res://cases/mover.luau"
var out_path := ""

var gd_obj: Node
var fnl_obj: Node


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--scale="):
			scale = float(arg.get_slice("=", 1))
		elif arg.begins_with("--repeats="):
			repeats = int(arg.get_slice("=", 1))
		elif arg.begins_with("--only="):
			only = arg.get_slice("=", 1)
		elif arg.begins_with("--mover="):
			mover_path = arg.get_slice("=", 1)
		elif arg == "--fennel-only":
			fennel_only = true
		elif arg.begins_with("--out="):
			out_path = arg.get_slice("=", 1)
	if out_path.is_empty():
		out_path = "res://results/luau.json"
	run()


func run() -> void:
	gd_obj = load("res://cases/gdscript_bench.gd").new()
	fnl_obj = load("res://cases/luau_bench.luau").new()
	# Names of equal length: api_object_method sums get_name().length()
	gd_obj.name = "BenchGD"
	fnl_obj.name = "BenchFN"
	root.add_child(gd_obj)
	root.add_child(fnl_obj)
	await process_frame

	var runtime := "luau"
	print("Benchmark: GDScript vs Fennel (%s runtime), scale %s, %d repeats" % [runtime, scale, repeats])
	print("%-24s %14s %14s %8s  %s" % ["case", "GDScript ns", "Fennel ns", "ratio", "check"])

	var results := {}
	for case in CASES:
		if not case.name in SUPPORTED:
			continue
		if not only.is_empty() and not case.name.contains(only):
			continue
		var n: int = maxi(1, int(case.n * scale)) if case.name != "vm_fib" else int(case.n)
		var entry: Dictionary
		if case.kind == "frame":
			entry = await run_frame_case(case, n)
		else:
			entry = run_case(case, n)
		results[case.name] = entry
		print("%-24s %14.1f %14.1f %8.2f  %s" % [case.name, entry.gdscript_ns, entry.fennel_ns, entry.fennel_ns / maxf(entry.gdscript_ns, 0.001), entry.check])

	var report := {
		"runtime": runtime,
		"godot_version": Engine.get_version_info().string,
		"date": Time.get_datetime_string_from_system(true),
		"scale": scale,
		"repeats": repeats,
		"cases": results,
	}
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(out_path.get_base_dir()))
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	f.store_string(JSON.stringify(report, "\t"))
	f.close()
	print("Wrote ", ProjectSettings.globalize_path(out_path))
	quit()


func run_case(case: Dictionary, n: int) -> Dictionary:
	var gd := {"median_usec": 0.0, "min_usec": 0.0, "result": null} if fennel_only else time_case(case, gd_obj, n)
	var fnl := time_case(case, fnl_obj, n)
	var ops := ops_for(case, n)
	return {
		"group": case.group,
		"kind": case.kind,
		"desc": case.desc,
		"n": n,
		"ops": ops,
		"gdscript_ns": gd.median_usec * 1000.0 / ops,
		"fennel_ns": fnl.median_usec * 1000.0 / ops,
		"gdscript_min_ns": gd.min_usec * 1000.0 / ops,
		"fennel_min_ns": fnl.min_usec * 1000.0 / ops,
		"check": "-" if fennel_only else check_results(gd.result, fnl.result),
	}


# Number of operations a case performs, for ns/op. fib(n) makes fib(n+1)*2-1 calls.
func ops_for(case: Dictionary, n: int) -> float:
	if case.name == "vm_fib":
		var a := 0
		var b := 1
		for i in n + 1:
			var t := a + b
			a = b
			b = t
		return float(a * 2 - 1)
	return float(n)


func time_case(case: Dictionary, obj: Object, n: int) -> Dictionary:
	var arg: Variant = n
	if case.kind == "array":
		var arr := []
		for i in range(1, n + 1):
			arr.append(i)
		arg = arr
	var result: Variant = null
	var times: Array[int] = []
	for rep in repeats + 1:
		var t0 := Time.get_ticks_usec()
		if case.kind == "boundary":
			result = call("b_" + case.name, obj, n)
		else:
			result = obj.call("bench_" + case.name, arg)
		var dt := Time.get_ticks_usec() - t0
		if rep > 0:  # first run is warm-up
			times.append(dt)
	times.sort()
	return {"median_usec": float(times[times.size() / 2]), "min_usec": float(times[0]), "result": result}


func check_results(a: Variant, b: Variant) -> String:
	if a == null and b == null:
		return "-"
	if (typeof(a) == TYPE_FLOAT or typeof(a) == TYPE_INT) and (typeof(b) == TYPE_FLOAT or typeof(b) == TYPE_INT):
		return "ok" if is_equal_approx(float(a), float(b)) else "MISMATCH %s vs %s" % [a, b]
	return "ok" if a == b else "MISMATCH %s vs %s" % [a, b]


# --- Boundary cases: this loop is the same for both objects -----------------

func b_call_noop(o: Object, n: int) -> Variant:
	for i in n:
		o.noop()
	return null


func b_call_add2(o: Object, n: int) -> Variant:
	var s := 0
	for i in n:
		s += o.add2(i, 1)
	return s


func b_echo_int(o: Object, n: int) -> Variant:
	var s := 0
	for i in n:
		s += o.echo(i)
	return s


func b_echo_float(o: Object, n: int) -> Variant:
	var s := 0.0
	for i in n:
		s += o.echo(1.5)
	return s


func b_echo_string(o: Object, n: int) -> Variant:
	var s := 0
	for i in n:
		s += o.echo("hello world").length()
	return s


func b_echo_vector2(o: Object, n: int) -> Variant:
	var v := Vector2(1, 2)
	var s := 0.0
	for i in n:
		s += o.echo(v).x
	return s


func b_echo_array16(o: Object, n: int) -> Variant:
	var arr := range(16)
	var s := 0
	for i in n:
		s += o.echo(arr).size()
	return s


func b_echo_dict16(o: Object, n: int) -> Variant:
	var d := {}
	for i in 16:
		d[i] = i
	var s := 0
	for i in n:
		s += o.echo(d).size()
	return s


func b_echo_object(o: Object, n: int) -> Variant:
	var node := gd_obj
	var s := 0
	for i in n:
		if o.echo(node) == node:
			s += 1
	return s


func b_call_args6(o: Object, n: int) -> Variant:
	var v := Vector2(1, 2)
	var arr := [1, 2]
	var s := 0
	for i in n:
		s += o.args6(i, 2.5, "three", v, arr, null)
	return s


func b_ret_vector2(o: Object, n: int) -> Variant:
	var s := 0.0
	for i in n:
		s += o.ret_vector2().y
	return s


func b_ret_array(o: Object, n: int) -> Variant:
	var s := 0
	for i in n:
		s += o.ret_array().size()
	return s


func b_prop_get_export(o: Object, n: int) -> Variant:
	var s := 0.0
	for i in n:
		s += o.speed
	return s


func b_prop_set_export(o: Object, n: int) -> Variant:
	for i in n:
		o.speed = 2.0
	return o.speed


func b_callable_from_script(o: Object, n: int) -> Variant:
	var cb: Callable = o.make_callable()
	var s := 0
	for i in n:
		s += cb.call(i)
	return s


func b_signal_into_script(o: Object, n: int) -> Variant:
	var handler := Callable(o, "on_ticked")
	o.counter = 0
	bench_signal.connect(handler)
	for i in n:
		bench_signal.emit(1)
	bench_signal.disconnect(handler)
	return o.counter


func b_new_instance(o: Object, n: int) -> Variant:
	var script: Script = o.get_script()
	for i in n:
		var node: Node = script.new()
		node.free()
	return n


# --- Frame case --------------------------------------------------------------

func run_frame_case(case: Dictionary, n: int) -> Dictionary:
	var gd_script := load("res://cases/mover.gd")
	var fnl_script := load(mover_path)
	var baselines: Array[float] = []
	var gds: Array[float] = []
	var fnls: Array[float] = []
	var gd_moved := true
	var fnl_moved := true
	for rep in repeats:
		baselines.append((await time_frames(null, n)).usec)
		var gd_run: Dictionary = {"usec": baselines[-1], "moved": true} if fennel_only else await time_frames(gd_script, n)
		var fnl_run: Dictionary = await time_frames(fnl_script, n)
		gds.append(gd_run.usec)
		fnls.append(fnl_run.usec)
		gd_moved = gd_moved and gd_run.moved
		fnl_moved = fnl_moved and fnl_run.moved
	for list in [baselines, gds, fnls]:
		list.sort()
	var baseline: float = baselines[baselines.size() / 2]
	var gd: float = gds[gds.size() / 2]
	var fnl: float = fnls[fnls.size() / 2]
	var per_node := float(FRAME_COUNT * n)
	return {
		"group": case.group,
		"kind": case.kind,
		"desc": case.desc,
		"n": n,
		"ops": per_node,
		"baseline_ns": baseline * 1000.0 / per_node,
		"gdscript_ns": (gd - baseline) * 1000.0 / per_node,
		"fennel_ns": (fnl - baseline) * 1000.0 / per_node,
		"gdscript_min_ns": (gds[0] - baseline) * 1000.0 / per_node,
		"fennel_min_ns": (fnls[0] - baseline) * 1000.0 / per_node,
		"check": "-" if fennel_only else "ok" if gd_moved and fnl_moved else "MISMATCH nodes didn't move (gd %s, fennel %s)" % [gd_moved, fnl_moved],
	}


# Processes first (start) or last (end) in a frame; together they time every
# _process call in between.
class FrameMark extends Node:
	var is_end := false
	var started_usec := 0
	var start_mark: FrameMark
	var total_usec := 0

	func _process(_delta: float) -> void:
		if is_end:
			total_usec += Time.get_ticks_usec() - start_mark.started_usec
		else:
			started_usec = Time.get_ticks_usec()


# {usec: time spent in _process over FRAME_COUNT frames with n nodes,
#  moved: whether _process ran}. Plain Node2Ds when script is null.
func time_frames(script: Script, n: int) -> Dictionary:
	var parent := Node.new()
	var start := FrameMark.new()
	start.process_priority = -1000
	var end := FrameMark.new()
	end.process_priority = 1000
	end.is_end = true
	end.start_mark = start
	parent.add_child(start)
	parent.add_child(end)
	for i in n:
		var node := Node2D.new()
		if script:
			node.set_script(script)
		parent.add_child(node)
	root.add_child(parent)
	for i in 3:
		await process_frame
	end.total_usec = 0
	for i in FRAME_COUNT:
		await process_frame
	var dt := float(end.total_usec)
	var moved: bool = (parent.get_child(2) as Node2D).position.x > 0.0
	parent.free()
	await process_frame
	return {"usec": dt, "moved": moved}
