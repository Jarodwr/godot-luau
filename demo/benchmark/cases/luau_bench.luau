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
function Bench:on_ticked(value) self.counter = (self.counter or 0) + value end
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

-- Called from the runner's loop
function Bench:args6(a, b, c, d, e, f) return a end
function Bench:ret_vector2() return Vector2(1, 2) end
function Bench:ret_array() return Array({ 1, 2, 3 }) end
function Bench:make_callable() return Callable(function(x) return x + 1 end) end
function Bench:async_noop() return nil end

-- VM
function Bench:bench_vm_table(n)
	local t = {}
	for i = 1, n do t[i] = i * 2 end
	local s = 0
	for _, v in ipairs(t) do s += v end
	return s
end

function Bench:bench_vm_map(n)
	local d = {}
	for i = 1, n do d["k" .. (i % 1000)] = i end
	local s = 0
	for i = 1, n do s += d["k" .. (i % 1000)] end
	return s
end

function Bench:bench_vm_string(n)
	local total = 0
	for i = 1, n do total += #("item" .. i) end
	return total
end

-- Script → Godot, cases shared with fennel-gdextension
function Bench:bench_api_vector2_new(n)
	local s = 0
	for i = 1, n do
		local v = Vector2(i, 1)
		s += 1
	end
	return s
end

function Bench:bench_api_vector2_method(n)
	local v = Vector2(3, 4)
	local s = 0.0
	for i = 1, n do s += v:length() end
	return s
end

function Bench:bench_api_utility_fn(n)
	local s = 0.0
	for i = 1, n do s += lerp(0.0, 10.0, 0.5) end
	return s
end

function Bench:bench_api_export_get(n)
	local s = 0.0
	for i = 1, n do s += self.speed end
	return s
end

function Bench:bench_api_constant(n)
	local s = 0
	for i = 1, n do s += Node.NOTIFICATION_READY end
	return s
end

function Bench:bench_api_new_object(n)
	local s = 0
	for i = 1, n do
		local o = RefCounted.new()
		s += 1
	end
	return s
end

function Bench:bench_api_array_build(n)
	local a = Array()
	for i = 1, n do a:append(i) end
	return a:size()
end

function Bench:bench_api_array_read(arr)
	local s = 0
	for i = 0, arr:size() - 1 do s += arr[i] end
	return s
end

function Bench:bench_api_array_iterate(arr)
	local s = 0
	for _, v in arr do s += v end
	return s
end

function Bench:bench_api_dict_rw(n)
	local d = Dictionary()
	for i = 1, n do d[i % 100] = i end
	local s = 0
	for i = 1, n do s += d[i % 100] end
	return s
end

function Bench:bench_api_string_method(n)
	local s = 0
	for i = 1, n do s += ("hello world"):to_upper():length() end
	return s
end

function Bench:bench_api_signal_emit(n)
	self.counter = 0
	for i = 1, n do self.ticked:emit(1) end
	return self.counter
end

function Bench:bench_api_callable_call(n)
	local cb = Callable(function(x) return x + 1 end)
	local s = 0
	for i = 1, n do s += cb(i) end
	return s
end

function Bench:bench_api_async_method_call(n)
	local s = 0
	for i = 1, n do
		self:async_noop()
		s += 1
	end
	return s
end

-- Edge cases (godot-luau)
function Bench:bench_api_get_node(n)
	local s = 0
	for i = 1, n do
		if self:get_node("Child") then s += 1 end
	end
	return s
end

function Bench:bench_api_object_return(n)
	local s = 0
	for i = 1, n do
		if self:get_parent() then s += 1 end
	end
	return s
end

function Bench:bench_api_other_prop_get(n)
	local c = self:get_node("Child")
	local s = 0.0
	for i = 1, n do s += c.position.x end
	return s
end

function Bench:bench_api_other_method(n)
	local c = self:get_node("Child")
	local s = 0
	for i = 1, n do
		if c:is_visible() then s += 1 end
	end
	return s
end

function Bench:bench_api_peer_call(n)
	local p = self:get_node("Peer")
	local s = 0
	for i = 1, n do s += p:helper(i) end
	return s
end

function Bench:bench_api_gdscript_call(n)
	local p = self:get_node("GDHelper")
	local s = 0
	for i = 1, n do s += p:helper(i) end
	return s
end

function Bench:bench_api_string_arg(n)
	local s = 0
	for i = 1, n do
		if self:has_method("helper") then s += 1 end
	end
	return s
end

function Bench:bench_api_node_create(n)
	local s = 0
	for i = 1, n do
		local node = Node.new()
		node:free()
		s += 1
	end
	return s
end

function Bench:bench_api_color_math(n)
	local c = Color(0, 0, 0)
	local step = Color(0.001, 0.002, 0.003)
	for i = 1, n do c = c + step * 0.5 end
	return math.round(c.r * 1000) / 1000
end

function Bench:bench_api_transform_xform(n)
	local t = Transform2D(0.5, Vector2(10, 20))
	local s = 0.0
	for i = 1, n do s += (t * Vector2(1, 0)).x end
	return math.round(s * 1000) / 1000
end

function Bench:bench_api_array_table_in(n)
	local s = 0
	for i = 1, n do s += Array({ 1, 2, 3 }):size() end
	return s
end

return Bench
