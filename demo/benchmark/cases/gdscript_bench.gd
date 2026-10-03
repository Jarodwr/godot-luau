extends Node2D

# GDScript side of the benchmark. Every method has a twin in fennel_bench.fnl
# doing the same work; bench_* methods run `n` iterations and return a checksum
# that runner.gd compares between the two.

signal ticked(value: int)

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
		s += lerp(0.0, 10.0, 0.5)
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


func bench_api_async_method_call(n: int) -> int:
	var s := 0
	for i in range(1, n + 1):
		async_noop()
		s += 1
	return s
