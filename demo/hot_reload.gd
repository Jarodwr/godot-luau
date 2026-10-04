extends SceneTree
# Modules and hot reload (docs/adr/0036). Writes scripts into res://hot_test/,
# changes them, reloads, and deletes the folder at the end.
#
#     godot --headless --path demo --script hot_reload.gd

const DIR := "res://hot_test"

var failures := 0


func check(ok: bool, what: String) -> void:
	print(("ok   " if ok else "FAIL ") + what)
	if not ok:
		failures += 1


func write(path: String, text: String) -> void:
	var file := FileAccess.open(DIR.path_join(path), FileAccess.WRITE)
	file.store_string(text)
	file.close()


func reload(path: String) -> Error:
	var script: Script = load(DIR.path_join(path))
	script.source_code = FileAccess.get_file_as_string(DIR.path_join(path))
	return script.reload(true)


func util_source(value: int) -> String:
	return """
local M = {}
M.VALUE = %d
M.count = 0
function M.get() return %d end
function M.inc() M.count += 1; return M.count end
return M
""" % [value, value]


func _initialize() -> void:
	DirAccess.make_dir_recursive_absolute(DIR.path_join("sub"))
	write("util.luau", util_source(1))
	write("sub/user.luau", """
local U = require("../util")
local Same = require("hot_test.util")
local User = { extends = "Node" }
function User:fetch() return U.get() end
function User:value() return U.VALUE end
function User:inc() return U.inc() end
function User:count() return U.count end
function User:same() return U == Same end
function User:set_state(n) self.state = n end
function User:get_state() return self.state end
function User:via_self() return self:fetch() end
return User
""")
	write("base.luau", """
local Base = { extends = "Node" }
function Base:greet() return "base 1" end
return Base
""")
	write("derived.luau", """
local Base = require("./base")
local Derived = { extends = Base }
function Derived:call_greet() return self:greet() end
return Derived
""")
	write("fn_module.luau", "return function(x) return x * 2 end")
	write("cycle_a.luau", "local B = require('./cycle_b'); return { extends = 'Node' }")
	write("cycle_b.luau", "local A = require('./cycle_a'); return {}")
	write("uses_fn.luau", """
local double = require("./fn_module")
local S = { extends = "Node" }
function S:run(x) return double(x) end
return S
""")
	write("fennel_user.fnl", """
(local U (require :hot_test.util))
(local F {:extends "Node"})
(fn F.fetch [self] (U.get))
F
""")
	await process_frame
	await run_checks()
	DirAccess.remove_absolute(DIR)  # only removes an empty folder: see cleanup
	cleanup(DIR)
	print("failures: ", failures)
	quit(failures)


func cleanup(path: String) -> void:
	for sub in DirAccess.get_directories_at(path):
		cleanup(path.path_join(sub))
	for file in DirAccess.get_files_at(path):
		DirAccess.remove_absolute(path.path_join(file))
	DirAccess.remove_absolute(path)


func node_with(path: String) -> Node:
	var node := Node.new()
	node.set_script(load(DIR.path_join(path)))
	root.add_child(node)
	return node


func run_checks() -> void:
	# Modules
	var user := node_with("sub/user.luau")
	check(user.fetch() == 1 and user.value() == 1, "relative require from a subfolder")
	check(user.same(), "dotted and relative names give the same module table")
	var uses_fn := node_with("uses_fn.luau")
	check(uses_fn.run(21) == 42, "a module can return a function")
	var cycle: Script = load(DIR.path_join("cycle_a.luau"))
	check(cycle == null or not cycle.can_instantiate(), "a require cycle fails with an error instead of hanging")
	var fennel := node_with("fennel_user.fnl")
	check(fennel.fetch() == 1, "Fennel requires a Luau module by dotted name")

	# Reloading a module: users see the new code, instances keep their fields
	user.set_state(5)
	check(user.inc() == 1 and user.inc() == 2, "module state before reloading")
	write("util.luau", util_source(2))
	check(reload("util.luau") == OK, "module reloads")
	check(user.fetch() == 2 and user.value() == 2, "users see the reloaded module")
	check(user.via_self() == 2, "Lua calls through self see it too (method cache emptied)")
	check(user.get_state() == 5, "the instance kept its fields")
	check(fennel.fetch() == 2, "a Fennel user sees it")
	check(user.inc() == 1 and user.count() == 1, "module state stays one table: the new code's M is the shared M")

	# Reloading a class script
	write("sub/user.luau", FileAccess.get_file_as_string(DIR.path_join("sub/user.luau")).replace(
		"function User:fetch() return U.get() end",
		"function User:fetch() return 100 + U.get() end").replace(
		"local User = { extends = \"Node\" }",
		"local User = { extends = \"Node\", properties = { added = 7 } }"))
	check(reload("sub/user.luau") == OK, "class script reloads")
	check(user.fetch() == 102, "Godot calls the new method")
	check(user.via_self() == 102, "Lua calls through self call the new method")
	check(user.get_state() == 5, "the instance kept its fields")
	check(user.get("added") == 7, "a property added by the reload gets its default")

	# A failed reload keeps the loaded version
	write("util.luau", "local M = { this is not Luau")
	check(reload("util.luau") != OK, "a syntax error fails the reload")
	check(user.fetch() == 102 and user.value() == 2, "the previous version keeps working")

	# Base script reloaded: derived scripts and their instances follow
	var derived := node_with("derived.luau")
	check(derived.call_greet() == "base 1" and derived.greet() == "base 1", "derived instance before")
	write("base.luau", """
local Base = { extends = "Node" }
function Base:greet() return "base 2" end
return Base
""")
	check(reload("base.luau") == OK, "base script reloads")
	check(derived.greet() == "base 2", "Godot calls the reloaded inherited method")
	check(derived.call_greet() == "base 2", "Lua calls the reloaded inherited method")

	# The file watcher (debug builds outside the editor): no reload() call
	write("util.luau", util_source(30))
	await create_timer(1.2).timeout
	check(user.fetch() == 130, "the file watcher reloads a changed module (got %s)" % user.fetch())
	write("base.luau", """
local Base = { extends = "Node" }
function Base:greet() return "watched" end
return Base
""")
	await create_timer(1.2).timeout
	check(derived.greet() == "watched", "the file watcher reloads a changed base script")

	for node in [user, uses_fn, fennel, derived]:
		node.free()
