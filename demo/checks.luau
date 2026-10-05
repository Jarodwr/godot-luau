local Checks = { extends = "Node" }

function Checks:keep(node) self.kept = node end
function Checks:use_kept() return self.kept:get_name() end
function Checks:make_ref() self.ref = RefCounted.new(); return self.ref:get_reference_count() end
function Checks:use_ref() return self.ref:get_reference_count() end
function Checks:frames() return Engine:get_physics_ticks_per_second() end
function Checks:keep_self_of(other) self.other = other end
function Checks:use_other() return self.other.name end

function Checks:dict_write(d) d["x"] = 5; d[1] = "y"; return d["x"] end
function Checks:array_write(a) a[0] = 10; return a[0] + a[1] end
function Checks:array_oob(a) return a[99] end

function Checks:echo(value) return value end

-- Opaque 64-bit ints (ADR 0043)
function Checks:int64_checks()
	local r = RefCounted.new()
	local id = r:get_instance_id()  -- RefCounted ids have bit 63 set: beyond 2^53
	local r2 = RefCounted.new()
	local id2 = r2:get_instance_id()
	local t = {}
	t[id] = "x"
	local rng = RandomNumberGenerator.new()
	rng.seed = 12345
	rng:randi()
	local state = rng.state
	local first = rng:randi()
	rng.state = state
	local arr = PackedInt64Array()
	arr:append(id)
	local d = Dictionary()
	d[id] = id
	self:set_meta("big_id", id)
	return {
		opaque = typeof(id) == "int64",
		roundtrip = instance_from_id(id) == r,
		equal = id == r:get_instance_id() and id ~= id2,
		key = t[r:get_instance_id()] == "x",
		order = ((id < id2) ~= (id2 < id)) and id <= id and not (id < id),
		less = id < id2,
		text = tostring(id),
		concat = "id " .. id,
		arith_error = not pcall(function() return id + 1 end),
		mixed_compare_error = not pcall(function() return id < 5 end),
		small_is_number = type(self:get_instance_id()) == "number",
		rng_state = rng:randi() == first,
		packed_array = arr[0] == id,
		dictionary = d[id] == id,
		refs = { r, r2 },
	}
end

-- Table conversions (ADR 0027, 0041): each shape, compared in checks.gd
function Checks:tables()
	local backwards = {}
	for i = 5, 1, -1 do backwards[i] = i * 10 end  -- a sequence in the hash part
	local holes = { 1, 2 }
	holes[4] = 4                                   -- not a sequence
	return {
		Array({ 1, 2.5, "s", true, Vector2(1, 2), Vector3(1, 2, 3) }),
		Array({ { 1, 2 }, { a = 1 } }),
		Array(backwards),
		Dictionary(holes),
		Array({}),
		Dictionary({ 1, 2, x = 3 }),
		{ "héllo ✓", { nested = { 1, { 2 } } } },
	}
end
function Checks:concat(a, b) return a .. b end
function Checks:lua_length(s) return #s end

