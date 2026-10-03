-- godot-luau-script twin of derived.gd
local GlsBase = require("gls_base")

--- @class
--- @extends GlsBase
local GlsDerived = {}
local GlsDerivedC = gdclass(GlsDerived)

export type GlsDerived = GlsBase.GlsBase & typeof(GlsDerived)

--- @registerMethod
function GlsDerived:overridden(x: number): number
	return GlsBase.overridden(self, x) + 1
end

return GlsDerivedC
