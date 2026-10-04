-- Inherits base_helper; overrides overridden() and calls the base version
local Base = require("res://cases/base.luau")
local Derived = { extends = Base }

function Derived:overridden(x) return Base.overridden(self, x) + 1 end

return Derived
