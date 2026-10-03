-- Answers a property that doesn't exist through _get
-- (todo/object-overrides.md: not implemented yet)
local Dynamic = { extends = "Node" }

function Dynamic:_get(property)
	if property == "virtual_value" then return 7 end
	return nil
end

return Dynamic
