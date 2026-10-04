extends Node2D

# GDScript side of the benchmark. Every method has a twin in fennel_bench.fnl
# doing the same work; bench_* methods run `n` iterations and return a checksum
# that runner.gd compares between the two.

signal ticked(value: int)
signal idle

const MAX_HEALTH := 100

var notified := 0
var _armor := 0
var armor: int:
	set(value):
		_armor = value * 2  # visible effect: a bypassed setter fails the check
	get:
		return _armor

@export var speed: float = 2.0

var counter: int = 0


func _ready() -> void:
	ticked.connect(on_ticked)


func on_ticked(value: int) -> void:
	counter += value


func fib(n: int) -> int:
	return n if n < 2 else fib(n - 1) + fib(n - 2)


func add(a: int, b: int) -> int:
	return a + b


func helper(x: int) -> int:
	return x + 1


# --- Called from the runner's loop (Godot → script boundary) ---------------

func noop() -> void:
	pass


func echo(value: Variant) -> Variant:
	return value


func add2(a: int, b: int) -> int:
	return a + b


func args6(a: Variant, b: Variant, c: Variant, d: Variant, e: Variant, f: Variant) -> Variant:
	return a


func ret_vector2() -> Vector2:
	return Vector2(1, 2)


func ret_array() -> Array:
	return [1, 2, 3]


func make_callable() -> Callable:
	return func(x: int) -> int: return x + 1


# GDScript has no separate async method kind; a function only becomes a
# coroutine when it awaits. This is the plain-call baseline for async_noop.
func async_noop() -> Variant:
	return null


# --- VM: no Godot API involved ---------------------------------------------

func bench_vm_fib(n: int) -> int:
	return fib(n)


