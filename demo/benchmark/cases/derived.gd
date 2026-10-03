extends "res://cases/base.gd"
# Inherits base_helper; overrides overridden() and calls the base version

func overridden(x: int) -> int:
	return super(x) + 1
