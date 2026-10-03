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
	return results
end

return Checks
