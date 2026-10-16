-- Runs in the editor (tool = true)
local ToolNode = { extends = gd.Node, tool = true, exports = { size = { default = 3 } } }
function ToolNode:_ready()
	print("LUAU TOOL READY editor=" .. tostring(gd.Engine:is_editor_hint()) .. " size=" .. tostring(self.size))
end
return ToolNode
