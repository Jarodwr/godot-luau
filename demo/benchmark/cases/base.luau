-- Base script for the inheritance cases
local Base = { extends = "Node" }

function Base:base_helper(x) return x + 1 end
function Base:overridden(x) return x * 2 end

return Base
