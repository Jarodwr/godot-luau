-- Per-frame work for the process_nodes case; twin of mover.gd.
local Mover = { extends = "Node2D" }

function Mover:_ready()
	self.velocity = Vector2(1, 0.5)
end

function Mover:_process(delta)
	self.position = self.position + self.velocity * delta
end

return Mover
