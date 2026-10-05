@tool
extends Node
# Reads the Luau nodes' properties in the editor: the tool node's real value,
# the plain node's placeholder value
func _ready() -> void:
	if Engine.is_editor_hint():
		print("PROBE tool.size=", get_node("../Tool").get("size"), " plain.speed=", get_node("../Plain").get("speed"))
