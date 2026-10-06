--!strict
-- A script checked by luau-lsp with godot.d.luau (docs/adr/0048):
-- `self` is typed as the base class, the script's table and its fields.
local Util = require("@res/types/util")

local Mover = { extends = "Node2D", exports = { speed = 100.0 } }
type Mover = Node2D & typeof(Mover) & { velocity: Vector2, speed: number }

function Mover._ready(self: Mover)
	self.velocity = Vector2(1, 0.5)
	local label = Label.new()
	label.text = Util.greeting("mover")
	self:add_child(label)
	self:get_tree().process_frame:connect(function() end)
end

function Mover._process(self: Mover, delta: number)
	self.position += self.velocity * self.speed * delta
	self.rotation = lerp_angle(self.rotation, 0, 0.1)
	local tint: Color = Color(1, 0, 0):lerp(Color.BLUE, 0.5)
	self.modulate = tint
	local cell: Vector2i = Vector2i(2, 3) + Vector2i.ONE
	self.position = Vector2(cell.x, cell.y) + Vector2.from_angle(math.pi) * 2
end

function Mover.wait_a_bit(self: Mover)
	await(self:get_tree():create_timer(1.0).timeout)
	print(Engine:get_frames_drawn(), Input:is_key_pressed(KEY_SPACE))
end

return Mover
