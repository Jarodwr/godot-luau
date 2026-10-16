-- Another node the Luau bench calls into (same-language peer)
local Peer = { extends = gd.Node }
function Peer:helper(x) return x + 1 end
return Peer
