-- Export sections in a map (ADR 0051): each property names its group
return {
	extends = "Node",
	exports = {
		zeta = 1,
		speed = { default = 1.5, group = "Movement" },
		accel = { default = 2.0, group = "Movement" },
		color = { default = Color(1, 0, 0), group = "Look" },
		alpha = 0,
	},
}
