-- Export sections in a sequence (ADR 0051): headers apply to what follows
return {
	extends = "Node",
	exports = {
		{ name = "title", default = "x" },
		{ category = "Stats" },
		{ name = "health", default = 10 },
		{ group = "Movement" },
		{ name = "speed", default = 1.5 },
		{ subgroup = "Jumping" },
		{ name = "jump", default = 4.0 },
		{ group = "" },
		{ name = "after", default = true },
	},
}
