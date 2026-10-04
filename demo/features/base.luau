-- Base of the feature checks: inherited properties, signals, methods,
-- constants and overridable methods
local Base = {
	extends = "Node",
	exports = {
		health = { type = "int", default = 10, range = { 0, 100 } },
	},
	signals = { "died" },
}

Base.MAX_HEALTH = 100

function Base:describe() return "base" end
function Base:hurt(amount)
	self.health -= amount
	if self.health <= 0 then self.died:emit() end
	return self.health
end

return Base
