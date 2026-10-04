local Thrower = {
	extends = "Node",
	signals = { "go" },
	properties = { broken = { type = "int", get = "get_broken" } },
}

local function deepest(t)
	return t.missing.field
end

local function middle(t)
	return deepest(t) + 1
end

function Thrower:nested()
	return middle({})
end

function Thrower:level3(t) return t.missing.field end
function Thrower:level2(t) return self:level3(t) + 1 end
function Thrower:chain() return self:level2({}) end

function Thrower:get_broken()
	return nil + 1
end

function Thrower:bad_engine_call()
	self:add_child("not a node")
end

function Thrower:after_await()
	await(self.go)
	error("failed after await")
end

function Thrower:handler_error()
	self.go:connect(function() local x = nil; return x.y end)
end

return Thrower
