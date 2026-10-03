extends SceneTree

func _initialize() -> void:
	var node = Node2D.new()
	node.set_script(load("res://mover.fnl"))
	root.add_child(node)
	for i in 3:
		await process_frame
	print("fennel mover position = ", node.position)
	node.free()
	quit()
