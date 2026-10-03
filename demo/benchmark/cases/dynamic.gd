extends Node
# Answers a property that doesn't exist through _get

func _get(property: StringName) -> Variant:
	if property == &"virtual_value":
		return 7
	return null
