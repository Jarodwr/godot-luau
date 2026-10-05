-- Runs in the editor (tool = true)
local ToolNode = { extends = "Node", tool = true, exports = { size = 3 } }
function ToolNode:_ready()
	print("LUAU TOOL READY editor=" .. tostring(Engine:is_editor_hint()) .. " size=" .. tostring(self.size))
end
return ToolNode
