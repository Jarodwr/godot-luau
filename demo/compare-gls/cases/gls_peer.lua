-- godot-luau-script twin of peer.gd: another node the bench calls into
--- @class
--- @extends Node
local Peer = {}
local PeerC = gdclass(Peer)

export type Peer = Node & typeof(Peer)

--- @registerMethod
function Peer:helper(x: number): number
	return x + 1
end

return PeerC
