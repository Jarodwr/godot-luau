-- Extends a Luau class by its class_name (ADR 0049). Calling the base's
-- version of a method takes its table, through require.
local Features = require("@res/features/derived")

local ByName = {
	extends = "LuauFeatures",
	class_name = "LuauByName",
}

function ByName:describe()
	return "by name+" .. Features.describe(self)
end

return ByName
