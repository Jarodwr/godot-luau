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
		awaiting = { type = "int", get = "get_awaiting" },
		ratio = { type = "float", default = 0 },       -- int default coerced
	},
	signals = {
		scored = { "points: int", "by" },
		go = {},
	},
	defaults = {
		greet = { "world", "!" },
	},
	types = {
		typed_add = { args = { "int", "float" }, ret = "float" },
		scale = { args = { "float" }, ret = "float" },
	},
	static = { "make", "scale" },
}

Features.GREETING = "hi"

-- Static functions (ADR 0039): no self
function Features.make(n) return n * 3 end
function Features.scale(x) return x * 1.5 end

-- More overrides
function Features:_get_property_list()
	return { { name = "dynamic", type = 2, usage = 6 } }  -- TYPE_INT, PROPERTY_USAGE_DEFAULT
end
function Features:_validate_property(property)
	if property.name == "speed" then
		property.hint = 1  -- PROPERTY_HINT_RANGE
		property.hint_string = "0,10"
	end
end
function Features:_to_string() return "Features<" .. self.GREETING .. ">" end
function Features:_property_can_revert(name) return name == "dynamic" end
function Features:_property_get_revert(name)
	if name == "dynamic" then return 5 end
	return nil
end

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

-- await (docs/adr/0035)
function Features:wait_scored()
	self.log = {}
	table.insert(self.log, "before")
	local points, by = await(self.scored)
	table.insert(self.log, by .. points)
	return "unused"
end
function Features:wait_loop(times)
	self.loops = 0
	for i = 1, times do
		await(self.go)
		self.loops += 1
	end
end
local function helper(self)
	await(self.go)  -- suspends the whole method that called it
	self.helped = true
end
function Features:wait_in_helper() helper(self) end
function Features:connect_waiting_closure()
	self.closure_done = false
	self.scored:connect(function()
		await(self.go)
		self.closure_done = true
	end, 4)  -- CONNECT_ONE_SHOT
end
function Features:wait_on(signal) await(signal); self.after = true end
function Features:wait_on_result(signal) await(signal) end
function Features:wait_timer()
	await(self:get_tree():create_timer(0.02).timeout)
	self.timer_done = true
end
function Features:wait_gdscript(tree) self.gd_result = await(tree:gd_wait()) end
function Features:spawn_two()
	self.spawned = 0
	local function waiter() await(self.go); self.spawned += 1 end
	spawn(waiter)
	spawn(waiter)
	self.spawned += 10  -- runs before either waiter resumes
end
-- Leak checks
function Features:thread_stats() return __luau_thread_stats() end
function Features:yield_wrongly() coroutine.yield() end
function Features:burst(n)
	for i = 1, n do
		spawn(function() await(self.go) end)
	end
end
function Features:churn(n)
	for i = 1, n do
		spawn(function()
			local t = { i, tostring(i) }  -- something for the stack to hold
			await(self.go)
			return t
		end)
		self.go:emit()
	end
end
function Features:wait_forever(signal) await(signal); self.never = true end
function Features:wait_then_return(n) await(self.go); return n * 2 end
function Features:return_at_once(n) return n + 1 end
function Features:wait_then_fail() await(self.go); error("failed while awaited") end
function Features:await_plain() return await(5) end
function Features:get_awaiting() return await(self.go) end
function Features:state()
	return { kind = "state", log = self.log and table.concat(self.log, ","), loops = self.loops,
		helped = self.helped, closure_done = self.closure_done,
		timer_done = self.timer_done, spawned = self.spawned, gd_result = self.gd_result }
end

return Features
