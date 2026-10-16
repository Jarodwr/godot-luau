--!strict
-- A script checked by luau-lsp with godot.d.luau (docs/adr/0048):
-- `self` is typed as the base class, the script's table and its fields.
local Util = require("@res/types/util")

local Mover = { extends = gd.Node2D, exports = { speed = { default = 100.0 }, target = { type = gd.Node2D }, count = { type = gd.int } } }
type Mover = Node2D & typeof(Mover) & { velocity: Vector2, speed: number }

function Mover._ready(self: Mover)
	self.velocity = gd.Vector2(1, 0.5)
	local label = gd.Label.new()
	label.text = Util.greeting("mover")
	self:add_child(label)
	self:get_tree().process_frame:connect(function() end)
end

function Mover._process(self: Mover, delta: number)
	self.position += self.velocity * self.speed * delta
	self.rotation = gd.lerp_angle(self.rotation, 0, 0.1)
	local tint: Color = gd.Color(1, 0, 0):lerp(gd.Color.BLUE, 0.5)
	self.modulate = tint
	local cell: Vector2i = gd.Vector2i(2, 3) + gd.Vector2i.ONE
	self.position = gd.Vector2(cell.x, cell.y) + gd.Vector2.from_angle(math.pi) * 2
end

function Mover.wait_a_bit(self: Mover)
	await(self:get_tree():create_timer(1.0).timeout)
	print(gd.Engine:get_frames_drawn(), gd.Input:is_key_pressed(gd.KEY_SPACE))
end

return Mover
