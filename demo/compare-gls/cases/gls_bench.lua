-- godot-luau-script twin of gdscript_bench.gd for the godot-luau comparison.
-- Same method names; bench_* methods return a checksum compared with GDScript.
-- Every method is registered so GDScript can call it.
--- @class
--- @extends Node2D
--- @permissions INTERNAL
local Bench = {}
local BenchC = gdclass(Bench)

export type Bench = Node2D & typeof(Bench) & {
	--- @property
	--- @default 2.0
	speed: number,

	--- @signal
	ticked: SignalWithArgs<(value: number) -> ()>,

	--- @signal
	idle: SignalWithArgs<() -> ()>,

	--- @property
	--- @set set_armor
	--- @get get_armor
	armor: number,

	--- @property
	--- @default 0
	notified: integer,

	counter: number,
	_armor: number,
}

--- @registerConstant
Bench.MAX_HEALTH = 100

local function fib(n)
	if n < 2 then return n end
	return fib(n - 1) + fib(n - 2)
end

local function add(a, b)
	return a + b
end


--- @registerMethod
function Bench:_Ready()
	self.counter = 0
	self.ticked:Connect(Callable.new(self, "on_ticked"))
end

--- @registerMethod
function Bench:on_ticked(value)
	self.counter = (self.counter or 0) + value
end

--- @registerMethod
function Bench:helper(x) return x + 1 end
--- @registerMethod
function Bench:noop() end
--- @registerMethod
function Bench:echo(value) return value end
--- @registerMethod
function Bench:add2(a, b) return a + b end
--- @registerMethod
function Bench:args6(a, b, c, d, e, f) return a end
--- @registerMethod
function Bench:ret_vector2() return Vector2.new(1, 2) end
--- @registerMethod
function Bench:ret_array()
	local a = Array.new()
	a:Append(1); a:Append(2); a:Append(3)
	return a
end
-- GLS only allows Callable.new(object, method) (no Lua-function Callables)
--- @registerMethod
function Bench:make_callable() return Callable.new(self, "helper") end
--- @registerMethod
function Bench:async_noop() return nil end

--- @registerMethod
function Bench:bench_vm_fib(n) return fib(n) end
--- @registerMethod
function Bench:bench_vm_loop_arith(n)
	local s = 0
	for i = 1, n do s = (s + i * i) % 1000003 end
	return s
end
--- @registerMethod
function Bench:bench_vm_function_calls(n)
	local s = 0
	for i = 1, n do s = add(s, i) end
	return s
end
--- @registerMethod
function Bench:bench_vm_table(n)
	local t = {}
	for i = 1, n do t[i] = i * 2 end
	local s = 0
	for _, v in ipairs(t) do s += v end
	return s
end
--- @registerMethod
function Bench:bench_vm_map(n)
	local d = {}
	for i = 1, n do d["k" .. (i % 1000)] = i end
	local s = 0
	for i = 1, n do s += d["k" .. (i % 1000)] end
	return s
end
--- @registerMethod
function Bench:bench_vm_string(n)
	local total = 0
	for i = 1, n do total += #("item" .. i) end
	return total
end

--- @registerMethod
function Bench:bench_api_vector2_new(n)
	local s = 0
	for i = 1, n do
		local v = Vector2.new(i, 1)
		s += 1
	end
	return s
end
--- @registerMethod
function Bench:bench_api_vector2_math(n)
	local v = Vector2.new(0, 0)
	local step = Vector2.new(1, 1)
	for i = 1, n do v = v + step * 0.5 end
	return v.x
end
--- @registerMethod
function Bench:bench_api_vector2_field(n)
	local v = Vector2.new(3, 4)
	local s = 0.0
	for i = 1, n do s += v.x end
	return s
end
--- @registerMethod
function Bench:bench_api_vector2_method(n)
	local v = Vector2.new(3, 4)
	local s = 0.0
	for i = 1, n do s += v:Length() end
	return s
end
--- @registerMethod
function Bench:bench_api_utility_fn(n)
	local s = 0.0
	for i = 1, n do s += lerp(0.0, 10.0, 0.5) end
	return s
end
--- @registerMethod
function Bench:bench_api_object_method(n)
	local s = 0
	for i = 1, n do s += utf8.len(self:GetName()) end
	return s
end
--- @registerMethod
function Bench:bench_api_object_prop_get(n)
	local s = 0.0
	for i = 1, n do s += self.position.x end
	return s
end
--- @registerMethod
function Bench:bench_api_object_prop_set(n)
	local v = Vector2.new(1, 2)
	for i = 1, n do self.position = v end
	return self.position.x
end
--- @registerMethod
function Bench:bench_api_export_get(n)
	local s = 0.0
	for i = 1, n do s += self.speed end
	return s
