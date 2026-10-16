-- Not a tool script: an inspector placeholder in the editor
local PlainNode = { extends = gd.Node, exports = { speed = { default = 7.5 } } }
function PlainNode:_ready() print("LUAU PLAIN READY (must not run in the editor)") end
return PlainNode
