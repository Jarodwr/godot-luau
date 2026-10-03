extends Node
# Base script for the inheritance cases

func base_helper(x: int) -> int:
	return x + 1


func overridden(x: int) -> int:
	return x * 2
