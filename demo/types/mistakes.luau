--!strict
-- Each line below has one mistake that luau-lsp must report (tools/check_types.sh)
local node = gd.Node2D.new()
node:add_chld(gd.Node.new())                      -- misspelt method
node.position = "here"                         -- wrong type for a property
node:rotate("a lot")                           -- wrong argument type
local n: number = node:get_child_count() .. ""  -- wrong result type
local t = gd.Tmer.new()                        -- unknown class
local v: Vector2 = gd.Vector2(1, 2) * gd.Color.RED -- no such operator
return nil
