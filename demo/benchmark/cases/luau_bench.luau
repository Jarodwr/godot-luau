-- Luau twin of gdscript_bench.gd for the cases godot-luau supports. Same
-- method names; bench_* methods return a checksum compared with GDScript's.
local Bench = { extends = "Node2D" }

local function fib(n)
	if n < 2 then return n end
	return fib(n - 1) + fib(n - 2)
end

local function add(a, b)
	return a + b
end

function Bench:helper(x) return x + 1 end
function Bench:noop() end
function Bench:echo(value) return value end
function Bench:add2(a, b) return a + b end

function Bench:bench_vm_fib(n) return fib(n) end

function Bench:bench_vm_loop_arith(n)
	local s = 0
	for i = 1, n do s = (s + i * i) % 1000003 end
	return s
end

function Bench:bench_vm_function_calls(n)
	local s = 0
	for i = 1, n do s = add(s, i) end
	return s
end

function Bench:bench_api_vector2_math(n)
	local v = Vector2(0, 0)
	local step = Vector2(1, 1)
	for i = 1, n do v = v + step * 0.5 end
	return v.x
end

function Bench:bench_api_vector2_field(n)
	local v = Vector2(3, 4)
	local s = 0.0
	for i = 1, n do s += v.x end
	return s
end

function Bench:bench_api_object_method(n)
	local s = 0
	for i = 1, n do s += self:get_name():length() end
	return s
end

function Bench:bench_api_object_prop_get(n)
	local s = 0.0
	for i = 1, n do s += self.position.x end
	return s
end

function Bench:bench_api_object_prop_set(n)
	local v = Vector2(1, 2)
	for i = 1, n do self.position = v end
	return self.position.x
end

function Bench:bench_api_dynamic_field(n)
	self.counter = 0
	for i = 1, n do self.counter = self.counter + 1 end
	return self.counter
end

function Bench:bench_api_singleton_call(n)
	local s = 0
	for i = 1, n do s += Engine:get_physics_ticks_per_second() end
	return s
end

function Bench:bench_api_self_method(n)
	local s = 0
	for i = 1, n do s += self:helper(i) end
	return s
end

return Bench