end
--- @registerMethod
function Bench:bench_api_dynamic_field(n)
	self.counter = 0
	for i = 1, n do self.counter = self.counter + 1 end
	return self.counter
end
--- @registerMethod
function Bench:bench_api_singleton_call(n)
	local s = 0
	local engine = Engine.singleton
	for i = 1, n do s += engine:GetPhysicsTicksPerSecond() end
	return s
end
--- @registerMethod
function Bench:bench_api_constant(n)
	local s = 0
	for i = 1, n do s += Node.NOTIFICATION_READY end
	return s
end
--- @registerMethod
function Bench:bench_api_new_object(n)
	local s = 0
	for i = 1, n do
		local o = RefCounted.new()
		s += 1
	end
	return s
end
--- @registerMethod
function Bench:bench_api_array_build(n)
	local a = Array.new()
	for i = 1, n do a:Append(i) end
	return a:Size()
end
--- @registerMethod
function Bench:bench_api_array_read(arr)
	local s = 0
	for i = 0, arr:Size() - 1 do s += arr:Get(i) end
	return s
end
--- @registerMethod
function Bench:bench_api_array_iterate(arr)
	local s = 0
	for _, v in arr do s += v end
	return s
end
--- @registerMethod
function Bench:bench_api_dict_rw(n)
	local d = Dictionary.new()
	for i = 1, n do d:Set(i % 100, i) end
	local s = 0
	for i = 1, n do s += d:Get(i % 100) end
	return s
end
-- GLS doesn't bind Godot's String methods; Lua's string library instead
--- @registerMethod
function Bench:bench_api_string_method(n)
	local s = 0
	for i = 1, n do s += #string.upper("hello world") end
	return s
end
--- @registerMethod
function Bench:bench_api_signal_emit(n)
	self.counter = 0
	for i = 1, n do self.ticked:Emit(1) end
	return self.counter
end
--- @registerMethod
function Bench:bench_api_self_method(n)
	local s = 0
	for i = 1, n do s += self:helper(i) end
	return s
end
-- A method Callable (GLS has no Lua-function Callables)
--- @registerMethod
function Bench:bench_api_callable_call(n)
	local cb = Callable.new(self, "helper")
	local s = 0
	for i = 1, n do s += cb:Call(i) end
	return s
end
--- @registerMethod
function Bench:bench_api_async_method_call(n)
	local s = 0
	for i = 1, n do
		self:async_noop()
		s += 1
	end
	return s
end

--- @registerMethod
function Bench:bench_api_get_node(n)
	local s = 0
	for i = 1, n do
		if self:GetNode("Child") then s += 1 end
	end
	return s
end
--- @registerMethod
function Bench:bench_api_object_return(n)
	local s = 0
	for i = 1, n do
		if self:GetParent() then s += 1 end
	end
	return s
end
--- @registerMethod
function Bench:bench_api_other_prop_get(n)
	local c = self:GetNode("Child")
	local s = 0.0
	for i = 1, n do s += c.position.x end
	return s
end
--- @registerMethod
function Bench:bench_api_other_method(n)
	local c = self:GetNode("Child")
	local s = 0
	for i = 1, n do
		if c:IsVisible() then s += 1 end
	end
	return s
end
--- @registerMethod
function Bench:bench_api_peer_call(n)
	local p = self:GetNode("Peer")
	local s = 0
	for i = 1, n do s += p:helper(i) end
	return s
end
--- @registerMethod
function Bench:bench_api_gdscript_call(n)
	local p = self:GetNode("GDHelper")
	local s = 0
	for i = 1, n do s += p:Call("helper", i) end
	return s
end
--- @registerMethod
function Bench:bench_api_string_arg(n)
	local s = 0
	for i = 1, n do
		if self:HasMethod("helper") then s += 1 end
	end
	return s
end
--- @registerMethod
function Bench:bench_api_node_create(n)
	local s = 0
	for i = 1, n do
		local node = Node.new()
		node:Free()
		s += 1
	end
	return s
end
--- @registerMethod
function Bench:bench_api_color_math(n)
	local c = Color.new(0, 0, 0)
	local step = Color.new(0.001, 0.002, 0.003)
	for i = 1, n do c = c + step * 0.5 end
	return math.round(c.r * 1000) / 1000
end
--- @registerMethod
function Bench:bench_api_transform_xform(n)
	local t = Transform2D.new(0.5, Vector2.new(10, 20))
	local s = 0.0
	for i = 1, n do s += (t * Vector2.new(1, 0)).x end
	return math.round(s * 1000) / 1000
end


-- API surface and script features
--- @registerMethod
--- @defaultArgs ["world"]
function Bench:greet(who: string): number
	return utf8.len(who)
end

--- @registerMethod
function Bench:typed_add(a: integer, b: number): number
	return a + b
