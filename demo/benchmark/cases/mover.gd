extends Node2D

# Per-frame work for the process_nodes case; twin of mover.fnl.

var velocity := Vector2(1, 0.5)


func _process(delta: float) -> void:
	position += velocity * delta