-- API surface (ADRs 0024-0027): each returns true or an error string
function Checks:api_checks()
	local results = {}
	local function check(name, ok) results[name] = ok == true end
	local c = Color(1, 0.5, 0)
	check("Color ctor", c.g == 0.5)
	check("Rect2 method", Rect2(0, 0, 10, 10):has_point(Vector2(5, 5)))
	local a = Vector2i(1, 2) + Vector2i(3, 4)
	check("Vector2i maths", a.x == 4 and a.y == 6)
	check("Transform2D xform", math.abs((Transform2D(0, Vector2(10, 20)) * Vector2(1, 0)).x - 11) < 1e-5)
	check("constant", Vector2.ZERO == Vector2(0, 0) and Color.RED == Color(1, 0, 0))
	check("builtin enum", Vector2.AXIS_Y == 1)
	check("static method", math.abs(Vector2.from_angle(0).x - 1) < 1e-6)
	check("method via type", Vector2.dot(Vector2(1, 2), Vector2(3, 4)) == 11)
	local v = Vector2(3, 4)
	check("vector fast methods", v:length() == 5 and math.abs(v:normalized().x - 0.6) < 1e-6 and v:dot(Vector2(1, 0)) == 3)
	check("vector generic 2D", math.abs(Vector2(0, 1):angle() - math.pi / 2) < 1e-6)
	local cr = Vector3(1, 0, 0):cross(Vector3(0, 1, 0))
	check("vector generic 3D", cr.z == 1)
	check("String method on Lua string", ("hello"):to_upper() == "HELLO" and ("hello world"):begins_with("hello"))
	check("Lua string library wins", string.format("%d", 5) == "5")
	check("utility", clampf(15, 0, 10) == 10 and math.abs(deg_to_rad(180) - math.pi) < 1e-9)
	check("vararg utility", max(1, 7, 3) == 7)
	check("global enum", KEY_SPACE == 32)
	check("class constant", Node.NOTIFICATION_READY == 13)
	local arr = Array({ 10, 20, 30 })
	local sum, firsti = 0, nil
	for i, x in arr do sum += x; firsti = firsti or i end
	check("Array from table, iteration, 0-based", sum == 60 and firsti == 0 and #arr == 3)
	local d = Dictionary({ a = 1, b = 2 })
	local dsum = 0
	for k, x in d do dsum += x end
	check("Dictionary from table, iteration", dsum == 3 and #d == 2)
	check("table to engine argument", PackedStringArray({ "x", "y" }):size() == 2)
	check("value types are immutable", not pcall(function() local col = Color(1, 1, 1); col.r = 0 end))
	check("Color.RED unchanged", Color.RED.r == 1)
	-- Cached builtin method pointers (ADR 0029)
	local arr2 = Array()
	arr2:append(5); arr2:append(7)
	check("Array.append mutates the held array", arr2:size() == 2 and arr2[1] == 7)
	local held = { arr = arr2 }
	held.arr:append(9)
	check("same Array through another reference", arr2:size() == 3)
	local d2 = Dictionary()
	d2[1] = "a"
	check("Dictionary methods", d2:has(1) and not d2:has(2) and d2:size() == 1)
	local grown = Rect2(0, 0, 10, 10):grow(1)
	check("method returning an inline type", grown.size.x == 12 and grown.position.x == -1)
	check("Color method", Color(0.5, 0.5, 0.5):lightened(1).r == 1)
	local t2 = Transform2D(0, Vector2(10, 20))
	check("heap-type receiver", t2:basis_xform(Vector2(1, 0)).x == 1 and t2:get_origin().y == 20)
	check("method returning a heap type", t2:translated(Vector2(1, 1)):get_origin().x == 11)
	check("Vector2i method", Vector2i(-3, 4):abs().x == 3)
	local pf = PackedFloat32Array()
	pf:append(1.5); pf:append(2.5)
	local same = pf
	same:append(3.5)
	check("PackedFloat32Array append/size through references", pf:size() == 3 and pf[2] == 3.5 and #pf == 3)
	local ps = PackedStringArray({ "a", "b" })
	check("PackedStringArray methods", ps:has("b") and ps:find("b") == 1)
	-- Direct constructors match the engine's
	check("Color(r, g, b) has alpha 1", Color(0.5, 0.25, 1).a == 1 and Color(0.5, 0.25, 1, 0.5).a == 0.5)
	check("Rect2 from numbers", Rect2(1, 2, 3, 4).size == Vector2(3, 4))
	check("Rect2i/Vector3i/Vector4 from numbers", Rect2i(1, 2, 3, 4).position == Vector2i(1, 2)
		and Vector3i(1, 2, 3).z == 3 and Vector4(1, 2, 3, 4).w == 4)
	check("Vector2i truncates floats", Vector2i(1.9, -1.9) == Vector2i(1, -1))
	check("Quaternion/Plane from numbers", Quaternion(0, 0, 0, 1).w == 1 and Plane(0, 1, 0, 5).d == 5)
	-- Packed values (ADR 0030)
	local vi = Vector2i(3, -4)
	check("Vector2i fields", vi.x == 3 and vi.y == -4)
	check("Vector2i maths", (vi + Vector2i(1, 1)) == Vector2i(4, -3) and (vi * 2).y == -8)
	check("Vector2i maths, more", (vi - Vector2i(1, 1)) == Vector2i(2, -5) and (2 * vi).x == 6
		and (vi * Vector2i(2, 3)) == Vector2i(6, -12) and (Vector2i(7, 9) / Vector2i(2, 2)) == Vector2i(3, 4))
	check("Vector2i * float is a Vector2", math.abs((Vector2i(1, 2) * 0.5).x - 0.5) < 1e-6)
	check("Vector2i equality is by value", Vector2i(1, 2) == Vector2i(1, 2) and Vector2i(1, 2) ~= Vector2i(2, 1))
	local grid = {}
	grid[Vector2i(5, 6)] = "cell"
	check("Vector2i as a table key", grid[Vector2i(5, 6)] == "cell")
	check("Vector2i methods", vi:abs() == Vector2i(3, 4) and vi:length() == 5)
	check("Vector2i constant", Vector2i.ONE == Vector2i(1, 1))
	check("Vector2i ordering", Vector2i(1, 2) < Vector2i(1, 3))
	check("Vector2i through an Array", Array({ Vector2i(7, 8) })[0] == Vector2i(7, 8))
	check("Vector2i into Rect2i", Rect2i(Vector2i(1, 2), Vector2i(3, 4)).size == Vector2i(3, 4))
	check("Vector2i tostring", tostring(Vector2i(1, 2)) == "(1, 2)")
	local rid = RID()
	check("RID", rid:get_id() == 0 and not rid:is_valid() and rid == RID())
	check("method with a default argument, fewer args", Dictionary({ a = 1 }):get("zz") == nil)
	return results
end

function Checks:string_ops(s)
	return { s:begins_with("he"), s:ends_with("lo"), s:contains("ll"), s:is_empty(), s:to_upper(),
		s:to_lower(), s:strip_edges(), s:strip_edges(false, true), s:replace("l", "L") }
end

-- Elementwise operators on builtin values (compared with Godot's in checks.gd)
function Checks:elementwise_ops()
	local c1, c2 = Color(0.1, 0.2, 0.3, 0.4), Color(0.7, 0.11, 0.13, 0.17)
	local v1, v2 = Vector4(1.1, 2.2, 3.3, 4.4), Vector4(0.3, 0.7, 1.9, 2.3)
	local q1, q2 = Quaternion(0.1, 0.2, 0.3, 0.9), Quaternion(0.4, 0.3, 0.2, 0.1)
	local i1, i2 = Vector3i(3, -4, 5), Vector3i(7, 8, -9)
	local j1, j2 = Vector4i(1, 2, 3, 4), Vector4i(-5, 6, 7, 8)
	return {
		c1 + c2, c1 - c2, c1 * c2, c1 / c2, c1 * 0.3, 0.3 * c1, c1 / 0.3, c1 * 2,
		v1 + v2, v1 - v2, v1 * v2, v1 / v2, v1 * 0.3, 0.3 * v1, v1 / 0.3,
		q1 + q2, q1 - q2, q1 * 0.3, 0.3 * q1, q1 / 0.3, q1 * q2,
		i1 + i2, i1 - i2, i1 * i2, i1 * 3, 3 * i1, i1 / Vector3i(2, 2, 2), i1 * 1.5,
		j1 + j2, j1 - j2, j1 * j2, j1 * -2,
	}
end

return Checks
