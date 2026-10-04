extends SceneTree
# Prints each kind of script error, for checking how they are reported
# (docs/adr/0037). tools/check_errors.sh runs it and checks the output.

func _initialize() -> void:
	await process_frame
	var t = Node.new()
	t.set_script(load("res://errors/thrower.luau"))
	root.add_child(t)
	print("--- nested")
	t.nested()
	print("--- method chain")
	t.chain()
	print("--- getter")
	t.get("broken")
	print("--- engine call")
	t.bad_engine_call()
	print("--- after await")
	t.after_await()
	t.go.emit()
	print("--- signal handler")
	t.handler_error()
	t.go.emit()
	print("--- syntax")
	load("res://errors/syntax.luau")
	print("--- fennel")
	var f = Node.new()
	f.set_script(load("res://errors/thrower.fnl"))
	root.add_child(f)
	f.boom()
	print("--- end")
	t.free()
	f.free()
	quit()
