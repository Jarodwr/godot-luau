extends SceneTree
# Fennel scripts: the mover's movement, and defnode/deftool's declarations
# (docs/adr/0057, features/fennel_node.fnl)

var failures := 0


func check(ok: bool, what: String) -> void:
	print(("ok   " if ok else "FAIL ") + what)
	if not ok:
		failures += 1


func _initialize() -> void:
	var node = Node2D.new()
	node.set_script(load("res://mover.fnl"))
	root.add_child(node)
	for i in 3:
		await process_frame
	print("fennel mover position = ", node.position)
	check(node.position.x > 0 and node.position.y > 0, "the defnode mover moves")
	node.free()

	var script: Script = load("res://features/fennel_node.fnl")
	check(script.get_global_name() == &"FennelNode", "the class is named after defnode's symbol")
	var f = script.new()
	var exported := []
	var plain := {}
	for p in f.get_property_list():
		if p.usage & PROPERTY_USAGE_GROUP:
			exported.append("[%s]" % p.name)
		elif p.usage & PROPERTY_USAGE_SUBGROUP:
			exported.append("[[%s]]" % p.name)
		elif p.usage & PROPERTY_USAGE_SCRIPT_VARIABLE:
			if p.usage & PROPERTY_USAGE_EDITOR:
				exported.append(p.name)
			else:
				plain[p.name] = p
	var order := ", ".join(exported.slice(exported.find("power")))
	check(order == "power, title, target, [Combat], health, [[Shield Tuning]], shield", "exports in declaration order, with (group …) and (subgroup …) sections (%s)" % order)
	check(f.power == 3 and f.title == "x" and f.target == null and f.shield == 2, "export defaults")
	check(plain.has("phase") and f.phase == 1, "a prop without a map is a plain property")
	check(plain.has("dead") and not exported.has("dead"), ":export false keeps a prop out of the inspector")
	f.health = -5
	check(f.health == 0 and f.dead == true, "the export's setter and the hidden getter")
	check(f.describe() == "fennel+base+derived", "super calls the base's method (%s)" % f.describe())
	check(f.shout() == "HEY world!", "argument defaults from method metadata")
	check(f.bonus() == 13, "a local constant and self in a method")
	var sig := {}
	for s in f.get_signal_list():
		sig[s.name] = s
	check(sig.has("enraged") and sig.has("hit") and sig.hit.args.map(func(a): return [a.name, a.type]) == [["damage", TYPE_INT], ["by", TYPE_NIL], ["boss", TYPE_OBJECT]] and sig.hit.args[2].class_name == &"LuauFeatures", "signals with typed arguments, a script type too (%s)" % [sig.hit.args])
	check(f is LuauFeatures, "extends a required script")
	f.free()

	# alias: the node at a path, looked up on first use, then a plain field
	var aliased := Node.new()
	var child := Node.new()
	child.name = "Child"
	var grandchild := Node.new()
	grandchild.name = "Grandchild"
	child.add_child(grandchild)
	aliased.add_child(child)
	aliased.set_script(load("res://features/fennel_alias.fnl"))
	check(aliased.child == child and aliased.deep == grandchild, "alias gets the node at its path, from GDScript too")
	check(aliased.cached(), "an alias is a plain field after its first read")
	aliased.free()
	var fresh_a := Node.new()
	var fresh_b := Node.new()
	fresh_a.set_script(load("res://features/fennel_fresh_defaults.fnl"))
	fresh_b.set_script(load("res://features/fennel_fresh_defaults.fnl"))
	fresh_a.items.append(1)
	fresh_a.tags["k"] = 1
	fresh_a.nested[0].append(2)
	check(fresh_b.items == [] and fresh_b.tags == {} and fresh_b.nested == [[1]], "each instance has its own Array and Dictionary defaults (%s %s %s)" % [fresh_b.items, fresh_b.tags, fresh_b.nested])
	check(typeof(fresh_a.tags) == TYPE_DICTIONARY, "a {} default is a Dictionary")
	check(fresh_a.curve is Curve and fresh_a.curve != fresh_b.curve, "an object default is made for each instance")
	check(fresh_a.speed == 1.5, "a literal default is the value")
	fresh_a.free()
	fresh_b.free()
	var macro_user = load("res://features/fennel_macros.fnl").new()
	check(macro_user != null and macro_user.run(5) == [10, 20, 15], "import-macros from res://: dotted and relative (nested) names (%s)" % [macro_user.run(5) if macro_user else null])
	if macro_user:
		check(macro_user.whoami() == "macros.util res://macros/util.fnlm", "a macro module gets its name and path as ... (%s)" % macro_user.whoami())
		macro_user.free()
	print("failures: ", failures)
	quit(failures)
