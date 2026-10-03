-- Another node the Luau bench calls into (same-language peer)
local Peer = { extends = "Node" }
function Peer:helper(x) return x + 1 end
return Peer
