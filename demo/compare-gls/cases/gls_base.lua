-- godot-luau-script twin of base.gd
--- @class GlsBase
--- @extends Node
local GlsBase = {}
local GlsBaseC = gdclass(GlsBase)

export type GlsBase = Node & typeof(GlsBase)

--- @registerMethod
function GlsBase:base_helper(x: number): number
	return x + 1
end

--- @registerMethod
function GlsBase:overridden(x: number): number
	return x * 2
end

return GlsBaseC
