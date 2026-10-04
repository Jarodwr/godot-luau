-- Feature checks: everything godot-luau's script declarations support
local Base = require("res://features/base.luau")

local Features = {
	extends = Base,
	class_name = "LuauFeatures",
	exports = {
		speed = 2.5,                                   -- type from the default
		label = "String",                              -- type name only
		target = { type = "Node" },                    -- object type, node hint
	},
	properties = {
		armor = { type = "int", default = 1, set = "set_armor", get = "get_armor" },
		readonly = { type = "int", get = "get_readonly" },
		ratio = { type = "float", default = 0 },       -- int default coerced
	},
	signals = {
		scored = { "points: int", "by" },
	},
	defaults = {
		greet = { "world", "!" },
	},
	types = {
		typed_add = { args = { "int", "float" }, ret = "float" },
	},
}

Features.GREETING = "hi"

function Features:set_armor(value) self._armor = value * 2 end
function Features:get_armor() return self._armor or 0 end
function Features:get_readonly() return 42 end

-- Override and call the base version
function Features:describe() return Base.describe(self) .. "+derived" end

function Features:greet(name, punct) return "hello " .. name .. punct end
function Features:typed_add(a, b) return a + b end

-- Overrides
function Features:_get(property)
	if property == "virtual" then return 7 end
	return nil
end
function Features:_set(property, value)
	if property == "virtual" then self.virtual_written = value; return true end
	return false
end
function Features:_notification(what)
	if what == 12345 then self.notified = what end
end

-- Lua functions as Callables
function Features:emit_scored(points, by) self.scored:emit(points, by) end
function Features:make_adder(n) return function(x) return x + n end end
function Features:take_callable(f) return f(5) end
function Features:connect_lua()
	self.lua_hits = 0
	local function on_scored(points) self.lua_hits += points end
	self.on_scored = on_scored
	self.scored:connect(on_scored)
	-- The same function makes an equal Callable
	return self.scored:is_connected(on_scored)
end
function Features:disconnect_lua()
	self.scored:disconnect(self.on_scored)
	return self.scored:is_connected(self.on_scored)
end
function Features:get_lua_hits() return self.lua_hits end
function Features:get_notified() return self.notified end
function Features:get_virtual_written() return self.virtual_written end
function Features:constant() return self.GREETING .. " " .. tostring(self.MAX_HEALTH) end

return Features
