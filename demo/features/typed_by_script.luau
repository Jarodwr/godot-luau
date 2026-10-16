-- Types are gd values or required scripts (ADR 0059); checks.gd reads the
-- property list
local Features = require("res://features/derived.luau")

return {
	extends = gd.Node,
	exports = {
		boss = { type = Features },          -- a script with a class_name: typed by it
		texture = { type = gd.Texture2D },   -- a type only
		count = { type = gd.int },
		title = { default = "Node" },        -- a string is a default, not a type
	},
	signals = { hit = { "damage", gd.int, "by", "where", gd.Vector2 } },
}
