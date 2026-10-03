-- godot-luau-script twin of mover.gd (process_nodes)
--- @class
--- @extends Node2D
local Mover = {}
local MoverC = gdclass(Mover)

export type Mover = Node2D & typeof(Mover) & {
	velocity: Vector2,
}

--- @registerMethod
function Mover:_Ready()
	self.velocity = Vector2.new(1, 0.5)
end

--- @registerMethod
function Mover:_Process(delta: number)
	self.position = self.position + self.velocity * delta
end

return MoverC