func bench_vm_loop_arith(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		s = (s + i * i) % 1000003
	return s


func bench_vm_function_calls(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		s = add(s, i)
	return s


func bench_vm_table(n: int) -> int:
	var a: Array[int] = []
	for i in range(1, n + 1):
		a.append(i * 2)
	var s := 0
	for v in a:
		s += v
	return s


func bench_vm_map(n: int) -> int:
	var d := {}
	for i in range(1, n + 1):
		d["k" + str(i % 1000)] = i
	var s := 0
	for i in range(1, n + 1):
		s += d["k" + str(i % 1000)]
	return s


func bench_vm_string(n: int) -> int:
	var total := 0
	for i in range(1, n + 1):
		total += ("item" + str(i)).length()
	return total


# --- Script → Godot API ----------------------------------------------------

func bench_api_vector2_new(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		var v := Vector2(i, 1)
		s += 1
	return s


func bench_api_vector2_math(n: int) -> float:
	var v := Vector2(0, 0)
	var step := Vector2(1, 1)
	for i in range(1, n + 1):
		v = v + step * 0.5
	return v.x


func bench_api_vector2_field(n: int) -> float:
	var v := Vector2(3, 4)
	var s := 0.0
	for i in range(1, n + 1):
		s += v.x
	return s


func bench_api_vector2_method(n: int) -> float:
	var v := Vector2(3, 4)
	var s := 0.0
	for i in range(1, n + 1):
		s += v.length()
	return s


func bench_api_utility_fn(n: int) -> float:
	var s := 0.0
	for i in range(1, n + 1):
		# A varying weight: constant arguments let GDScript fold the call away
		s += lerp(0.0, 10.0, (i % 10) * 0.1)
	return s


func bench_api_object_method(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		s += get_name().length()
	return s


func bench_api_object_prop_get(n: int) -> float:
	var s := 0.0
	for i in range(1, n + 1):
		s += position.x
	return s


func bench_api_object_prop_set(n: int) -> float:
	var v := Vector2(1, 2)
	for i in range(1, n + 1):
		position = v
	return position.x


func bench_api_export_get(n: int) -> float:
	var s := 0.0
	for i in range(1, n + 1):
		s += speed
	return s


func bench_api_dynamic_field(n: int) -> int:
	counter = 0
	for i in range(1, n + 1):
		counter += 1
	return counter


func bench_api_singleton_call(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		s += Engine.get_physics_ticks_per_second()
	return s


func bench_api_constant(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		s += Node.NOTIFICATION_READY
	return s


func bench_api_new_object(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		var o := RefCounted.new()
		s += 1
	return s


func bench_api_array_build(n: int) -> int:
	var a := []
	for i in range(1, n + 1):
		a.append(i)
	return a.size()


func bench_api_array_read(arr: Array) -> int:
	var s := 0
	for i in range(arr.size()):
		s += arr[i]
	return s


func bench_api_array_iterate(arr: Array) -> int:
	var s := 0
	for v in arr:
		s += v
	return s


func bench_api_dict_rw(n: int) -> int:
	var d := {}
	for i in range(1, n + 1):
		d[i % 100] = i
	var s := 0
	for i in range(1, n + 1):
		s += d[i % 100]
	return s


func bench_api_string_method(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		s += "hello world".to_upper().length()
	return s


func bench_api_signal_emit(n: int) -> int:
	counter = 0
	for i in range(1, n + 1):
		ticked.emit(1)
	return counter


func bench_api_self_method(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		s += helper(i)
	return s


func bench_api_callable_call(n: int) -> int:
	var cb := func(x: int) -> int: return x + 1
	var s := 0
	for i in range(1, n + 1):
		s += cb.call(i)
	return s


var awaited := 0

func await_once() -> void:
	await idle
	awaited += 1


# Start a coroutine that waits for a signal, then emit it
func bench_api_await_signal(n: int) -> int:
	awaited = 0
	for i in range(n):
		await_once()
		idle.emit()
	return awaited


func bench_api_async_method_call(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		async_noop()
		s += 1
	return s


# --- Edge cases (godot-luau): other nodes, object returns, more types ---------

func bench_api_get_node(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		if get_node("Child"):
			s += 1
	return s


func bench_api_object_return(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		if get_parent():
			s += 1
	return s


func bench_api_other_prop_get(n: int) -> float:
	var c: Node2D = get_node("Child")
	var s := 0.0
	for i in range(1, n + 1):
		s += c.position.x
	return s


func bench_api_other_method(n: int) -> int:
	var c: Node2D = get_node("Child")
	var s := 0
	for i in range(1, n + 1):
		if c.is_visible():
			s += 1
	return s


func bench_api_peer_call(n: int) -> int:
	var p = get_node("Peer")
	var s := 0
	for i in range(1, n + 1):
		s += p.helper(i)
	return s


func bench_api_gdscript_call(n: int) -> int:
	var p = get_node("GDHelper")
	var s := 0
	for i in range(1, n + 1):
		s += p.helper(i)
	return s


func bench_api_string_arg(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		if has_method("helper"):
			s += 1
	return s


func bench_api_node_create(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		var node := Node.new()
		node.free()
		s += 1
	return s


func bench_api_color_math(n: int) -> float:
	var c := Color(0, 0, 0)
	var step := Color(0.001, 0.002, 0.003)
	for i in range(1, n + 1):
		c = c + step * 0.5
	return snappedf(c.r, 0.001)


func bench_api_transform_xform(n: int) -> float:
	var t := Transform2D(0.5, Vector2(10, 20))
	var s := 0.0
	for i in range(1, n + 1):
		s += (t * Vector2(1, 0)).x
	return snappedf(s, 0.001)


func bench_api_array_table_in(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		s += [1, 2, 3].size()
	return s


# --- API surface and script features ----------------------------------------

func greet(who: String = "world") -> int:
	return who.length()


func typed_add(a: int, b: float) -> float:
	return a + b


func on_idle() -> void:
	pass


func _notification(what: int) -> void:
	if what == 12345:
		notified += 1


func bench_api_vector2_methods(n: int) -> float:
	var v := Vector2(3, 4)
	var w := Vector2(1, 2)
	var s := 0.0
	for i in range(1, n + 1):
		s += v.normalized().dot(w) + v.distance_to(w)
	return snappedf(s, 0.001)


func bench_api_vector3_math(n: int) -> float:
	var v := Vector3(0, 0, 0)
	var step := Vector3(1, 2, 3)
	for i in range(1, n + 1):
		v = v + step * 0.5
	return v.x


func bench_api_vector2i_math(n: int) -> int:
	var a := Vector2i(0, 0)
	# The step is built outside the loop: GDScript folds constant constructors
	var step := Vector2i(1, 2)
	for i in range(1, n + 1):
		a = a + step
	return a.y


func bench_api_rect_has_point(n: int) -> int:
	var r := Rect2(0, 0, 10, 10)
	var p := Vector2(5, 5)
	var s := 0
	for i in range(1, n + 1):
		if r.has_point(p):
			s += 1
	return s


func bench_api_builtin_static(n: int) -> float:
	var s := 0.0
	for i in range(1, n + 1):
		s += Vector2.from_angle(0.5).x
	return snappedf(s, 0.001)


func bench_api_utility_mix(n: int) -> float:
	var s := 0.0
	for i in range(1, n + 1):
		s += clampf(float(i % 20), 0.0, 10.0) + deg_to_rad(90.0)
	return snappedf(s, 0.001)


func bench_api_global_enum(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		s += KEY_SPACE
	return s


func bench_api_dict_iterate(n: int) -> int:
	var d := {}
	for i in range(1, 101):
		d[i] = i
	var s := 0
	for r in range(n / 100):
		for k in d:
			s += d[k]
	return s


func bench_api_packed_float_array(n: int) -> int:
	var a := PackedFloat32Array()
	for i in range(1, n + 1):
		a.append(i * 0.5)
	return a.size()


func bench_api_string_godot_method(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		if "hello world".begins_with("hello"):
			s += 1
	return s


func bench_api_transform3d_xform(n: int) -> float:
	var t := Transform3D(Basis(Vector3(0, 1, 0), 0.5), Vector3(1, 2, 3))
	var s := 0.0
	for i in range(1, n + 1):
		s += (t * Vector3(1, 0, 0)).x
	return snappedf(s, 0.001)


func bench_api_export_set(n: int) -> float:
	var last := 0.0
	for i in range(1, n + 1):
		speed = i * 0.5
		last = speed
	speed = 2.0
	return last


func bench_api_property_accessor(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		armor = i
		s += armor
	return s


func bench_api_signal_emit_unconnected(n: int) -> int:
	for i in range(1, n + 1):
		idle.emit()
	return n


func bench_api_signal_connect(n: int) -> int:
	var cb := Callable(self, "on_idle")
	for i in range(1, n + 1):
		idle.connect(cb)
		idle.disconnect(cb)
	return n


func bench_api_inherited_call(n: int) -> int:
	var d = get_node("Derived")
	var s := 0
	for i in range(1, n + 1):
		s += d.base_helper(i)
	return s


func bench_api_super_call(n: int) -> int:
	var d = get_node("Derived")
	var s := 0
	for i in range(1, n + 1):
		s += d.overridden(i)
	return s


func bench_api_get_override(n: int) -> int:
	var dyn = get_node("Dynamic")
	var s := 0
	for i in range(1, n + 1):
		s += dyn.virtual_value
	return s


func bench_api_script_constant(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		s += MAX_HEALTH
	return s
