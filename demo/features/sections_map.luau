-- Export sections in a map (ADR 0051): each property names its group
return {
	extends = gd.Node,
	exports = {
		zeta = { default = 1 },
		speed = { default = 1.5, group = "Movement" },
		accel = { default = 2.0, group = "Movement" },
		color = { default = gd.Color(1, 0, 0), group = "Look" },
		alpha = { default = 0 },
	},
}
