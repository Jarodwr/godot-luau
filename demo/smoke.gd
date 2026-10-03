extends SceneTree

func _initialize() -> void:
	var node = Node2D.new()
	node.name = "Smokey"
	node.set_script(load("res://smoke.luau"))
	root.add_child(node)
	await process_frame
	print("check = ", node.check())
	node.free()
	quit()
