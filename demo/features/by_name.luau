-- Extends a Luau class with a class_name through its required table (ADR
-- 0056), which also calls the base's version of a method.
local Features = require("@res/features/derived")

local ByName = {
	extends = Features,
	class_name = "LuauByName",
}

function ByName:describe()
	return "by name+" .. Features.describe(self)
end

return ByName