end

--- @registerMethod
function Bench:on_idle()
end

--- @registerMethod
function Bench:set_armor(value: number)
	self._armor = value * 2
end

--- @registerMethod
function Bench:get_armor(): number
	return self._armor or 0
end

--- @registerMethod
function Bench:_Notification(what: integer)
	if what == 12345 then self.notified += 1 end
end

--- @registerMethod
function Bench:bench_api_vector2_methods(n)
	local v = Vector2.new(3, 4)
	local w = Vector2.new(1, 2)
	local s = 0.0
	for i = 1, n do s += v:Normalized():Dot(w) + v:DistanceTo(w) end
	return math.round(s * 1000) / 1000
end

--- @registerMethod
function Bench:bench_api_vector3_math(n)
	local v = Vector3.new(0, 0, 0)
	local step = Vector3.new(1, 2, 3)
	for i = 1, n do v = v + step * 0.5 end
	return v.x
end

--- @registerMethod
function Bench:bench_api_vector2i_math(n)
	local a = Vector2i.new(0, 0)
	for i = 1, n do a = a + Vector2i.new(1, 2) end
	return a.y
end

--- @registerMethod
function Bench:bench_api_rect_has_point(n)
	local r = Rect2.new(0, 0, 10, 10)
	local p = Vector2.new(5, 5)
	local s = 0
	for i = 1, n do
		if r:HasPoint(p) then s += 1 end
	end
	return s
end

--- @registerMethod
function Bench:bench_api_builtin_static(n)
	local s = 0.0
	for i = 1, n do s += Vector2.FromAngle(0.5).x end
	return math.round(s * 1000) / 1000
end

--- @registerMethod
function Bench:bench_api_utility_mix(n)
	local s = 0.0
	for i = 1, n do s += clampf(i % 20, 0, 10) + deg_to_rad(90) end
	return math.round(s * 1000) / 1000
end

--- @registerMethod
function Bench:bench_api_global_enum(n)
	local s = 0
	for i = 1, n do s += Enum.Key.SPACE end
	return s
end

--- @registerMethod
function Bench:bench_api_dict_iterate(n)
	local d = Dictionary.new()
	for i = 1, 100 do d:Set(i, i) end
	local s = 0
	for r = 1, n // 100 do
		for _, v in d do s += v end
	end
	return s
end

--- @registerMethod
function Bench:bench_api_packed_float_array(n)
	local a = PackedFloat32Array.new()
	for i = 1, n do a:Append(i * 0.5) end
	return a:Size()
end

-- GLS doesn't bind Godot's String methods: Lua's string library instead
--- @registerMethod
function Bench:bench_api_string_godot_method(n)
	local s = 0
	for i = 1, n do
		if string.sub("hello world", 1, 5) == "hello" then s += 1 end
	end
	return s
end

--- @registerMethod
function Bench:bench_api_transform3d_xform(n)
	local t = Transform3D.new(Basis.new(Vector3.new(0, 1, 0), 0.5), Vector3.new(1, 2, 3))
	local s = 0.0
	for i = 1, n do s += (t * Vector3.new(1, 0, 0)).x end
	return math.round(s * 1000) / 1000
end

--- @registerMethod
function Bench:bench_api_export_set(n)
	local last = 0.0
	for i = 1, n do
		self.speed = i * 0.5
		last = self.speed
	end
	self.speed = 2.0
	return last
end

--- @registerMethod
function Bench:bench_api_property_accessor(n)
	local s = 0
	for i = 1, n do
		self.armor = i
		s += self.armor
	end
	return s
end

--- @registerMethod
function Bench:bench_api_signal_emit_unconnected(n)
	for i = 1, n do self.idle:Emit() end
	return n
end

--- @registerMethod
function Bench:bench_api_signal_connect(n)
	local cb = Callable.new(self, "on_idle")
	for i = 1, n do
		self.idle:Connect(cb)
		self.idle:Disconnect(cb)
	end
	return n
end

--- @registerMethod
function Bench:bench_api_inherited_call(n)
	local d = self:GetNode("Derived")
	local s = 0
	for i = 1, n do s += d:base_helper(i) end
	return s
end

--- @registerMethod
function Bench:bench_api_super_call(n)
	local d = self:GetNode("Derived")
	local s = 0
	for i = 1, n do s += d:overridden(i) end
	return s
end

--- @registerMethod
function Bench:bench_api_get_override(n)
	local dyn = self:GetNode("Dynamic")
	local s = 0
	for i = 1, n do s += dyn:Get("virtual_value") end
	return s
end

--- @registerMethod
function Bench:bench_api_script_constant(n)
	local s = 0
	for i = 1, n do s += Bench.MAX_HEALTH end
	return s
end

return BenchC
