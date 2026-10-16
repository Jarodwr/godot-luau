-- A function default is called for each instance (ADR 0060); checks.gd
-- changes one instance's values and reads another's
return {
	extends = gd.Node,
	exports = {
		items = { default = function() return { 1 } end },
		tags = { type = gd.Dictionary, default = function() return {} end },  -- {} alone is an Array
		empty = { type = gd.Array },                                          -- a new empty one each
		curve = { type = gd.Curve, default = function() return gd.Curve.new() end },
	},
	properties = { nested = { default = function() return { { 1 } } end } },
}
