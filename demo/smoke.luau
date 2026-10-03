local Smoke = { extends = "Node2D" }

function Smoke:_ready()
	self.velocity = Vector2(1, 0.5)
	print("ready", self:get_name(), self.position, self.velocity)
end

function Smoke:check()
	self.position = Vector2(3, 4)
	local p = self.position
	local frames = Engine:get_physics_ticks_per_second()
	local child = Node.new(); child:set_name("Kid"); self:add_child(child); frames += #child:get_name()
	return p.x + p.y + frames + #self:get_name()
end

return Smoke
