-- godot-luau-script twin of dynamic.gd
--- @class
--- @extends Node
local GlsDynamic = {}
local GlsDynamicC = gdclass(GlsDynamic)

export type GlsDynamic = Node & typeof(GlsDynamic)

function GlsDynamic:_Get(property: string): Variant
	if property == "virtual_value" then return 7 end
	return nil
end

return GlsDynamicC
