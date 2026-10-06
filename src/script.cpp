#include "script.h"

#include "api.h"
#include "internal.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <Luau/Parser.h>
#include <luacode.h>
#include <lualib.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <vector>

namespace luau {

LuauLanguage *LuauLanguage::singleton = nullptr;

// Scripts by their class table, for `extends = require("res://…")`
static HashMap<const void *, LuauScript *> scripts_by_class;

// ---------------------------------------------------------------- modules
// (docs/adr/0036)

// Scripts loaded through require: kept loaded while the Luau state lives, as
// package.loaded keeps modules (a script used as a base must outlive the
// require call)
static HashMap<String, Ref<LuauScript>> required_scripts;
// Loaded scripts by path, for reloading dependents and the file watcher
static HashMap<String, LuauScript *> scripts_by_path;
// Scripts whose chunk required a path while it ran: reloaded after it
static HashMap<String, HashSet<String>> dependents;
// Scripts whose chunk is running now (nested by require)
static std::vector<LuauScript *> loading;
// Scripts being reloaded now (with their dependents)
static HashSet<String> reloading_paths;

// A module name to a file: "res://a/b" (or "@res/a/b", Luau's alias form,
// which luau-lsp follows: docs/adr/0048), "./b" or "../b" (relative to the
// requiring file), or "a.b" (from res://). The extension may be left out.
static String resolve_module(lua_State *L, const String &p_name) {
	String name = p_name.begins_with("@res/") ? "res://" + p_name.substr(5) : p_name;
	String base;
	if (name.begins_with("res://")) {
		base = name;
	} else if (name.begins_with("./") || name.begins_with("../")) {
		lua_Debug ar;
		String from;
		if (lua_getinfo(L, 1, "s", &ar) && ar.source[0] == '@') {
			from = String::utf8(ar.source + 1);
		}
		if (!from.begins_with("res://")) {
			return String();
		}
		base = from.get_base_dir().path_join(name).simplify_path();
	} else {
		base = "res://" + name.replace(".", "/");
	}
	String ext = base.get_extension();
	if ((ext == "luau" || ext == "fnl") && FileAccess::file_exists(base)) {
		return base;
	}
	for (const char *candidate : { ".luau", ".fnl" }) {
		if (FileAccess::file_exists(base + candidate)) {
			return base + candidate;
		}
	}
	return String();
}

// The value a module file returned, loading it (as a script resource) once
static int load_module(lua_State *L, const String &path) {
	for (size_t i = 0; i < loading.size(); i++) {
		if (loading[i]->get_path() == path) {
			String cycle;
			for (size_t j = i; j < loading.size(); j++) {
				cycle += loading[j]->get_path() + " -> ";
			}
			luaL_error(L, "require cycle: %s", (cycle + path).utf8().get_data());
		}
	}
	if (!loading.empty()) {
		dependents[path].insert(loading.back()->get_path());
	}
	Ref<LuauScript> script;
	if (Ref<LuauScript> *kept = required_scripts.getptr(path)) {
		script = *kept;
	} else {
		script = ResourceLoader::get_singleton()->load(path);
	}
	if (script.is_null() || script->class_ref == LUA_NOREF) {
		luaL_error(L, "can't load module '%s'", path.utf8().get_data());
	}
	required_scripts[path] = script;
	lua_getref(L, script->class_ref);
	return 1;
}

// require(name): Luau and Fennel files by path, else package.preload and
// package.loaded (the prelude's require, for Fennel's own modules)
static int lua_require(lua_State *L) {
	String name = String::utf8(luaL_checkstring(L, 1));
	bool is_path = name.begins_with("res://") || name.begins_with("@res/") || name.begins_with("./") || name.begins_with("../");
	if (!is_path) {
		// package.loaded, then package.preload (the prelude's require)
		lua_getglobal(L, "package");
		lua_getfield(L, -1, "loaded");
		lua_pushvalue(L, 1);
		lua_rawget(L, -2);
		if (!lua_isnil(L, -1)) {
			return 1;
		}
		lua_getfield(L, -3, "preload");
		lua_pushvalue(L, 1);
		lua_rawget(L, -2);
		if (lua_isfunction(L, -1)) {
			lua_getglobal(L, "__lua_require");
			lua_pushvalue(L, 1);
			lua_call(L, 1, 1);
			return 1;
		}
		lua_settop(L, 1);
	}
	String path = resolve_module(L, name);
	if (path.is_empty()) {
		luaL_error(L, "module '%s' not found", lua_tostring(L, 1));
	}
	return load_module(L, path);
}

// Lua lets go of everything when the main loop is deleted. Godot finishes
// script languages in no fixed order, and a value from another language that
// Lua still holds (a GDScript lambda) must be released before that language
// finishes. After this the state stays closed.
static bool main_loop_gone = false;

static void main_loop_freed(void *, void *, void *) {
	main_loop_gone = true;
	required_scripts.clear();
	dependents.clear();
	close_state();
}

static void *main_loop_bound(void *p_token, void *) {
	return p_token;  // any non-null binding
}

static GDExtensionBool main_loop_reference(void *, void *, GDExtensionBool) {
	return true;
}

static const GDExtensionInstanceBindingCallbacks main_loop_callbacks = { main_loop_bound, main_loop_freed, main_loop_reference };

// Called when a script instance is made: the main loop exists by then
static void watch_main_loop() {
	static GDExtensionObjectPtr watched = nullptr;
	if (watched != nullptr) {
		return;
	}
	Object *loop = Engine::get_singleton()->call("get_main_loop");
	if (loop == nullptr) {
		return;
	}
	watched = loop->_owner;
	gdextension_interface::object_get_instance_binding(watched, (void *)&main_loop_callbacks, &main_loop_callbacks);
}

static lua_State *ensure_state() {
	if (state() == nullptr && !main_loop_gone) {
		open_state();
		lua_State *L = state();
		lua_getglobal(L, "require");
		lua_setglobal(L, "__lua_require");
		lua_pushcfunction(L, lua_require, "require");
		lua_setglobal(L, "require");
	}
	return state();
}

// ---------------------------------------------------------------- instances

// `self` for a Luau script instance is a table T (fields). T's metatable M
// sends misses to B (cached script functions and engine method binds); B's
// metatable resolves names not cached yet: engine properties are read through
// their getters on every access, methods are cached in B.
struct Instance {
	Ref<LuauScript> script;
	GDExtensionObjectPtr owner;
	uint64_t owner_id;  // ObjectID of owner
	ClassInfo *cls;
	LuauScript::Routes *routes;
	int table_ref = LUA_NOREF;  // T
	int cache_ref = LUA_NOREF;  // B
	int meta_ref = LUA_NOREF;   // M
	// Upvalue of the miss handlers: holds this instance, or null once freed
	// (docs/adr/0011)
	struct Handle *handle = nullptr;
};

struct Handle {
	Instance *instance;
};

static HashMap<GDExtensionObjectPtr, Instance *> instances;

static Instance *upvalue_instance(lua_State *L) {
	return static_cast<Handle *>(lua_touserdata(L, lua_upvalueindex(1)))->instance;
}
static const char INSTANCE_KEY = 0;

bool push_self_table(lua_State *L, GDExtensionObjectPtr object) {
	Instance **instance = instances.getptr(object);
	if (instance == nullptr) {
		return false;
	}
	lua_getref(L, (*instance)->table_ref);
	return true;
}

// The instance stored at INSTANCE_KEY in the table at `index`, or null
static Instance *instance_in(lua_State *L, int index) {
	lua_pushlightuserdata(L, (void *)&INSTANCE_KEY);
	lua_rawget(L, index < 0 ? index - 1 : index);
	Instance *instance = (Instance *)lua_tolightuserdata(L, -1);
	lua_pop(L, 1);
	return instance;
}

GDExtensionObjectPtr self_table_owner(lua_State *L, int index) {
	if (!lua_getmetatable(L, index)) {
		return nullptr;
	}
	Instance *instance = instance_in(L, -1);
	lua_pop(L, 1);
	return instance ? instance->owner : nullptr;
}

// Calls script method `method` on the instance with `nargs` arguments already
// on the stack; leaves one result. Errors propagate as Lua errors.
static void call_script_method(lua_State *L, Instance *instance, const LuauScript::MethodDef *method, int nargs) {
	lua_getref(L, method->ref);
	lua_getref(L, instance->table_ref);
	if (nargs > 0) {
		lua_insert(L, -2 - nargs);
		lua_insert(L, -2 - nargs);
	}
	lua_call(L, nargs + 1, 1);
}

// Same, protected: false (error reported) if it raised
static bool pcall_script_method(lua_State *L, Instance *instance, const LuauScript::MethodDef *method, int nargs) {
	lua_getref(L, method->ref);
	lua_getref(L, instance->table_ref);
	if (nargs > 0) {
		lua_insert(L, -2 - nargs);
		lua_insert(L, -2 - nargs);
	}
	if (lua_pcall(L, nargs + 1, 1, ERROR_HANDLER) != LUA_OK) {
		lua_pop(L, 1);  // reported by the handler
		return false;
	}
	return true;
}

// The route of the string key at index 2. Order: script function (including
// inherited ones), declared property, signal, engine member.
static LuauScript::Route resolve_route(lua_State *L, Instance *instance, const StringName &name) {
	LuauScript::Route route;
	lua_getref(L, instance->script->class_ref);
	lua_pushvalue(L, 2);
	// Methods and constants, through `extends`
	int type = lua_gettable(L, -2);
	lua_pop(L, 2);
	if (type == LUA_TFUNCTION || (type != LUA_TNIL && instance->script->constants.has(String(name)))) {
		route.kind = LuauScript::ROUTE_SCRIPT;
	} else if (const LuauScript::PropertyDef *property = instance->script->find_property(name)) {
		route.kind = LuauScript::ROUTE_PROPERTY;
		route.property = property;
	} else if (instance->script->find_signal(name)) {
		route.kind = LuauScript::ROUTE_SIGNAL;
	} else {
		route.kind = LuauScript::ROUTE_ENGINE;
	}
	return route;
}

// Copied: resolving members can run scripts, which may grow the table
static LuauScript::Route route_of(lua_State *L, Instance *instance, int atom) {
	if (atom < 0) {
		StringName name(lua_tostring(L, 2));
		LuauScript::Route route = resolve_route(L, instance, name);
		if (route.kind == LuauScript::ROUTE_ENGINE) {
			route.member = &instance->cls->member(name);
		}
		return route;
	}
	std::vector<LuauScript::Route> &by_atom = instance->routes->by_atom;
	if ((size_t)atom >= by_atom.size()) {
		by_atom.resize(atom + 1);
	}
	if (by_atom[atom].kind == LuauScript::ROUTE_UNRESOLVED) {
		LuauScript::Route route = resolve_route(L, instance, atom_name(atom));
		if (route.kind == LuauScript::ROUTE_ENGINE) {
			route.member = &instance->cls->member(atom);
		}
		instance->routes->by_atom[atom] = route;
	}
	return instance->routes->by_atom[atom];
}

static const StringName &key_name(lua_State *L, StringName &storage) {
	int atom = string_atom(L, 2);
	if (atom >= 0) {
		return atom_name(atom);
	}
	storage = StringName(lua_tostring(L, 2));
	return storage;
}

// B's __index: names neither in T nor cached in B
static int cache_index(lua_State *L) {
	Instance *instance = upvalue_instance(L);
	if (instance == nullptr) {
		luaL_error(L, "attempt to use a freed object");
	}
	if (lua_type(L, 2) != LUA_TSTRING) {
		lua_pushnil(L);
		return 1;
	}
	LuauScript::Route route = route_of(L, instance, string_atom(L, 2));
	switch (route.kind) {
		case LuauScript::ROUTE_SCRIPT:
			// Cache the script function or constant in B
			lua_getref(L, instance->script->class_ref);
			lua_pushvalue(L, 2);
			lua_gettable(L, -2);
			lua_pushvalue(L, 2);
			lua_pushvalue(L, -2);
			lua_rawset(L, 1);
			return 1;
		case LuauScript::ROUTE_PROPERTY: {
			// Plain properties are fields of T: missing means nil. Properties
			// with accessors call their getter on every read.
			const LuauScript::PropertyDef *property = route.property;
			const LuauScript::MethodDef *getter = property->getter_method;
			if (getter == nullptr) {
				lua_pushnil(L);
				return 1;
			}
			call_script_method(L, instance, getter, 0);
			return 1;
		}
		case LuauScript::ROUTE_SIGNAL: {
			// A Signal value, created on first use and kept as a field of T
			StringName storage;
			const StringName &name = key_name(L, storage);
			Object *owner = ObjectDB::get_instance(gdextension_interface::object_get_instance_id(instance->owner));
			push_variant(L, Signal(owner, name));
			lua_getref(L, instance->table_ref);
			lua_pushvalue(L, 2);
			lua_pushvalue(L, -3);
			lua_rawset(L, -3);
			lua_pop(L, 1);
			return 1;
		}
		default:
			break;
	}
	const Member &member = *route.member;
	switch (member.kind) {
		case MemberKind::METHOD:
			push_member_method(L, member);
			lua_pushvalue(L, 2);
			lua_pushvalue(L, -2);
			lua_rawset(L, 1);
			return 1;
		case MemberKind::PROPERTY:
			if (!get_property(L, instance->owner, member)) {
				lua_error(L);
			}
			return 1;
		default:
			// Unknown to the engine: the script's _get answers it, as
			// Object::get would end up asking it
			if (const LuauScript::MethodDef *get = instance->script->get_method) {
				lua_pushvalue(L, 2);
				call_script_method(L, instance, get, 1);
				return 1;
			}
			push_object_get(L, instance->owner, member.name.is_empty() ? StringName(lua_tostring(L, 2)) : member.name);
			return 1;
	}
}

// T's __newindex: names not in T yet. Properties with accessors call their
// setter; engine properties go to theirs; anything else becomes a field.
static int self_newindex(lua_State *L) {
	Instance *instance = upvalue_instance(L);
	if (instance == nullptr) {
		luaL_error(L, "attempt to use a freed object");
	}
	if (lua_type(L, 2) == LUA_TSTRING) {
		LuauScript::Route route = route_of(L, instance, string_atom(L, 2));
		if (route.kind == LuauScript::ROUTE_PROPERTY && route.property->has_accessors()) {
			const LuauScript::PropertyDef *property = route.property;
			const LuauScript::MethodDef *setter = property->setter_method;
			if (setter == nullptr) {
				CharString name = String(property->name).utf8();
				luaL_error(L, "property '%s' is read-only", name.get_data());
			}
			lua_pushvalue(L, 3);
			call_script_method(L, instance, setter, 1);
			return 0;
		}
		if (route.kind == LuauScript::ROUTE_ENGINE && route.member->kind == MemberKind::PROPERTY) {
			if (!set_property(L, instance->owner, *route.member, 3)) {
				lua_error(L);
			}
			return 0;
		}
	}
	lua_rawset(L, 1);
	return 0;
}

static void create_self_table(lua_State *L, Instance *instance) {
	lua_newtable(L);  // T
	lua_newtable(L);  // M
	lua_newtable(L);  // B

	instance->handle = static_cast<Handle *>(lua_newuserdata(L, sizeof(Handle)));
	instance->handle->instance = instance;
	int handle = lua_gettop(L);

	lua_newtable(L);  // B's metatable
	lua_pushvalue(L, handle);
	lua_pushcclosurek(L, cache_index, "__index", 1, nullptr);
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, -3);  // B
	lua_insert(L, -3);        // T handle M B
	instance->cache_ref = lua_ref(L, -1);
	lua_setfield(L, -2, "__index");  // M.__index = B

	lua_pushlightuserdata(L, (void *)&INSTANCE_KEY);
	lua_pushlightuserdata(L, instance);
	lua_rawset(L, -3);
	lua_pushvalue(L, -2);
	lua_pushcclosurek(L, self_newindex, "__newindex", 1, nullptr);
	lua_setfield(L, -2, "__newindex");
	instance->meta_ref = lua_ref(L, -1);
	lua_remove(L, -2);        // T M
	lua_setmetatable(L, -2);  // T's metatable = M

	// Plain properties start with their defaults, as fields
	for (const LuauScript::PropertyDef &property : instance->script->properties) {
		if (!property.has_accessors() && property.default_value.get_type() != Variant::NIL) {
			push_variant(L, property.default_value);
			lua_rawsetfield(L, -2, String(property.name).utf8().get_data());
		}
	}

	instance->table_ref = lua_ref(L, -1);
	lua_pop(L, 1);
}

// ---- GDExtensionScriptInstanceInfo3 callbacks

// A value of a declared type: numbers converted directly, anything else
// through coerce_to_type
static void push_typed(lua_State *L, const Variant &value, Variant::Type type) {
	Variant::Type have = type_of(value);  // read from the bytes: no engine call
	const unsigned char *data = reinterpret_cast<const unsigned char *>(value._native_ptr()) + VARIANT_DATA;
	if (type == Variant::NIL || have == type || have == Variant::NIL) {
		push_variant(L, value);
	} else if (type == Variant::FLOAT && have == Variant::INT && variant_layout_checked()) {
		lua_pushnumber(L, (double)*reinterpret_cast<const int64_t *>(data));
	} else if (type == Variant::INT && have == Variant::FLOAT && variant_layout_checked()) {
		lua_pushnumber(L, (double)(int64_t)*reinterpret_cast<const double *>(data));
	} else {
		push_variant(L, coerce_to_type(value, type));
	}
}

static void to_typed(lua_State *L, int index, Variant::Type type, Variant *r_value) {
	write_result(L, index, r_value, type);
}

static GDExtensionBool set_func(Instance *instance, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) {
	lua_State *L = state();
	const StringName &name = *(const StringName *)p_name;
	const Variant &value = *(const Variant *)p_value;
	const LuauScript *script = instance->script.ptr();
	if (const LuauScript::PropertyDef *property = script->find_property(name)) {
		if (property->has_accessors()) {
			const LuauScript::MethodDef *setter = property->setter_method;
			if (setter == nullptr) {
				return false;
			}
			push_variant(L, value);
			if (pcall_script_method(L, instance, setter, 1)) {
				lua_pop(L, 1);
			}
			return true;
		}
		lua_getref(L, instance->table_ref);
		push_string_name(L, name);
		push_variant(L, value);
		lua_rawset(L, -3);
		lua_pop(L, 1);
		return true;
	}
	// An existing field
	lua_getref(L, instance->table_ref);
	push_string_name(L, name);
	lua_pushvalue(L, -1);
	bool has = lua_rawget(L, -3) != LUA_TNIL;
	lua_pop(L, 1);
	if (has) {
		push_variant(L, value);
		lua_rawset(L, -3);
		lua_pop(L, 1);
		return true;
	}
	lua_pop(L, 2);
	// _set(name, value) -> true if handled
	if (script->set_method) {
		push_string_name(L, name);
		push_variant(L, value);
		if (pcall_script_method(L, instance, script->set_method, 2)) {
			bool handled = lua_toboolean(L, -1);
			lua_pop(L, 1);
			return handled;
		}
	}
	return false;
}

static GDExtensionBool get_func(Instance *instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) {
	lua_State *L = state();
	const StringName &name = *(const StringName *)p_name;
	const LuauScript *script = instance->script.ptr();
	if (const LuauScript::PropertyDef *property = script->find_property(name)) {
		if (property->has_accessors()) {
			const LuauScript::MethodDef *getter = property->getter_method;
			if (getter == nullptr || !pcall_script_method(L, instance, getter, 0)) {
				return false;
			}
		} else {
			lua_getref(L, instance->table_ref);
			push_string_name(L, name);
			lua_rawget(L, -2);
			lua_remove(L, -2);
		}
		// Lua loses some types (whole floats read as ints, Vector3 with z = 0
		// as Vector2): the declared type restores them
		to_typed(L, -1, property->type, (Variant *)r_ret);
		lua_pop(L, 1);
		return true;
	}
	if (script->find_signal(name)) {
		Object *owner = ObjectDB::get_instance(gdextension_interface::object_get_instance_id(instance->owner));
		*(Variant *)r_ret = Signal(owner, name);
		return true;
	}
	lua_getref(L, instance->table_ref);
	push_string_name(L, name);
	bool has = lua_rawget(L, -2) != LUA_TNIL;
	if (has) {
		write_result(L, -1, (Variant *)r_ret);
	}
	lua_pop(L, 2);
	if (has) {
		return true;
	}
	// _get(name) -> a value, or nil if not handled
	if (script->get_method) {
		push_string_name(L, name);
		if (pcall_script_method(L, instance, script->get_method, 1)) {
			bool handled = !lua_isnil(L, -1);
			if (handled) {
				write_result(L, -1, (Variant *)r_ret);
			}
			lua_pop(L, 1);
			return handled;
		}
	}
	return false;
}

// Property and method lists handed to Godot: the info structs point into
// storage kept alive until Godot frees the list
struct PropertyListStorage {
	std::vector<GDExtensionPropertyInfo> infos;
	std::vector<StringName> names;
	std::vector<StringName> class_names;
	std::vector<String> hints;
};
static HashMap<const void *, PropertyListStorage *> property_lists;

static uint32_t property_usage(const LuauScript::PropertyDef &property) {
	if (property.header_usage) {
		return property.header_usage;
	}
	return property.exported ? (PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SCRIPT_VARIABLE) : PROPERTY_USAGE_SCRIPT_VARIABLE;
}

// A property list as dictionaries, when the script overrides
// _get_property_list (properties added at runtime) or _validate_property
// (changes to each property's info), as in GDScript
static Vector<Dictionary> script_property_list(Instance *instance) {
	const LuauScript *script = instance->script.ptr();
	Vector<Dictionary> list;
	for (const LuauScript::PropertyDef &p : script->listed) {
		Dictionary d;
		d["name"] = p.name;
		d["type"] = p.type;
		d["class_name"] = p.class_name;
		d["hint"] = p.hint;
		d["hint_string"] = p.hint_string;
		d["usage"] = property_usage(p);
		list.push_back(d);
	}
	lua_State *L = state();
	if (script->property_list_method && pcall_script_method(L, instance, script->property_list_method, 0)) {
		Variant added = to_variant(L, -1);
		lua_pop(L, 1);
		if (added.get_type() == Variant::ARRAY) {
			Array array = added;
			for (int i = 0; i < array.size(); i++) {
				if (array[i].get_type() == Variant::DICTIONARY) {
					list.push_back(array[i]);
				}
			}
		}
	}
	if (script->validate_property_method) {
		for (Dictionary &d : list) {
			push_variant(L, d);  // a Dictionary: the method changes it in place
			if (pcall_script_method(L, instance, script->validate_property_method, 1)) {
				lua_pop(L, 1);
			}
		}
	}
	return list;
}

static const GDExtensionPropertyInfo *get_property_list_func(Instance *instance, uint32_t *r_count) {
	const Vector<LuauScript::PropertyDef> &properties = instance->script->listed;
	if (instance->script->property_list_method || instance->script->validate_property_method) {
		Vector<Dictionary> list = script_property_list(instance);
		*r_count = (uint32_t)list.size();
		if (list.is_empty()) {
			return nullptr;
		}
		PropertyListStorage *storage = new PropertyListStorage();
		storage->infos.resize(list.size());
		storage->names.resize(list.size());
		storage->class_names.resize(list.size());
		storage->hints.resize(list.size());
		for (int i = 0; i < list.size(); i++) {
			const Dictionary &d = list[i];
			storage->names[i] = d.get("name", "");
			storage->class_names[i] = d.get("class_name", "");
			storage->hints[i] = d.get("hint_string", "");
			GDExtensionPropertyInfo &info = storage->infos[i];
			info.type = (GDExtensionVariantType)(int)d.get("type", 0);
			info.name = storage->names[i]._native_ptr();
			info.class_name = storage->class_names[i]._native_ptr();
			info.hint = (uint32_t)(int)d.get("hint", 0);
			info.hint_string = storage->hints[i]._native_ptr();
			info.usage = (uint32_t)(int)d.get("usage", PROPERTY_USAGE_DEFAULT);
		}
		property_lists.insert(storage->infos.data(), storage);
		return storage->infos.data();
	}
	*r_count = (uint32_t)properties.size();
	if (properties.is_empty()) {
		return nullptr;
	}
	PropertyListStorage *storage = new PropertyListStorage();
	storage->infos.resize(properties.size());
	storage->names.resize(properties.size());
	storage->class_names.resize(properties.size());
	storage->hints.resize(properties.size());
	for (int i = 0; i < properties.size(); i++) {
		const LuauScript::PropertyDef &p = properties[i];
		storage->names[i] = p.name;
		storage->class_names[i] = p.class_name;
		storage->hints[i] = p.hint_string;
		GDExtensionPropertyInfo &info = storage->infos[i];
		info.type = (GDExtensionVariantType)p.type;
		info.name = storage->names[i]._native_ptr();
		info.class_name = storage->class_names[i]._native_ptr();
		info.hint = p.hint;
		info.hint_string = storage->hints[i]._native_ptr();
		info.usage = property_usage(p);
	}
	property_lists.insert(storage->infos.data(), storage);
	return storage->infos.data();
}

static void free_property_list_func(Instance *, const GDExtensionPropertyInfo *list, uint32_t) {
	if (PropertyListStorage **storage = property_lists.getptr(list)) {
		delete *storage;
		property_lists.erase(list);
	}
}

static GDExtensionVariantType get_property_type_func(Instance *instance, GDExtensionConstStringNamePtr p_name, GDExtensionBool *r_is_valid) {
	const LuauScript::PropertyDef *property = instance->script->find_property(*(const StringName *)p_name);
	*r_is_valid = property != nullptr;
	return property ? (GDExtensionVariantType)property->type : GDEXTENSION_VARIANT_TYPE_NIL;
}

static GDExtensionBool property_can_revert_func(Instance *instance, GDExtensionConstStringNamePtr p_name) {
	if (const LuauScript::MethodDef *method = instance->script->can_revert_method) {
		lua_State *L = state();
		push_string_name(L, *(const StringName *)p_name);
		if (pcall_script_method(L, instance, method, 1)) {
			bool can = lua_toboolean(L, -1);
			lua_pop(L, 1);
			if (can) {
				return true;
			}
		}
	}
	const LuauScript::PropertyDef *property = instance->script->find_property(*(const StringName *)p_name);
	if (property == nullptr || !property->exported) {
		return false;
	}
	Variant current;
	if (!get_func(instance, p_name, &current)) {
		return false;
	}
	return current != property->default_value;
}

static GDExtensionBool property_get_revert_func(Instance *instance, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) {
	if (const LuauScript::MethodDef *method = instance->script->get_revert_method) {
		lua_State *L = state();
		push_string_name(L, *(const StringName *)p_name);
		if (pcall_script_method(L, instance, method, 1)) {
			bool has = !lua_isnil(L, -1);
			if (has) {
				*(Variant *)r_ret = to_variant(L, -1);
			}
			lua_pop(L, 1);
			if (has) {
				return true;
			}
		}
	}
	const LuauScript::PropertyDef *property = instance->script->find_property(*(const StringName *)p_name);
	if (property == nullptr) {
		return false;
	}
	*(Variant *)r_ret = property->default_value;
	return true;
}

struct MethodListStorage {
	std::vector<GDExtensionMethodInfo> infos;
	std::vector<StringName> names;
	std::vector<std::vector<GDExtensionPropertyInfo>> args;
	std::vector<std::vector<StringName>> arg_names;
	StringName empty;
	String empty_hint;
};
static HashMap<const void *, MethodListStorage *> method_lists;

static const GDExtensionMethodInfo *get_method_list_func(Instance *instance, uint32_t *r_count) {
	const HashMap<StringName, LuauScript::MethodDef> &methods = instance->script->methods;
	*r_count = (uint32_t)methods.size();
	if (methods.is_empty()) {
		return nullptr;
	}
	MethodListStorage *storage = new MethodListStorage();
	size_t n = methods.size();
	storage->infos.resize(n);
	storage->names.resize(n);
	storage->args.resize(n);
	storage->arg_names.resize(n);
	size_t i = 0;
	for (const auto &[name, method] : methods) {
		storage->names[i] = name;
		storage->arg_names[i].resize(method.nparams);
		storage->args[i].resize(method.nparams);
		for (int a = 0; a < method.nparams; a++) {
			storage->arg_names[i][a] = StringName("arg" + itos(a + 1));
			GDExtensionPropertyInfo &arg = storage->args[i][a];
			arg = {};
			arg.type = (GDExtensionVariantType)(a < method.arg_types.size() ? method.arg_types[a] : Variant::NIL);
			arg.name = storage->arg_names[i][a]._native_ptr();
			arg.class_name = storage->empty._native_ptr();
			arg.hint_string = storage->empty_hint._native_ptr();
			arg.usage = arg.type == GDEXTENSION_VARIANT_TYPE_NIL ? PROPERTY_USAGE_NIL_IS_VARIANT : PROPERTY_USAGE_DEFAULT;
		}
		GDExtensionMethodInfo &info = storage->infos[i];
		info = {};
		info.name = storage->names[i]._native_ptr();
		info.return_value.type = (GDExtensionVariantType)method.ret_type;
		info.return_value.name = storage->empty._native_ptr();
		info.return_value.class_name = storage->empty._native_ptr();
		info.return_value.hint_string = storage->empty_hint._native_ptr();
		info.return_value.usage = method.has_ret_type ? PROPERTY_USAGE_DEFAULT : PROPERTY_USAGE_NIL_IS_VARIANT;
		info.flags = GDEXTENSION_METHOD_FLAGS_DEFAULT | (method.is_static ? GDEXTENSION_METHOD_FLAG_STATIC : 0);
		info.argument_count = (uint32_t)method.nparams;
		info.arguments = storage->args[i].empty() ? nullptr : storage->args[i].data();
		i++;
	}
	method_lists.insert(storage->infos.data(), storage);
	return storage->infos.data();
}

static void free_method_list_func(Instance *, const GDExtensionMethodInfo *list, uint32_t) {
	if (MethodListStorage **storage = method_lists.getptr(list)) {
		delete *storage;
		method_lists.erase(list);
	}
}

static GDExtensionBool has_method_func(Instance *instance, GDExtensionConstStringNamePtr name) {
	return instance->script->find_method(*(const StringName *)name) != nullptr;
}

static GDExtensionInt get_method_argument_count_func(Instance *instance, GDExtensionConstStringNamePtr name, GDExtensionBool *r_is_valid) {
	const LuauScript::MethodDef *method = instance->script->find_method(*(const StringName *)name);
	*r_is_valid = method != nullptr;
	return method ? method->nparams : 0;
}

static void call_func(Instance *instance, GDExtensionConstStringNamePtr p_method, const GDExtensionConstVariantPtr *args,
		GDExtensionInt argc, GDExtensionVariantPtr r_ret, GDExtensionCallError *r_error) {
	const LuauScript::MethodDef *method = instance->script->find_method(*(const StringName *)p_method);
	if (method == nullptr) {
		r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return;
	}
	// On a pooled thread, so the method can await (docs/adr/0035)
	Coroutine *co = acquire_thread(instance->owner_id);
	lua_State *L = co->thread;
	lua_getref(L, method->ref);
	int self_count = method->is_static ? 0 : 1;
	if (self_count) {
		lua_getref(L, instance->table_ref);
	}
	bool typed = !method->arg_types.is_empty();
	for (GDExtensionInt i = 0; i < argc; i++) {
		const Variant &arg = *(const Variant *)args[i];
		if (typed && i < method->arg_types.size()) {
			push_typed(L, arg, method->arg_types[i]);
		} else {
			push_variant(L, arg);
		}
	}
	// Declared defaults fill the rightmost missing parameters
	int pushed = (int)argc;
	int missing = method->nparams - pushed;
	if (missing > 0 && missing <= method->defaults.size()) {
		lua_getref(L, method->defaults_ref);
		int table = lua_gettop(L);
		for (int i = method->defaults.size() - missing; i < method->defaults.size(); i++) {
			lua_rawgeti(L, table, i + 1);
			pushed++;
		}
		lua_remove(L, table);
	}
	r_error->error = GDEXTENSION_CALL_OK;
	int status = run_thread(co, pushed + self_count);
	if (status == LUA_YIELD) {
		*(Variant *)r_ret = completion_signal(co);  // GDScript can await it
		return;
	}
	if (status != LUA_OK) {
		return;  // failed: reported
	}
	if (!method->has_ret_type) {
		to_variant_into_nil(L, 1, (Variant *)r_ret);  // nothing returned: stays nil
	} else if (lua_gettop(L) > 0) {
		to_typed(L, 1, method->ret_type, (Variant *)r_ret);
	}
	release_thread(co);
}

static void notification_func(Instance *instance, int32_t what, GDExtensionBool) {
	if (!instance->script->notification_method) {
		return;
	}
	Coroutine *co = acquire_thread(instance->owner_id);
	lua_State *L = co->thread;
	lua_getref(L, instance->script->notification_method->ref);
	lua_getref(L, instance->table_ref);
	lua_pushinteger(L, what);
	if (run_thread(co, 2) == LUA_OK) {
		release_thread(co);
	}
}

static void to_string_func(Instance *instance, GDExtensionBool *r_is_valid, GDExtensionStringPtr r_out) {
	*r_is_valid = false;
	const LuauScript::MethodDef *method = instance->script->to_string_method;
	lua_State *L = state();
	if (method == nullptr || L == nullptr || !pcall_script_method(L, instance, method, 0)) {
		return;
	}
	if (lua_type(L, -1) == LUA_TSTRING) {
		*(String *)r_out = String::utf8(lua_tostring(L, -1));
		*r_is_valid = true;
	}
	lua_pop(L, 1);
}

static GDExtensionObjectPtr get_owner_func(Instance *instance) {
	return instance->owner;
}

static GDExtensionObjectPtr get_script_func(Instance *instance) {
	return instance->script->_owner;
}

static GDExtensionBool is_placeholder_func(Instance *) {
	return false;
}

static GDExtensionScriptLanguagePtr get_language_func(Instance *) {
	return LuauLanguage::get_singleton()->_owner;
}

static GDExtensionBool refcount_decremented_func(Instance *) {
	return true;
}

static void free_func(Instance *instance) {
	lua_State *L = state();
	if (L) {
		// Its methods suspended in await don't continue (as in GDScript),
		// and let go of the self table now
		cancel_coroutines_of(instance->owner_id);
		// Lua may still hold the table: forget the instance
		instance->handle->instance = nullptr;
		lua_getref(L, instance->meta_ref);
		lua_pushlightuserdata(L, (void *)&INSTANCE_KEY);
		lua_pushnil(L);
		lua_rawset(L, -3);
		lua_pop(L, 1);
		lua_unref(L, instance->meta_ref);
		lua_unref(L, instance->cache_ref);
		lua_unref(L, instance->table_ref);
	}
	instances.erase(instance->owner);
	delete instance;
}

static GDExtensionScriptInstanceInfo3 instance_info = [] {
	GDExtensionScriptInstanceInfo3 info = {};
	info.set_func = (GDExtensionScriptInstanceSet)set_func;
	info.get_func = (GDExtensionScriptInstanceGet)get_func;
	info.get_property_list_func = (GDExtensionScriptInstanceGetPropertyList)get_property_list_func;
	info.free_property_list_func = (GDExtensionScriptInstanceFreePropertyList2)free_property_list_func;
	info.property_can_revert_func = (GDExtensionScriptInstancePropertyCanRevert)property_can_revert_func;
	info.property_get_revert_func = (GDExtensionScriptInstancePropertyGetRevert)property_get_revert_func;
	info.get_owner_func = (GDExtensionScriptInstanceGetOwner)get_owner_func;
	info.to_string_func = (GDExtensionScriptInstanceToString)to_string_func;
	info.get_method_list_func = (GDExtensionScriptInstanceGetMethodList)get_method_list_func;
	info.free_method_list_func = (GDExtensionScriptInstanceFreeMethodList2)free_method_list_func;
	info.get_property_type_func = (GDExtensionScriptInstanceGetPropertyType)get_property_type_func;
	info.has_method_func = (GDExtensionScriptInstanceHasMethod)has_method_func;
	info.get_method_argument_count_func = (GDExtensionScriptInstanceGetMethodArgumentCount)get_method_argument_count_func;
	info.call_func = (GDExtensionScriptInstanceCall)call_func;
	info.notification_func = (GDExtensionScriptInstanceNotification2)notification_func;
	info.refcount_decremented_func = (GDExtensionScriptInstanceRefCountDecremented)refcount_decremented_func;
	info.get_script_func = (GDExtensionScriptInstanceGetScript)get_script_func;
	info.is_placeholder_func = (GDExtensionScriptInstanceIsPlaceholder)is_placeholder_func;
	info.get_language_func = (GDExtensionScriptInstanceGetLanguage)get_language_func;
	info.free_func = (GDExtensionScriptInstanceFree)free_func;
	return info;
}();

// ---------------------------------------------------------------- statics (ADR 0039)
// A script instance on the script object itself: Object::callp and
// Object::get ask it first, so `preload("x.luau").make()` and
// `preload("x.luau").MAX` reach the script. Everything else falls through to
// the script's own methods and properties.

struct StaticInstance {
	LuauScript *script;
};

static GDExtensionBool static_set_func(StaticInstance *, GDExtensionConstStringNamePtr, GDExtensionConstVariantPtr) {
	return false;
}

static GDExtensionBool static_get_func(StaticInstance *s, GDExtensionConstStringNamePtr p_name, GDExtensionVariantPtr r_ret) {
	String name = *(const StringName *)p_name;
	if (!s->script->constants.has(name)) {
		return false;
	}
	*(Variant *)r_ret = s->script->constants[name];
	return true;
}

static GDExtensionBool static_has_method_func(StaticInstance *s, GDExtensionConstStringNamePtr p_name) {
	const LuauScript::MethodDef *method = s->script->find_method(*(const StringName *)p_name);
	return method != nullptr && method->is_static;
}

static void static_call_func(StaticInstance *s, GDExtensionConstStringNamePtr p_method, const GDExtensionConstVariantPtr *args,
		GDExtensionInt argc, GDExtensionVariantPtr r_ret, GDExtensionCallError *r_error) {
	const LuauScript::MethodDef *method = s->script->find_method(*(const StringName *)p_method);
	if (method == nullptr || !method->is_static || state() == nullptr) {
		r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;  // the script object's own methods
		return;
	}
	Coroutine *co = acquire_thread(0);
	lua_State *L = co->thread;
	lua_getref(L, method->ref);
	for (GDExtensionInt i = 0; i < argc; i++) {
		const Variant &arg = *(const Variant *)args[i];
		if (i < method->arg_types.size()) {
			push_typed(L, arg, method->arg_types[i]);
		} else {
			push_variant(L, arg);
		}
	}
	int pushed = (int)argc;
	int missing = method->nparams - pushed;
	if (missing > 0 && missing <= method->defaults.size()) {
		lua_getref(L, method->defaults_ref);
		int table = lua_gettop(L);
		for (int i = method->defaults.size() - missing; i < method->defaults.size(); i++) {
			lua_rawgeti(L, table, i + 1);
			pushed++;
		}
		lua_remove(L, table);
	}
	r_error->error = GDEXTENSION_CALL_OK;
	int status = run_thread(co, pushed);
	if (status == LUA_YIELD) {
		*(Variant *)r_ret = completion_signal(co);
		return;
	}
	if (status != LUA_OK) {
		return;
	}
	if (!method->has_ret_type) {
		to_variant_into_nil(L, 1, (Variant *)r_ret);
	} else if (lua_gettop(L) > 0) {
		to_typed(L, 1, method->ret_type, (Variant *)r_ret);
	}
	release_thread(co);
}

static GDExtensionObjectPtr static_get_owner_func(StaticInstance *s) {
	return s->script->_owner;
}

// Without this Godot's default answer is "keep the object alive", and the
// script would never be freed
static GDExtensionBool static_refcount_decremented_func(StaticInstance *) {
	return true;
}

static GDExtensionBool static_is_placeholder_func(StaticInstance *) {
	return false;
}

static GDExtensionScriptLanguagePtr static_get_language_func(StaticInstance *) {
	return LuauLanguage::get_singleton()->_owner;
}

static void static_free_func(StaticInstance *s) {
	s->script->has_static_instance = false;
	delete s;
}

static GDExtensionScriptInstanceInfo3 static_instance_info = [] {
	GDExtensionScriptInstanceInfo3 info = {};
	info.set_func = (GDExtensionScriptInstanceSet)static_set_func;
	info.get_func = (GDExtensionScriptInstanceGet)static_get_func;
	info.has_method_func = (GDExtensionScriptInstanceHasMethod)static_has_method_func;
	info.call_func = (GDExtensionScriptInstanceCall)static_call_func;
	info.get_owner_func = (GDExtensionScriptInstanceGetOwner)static_get_owner_func;
	info.is_placeholder_func = (GDExtensionScriptInstanceIsPlaceholder)static_is_placeholder_func;
	info.refcount_decremented_func = (GDExtensionScriptInstanceRefCountDecremented)static_refcount_decremented_func;
	info.get_language_func = (GDExtensionScriptInstanceGetLanguage)static_get_language_func;
	info.free_func = (GDExtensionScriptInstanceFree)static_free_func;
	return info;
}();

void LuauScript::ensure_static_instance() {
	if (has_static_instance) {
		return;
	}
	StaticInstance *s = new StaticInstance{ this };
	gdextension_interface::object_set_script_instance(_owner, gdextension_interface::script_instance_create3(&static_instance_info, s));
	has_static_instance = true;
}

// ---------------------------------------------------------------- declarations (ADR 0032)

namespace {

// A Godot type name ("float", "Vector2", "Node"…): its Variant type and, for
// objects, the class. False if unknown.
bool parse_type(const String &name, Variant::Type &r_type, StringName &r_class) {
	r_class = StringName();
	if (name == "Variant" || name == "any") {
		r_type = Variant::NIL;
		return true;
	}
	for (int i = 1; i < Variant::VARIANT_MAX; i++) {
		if (Variant::get_type_name((Variant::Type)i) == name) {
			r_type = (Variant::Type)i;
			return true;
		}
	}
	if (ClassDB::class_exists(name)) {
		r_type = Variant::OBJECT;
		r_class = name;
		return true;
	}
	return false;
}

// The zero value of a type (what a declared property without a default has)
Variant zero_value(Variant::Type type) {
	if (type == Variant::NIL || type == Variant::OBJECT) {
		return Variant();
	}
	alignas(Variant) unsigned char bytes[sizeof(Variant)];
	GDExtensionCallError error;
	gdextension_interface::variant_construct((GDExtensionVariantType)type, bytes, nullptr, 0, &error);
	Variant *value = reinterpret_cast<Variant *>(bytes);
	Variant result = *value;
	value->~Variant();
	return result;
}

String field_string(lua_State *L, int table, const char *key) {
	lua_rawgetfield(L, table, key);
	String value = lua_isstring(L, -1) ? String::utf8(lua_tostring(L, -1)) : String();
	lua_pop(L, 1);
	return value;
}

// Joins the values of a Lua sequence with commas ({0, 10, 0.5} -> "0,10,0.5")
String join_sequence(lua_State *L, int index) {
	index = lua_absindex(L, index);
	String joined;
	int n = lua_objlen(L, index);
	for (int i = 1; i <= n; i++) {
		lua_rawgeti(L, index, i);
		if (i > 1) {
			joined += ",";
		}
		joined += lua_type(L, -1) == LUA_TNUMBER ? String::num(lua_tonumber(L, -1)) : String::utf8(lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	return joined;
}

// One property declaration (value at `index`):
//   { type = "float", default = 2.0, get = "get_x", set = "set_x",
//     range = {0, 10, 0.5} | enum = {"A", "B"} | multiline = true |
//     file = "*.png" | dir = true | hint = n, hint_string = "…" }
// or a type name ("float"), or a default value (its type inferred).
LuauScript::PropertyDef parse_property(lua_State *L, const StringName &name, int index, bool exported) {
	index = lua_absindex(L, index);
	LuauScript::PropertyDef p;
	p.name = name;
	p.exported = exported;
	bool has_type = false;
	if (lua_type(L, index) == LUA_TTABLE) {
		String type = field_string(L, index, "type");
		if (!type.is_empty()) {
			has_type = parse_type(type, p.type, p.class_name);
			if (!has_type) {
				UtilityFunctions::push_warning("Unknown type '" + type + "' for property '" + String(name) + "'");
			}
		}
		lua_rawgetfield(L, index, "default");
		if (!lua_isnil(L, -1)) {
			p.default_value = to_variant(L, -1);
			p.has_default = true;
		}
		lua_pop(L, 1);
		p.category = field_string(L, index, "category");
		p.group = field_string(L, index, "group");
		p.subgroup = field_string(L, index, "subgroup");
		p.getter = field_string(L, index, "get");
		p.setter = field_string(L, index, "set");
		p.accessors = !p.getter.is_empty() || !p.setter.is_empty();
		lua_rawgetfield(L, index, "range");
		if (lua_istable(L, -1)) {
			p.hint = PROPERTY_HINT_RANGE;
			p.hint_string = join_sequence(L, -1);
		}
		lua_pop(L, 1);
		lua_rawgetfield(L, index, "enum");
		if (lua_istable(L, -1)) {
			p.hint = PROPERTY_HINT_ENUM;
			p.hint_string = join_sequence(L, -1);
		}
		lua_pop(L, 1);
		lua_rawgetfield(L, index, "multiline");
		if (lua_toboolean(L, -1)) {
			p.hint = PROPERTY_HINT_MULTILINE_TEXT;
		}
		lua_pop(L, 1);
		String file = field_string(L, index, "file");
		if (!file.is_empty()) {
			p.hint = PROPERTY_HINT_FILE;
			p.hint_string = file;
		}
		lua_rawgetfield(L, index, "dir");
		if (lua_toboolean(L, -1)) {
			p.hint = PROPERTY_HINT_DIR;
		}
		lua_pop(L, 1);
		lua_rawgetfield(L, index, "hint");
		if (lua_type(L, -1) == LUA_TNUMBER) {
			p.hint = (PropertyHint)(int)lua_tonumber(L, -1);
			p.hint_string = field_string(L, index, "hint_string");
		}
		lua_pop(L, 1);
	} else if (lua_type(L, index) == LUA_TSTRING && parse_type(String::utf8(lua_tostring(L, index)), p.type, p.class_name)) {
		has_type = true;
	} else {
		p.default_value = to_variant(L, index);
		p.has_default = true;
	}
	if (!has_type && p.has_default) {
		p.type = p.default_value.get_type();
	}
	if (p.has_default) {
		p.default_value = coerce_to_type(p.default_value, p.type);
	} else {
		p.default_value = zero_value(p.type);
	}
	if (p.type == Variant::OBJECT && p.hint == PROPERTY_HINT_NONE && !p.class_name.is_empty()) {
		if (ClassDB::is_parent_class(p.class_name, "Node")) {
			p.hint = PROPERTY_HINT_NODE_TYPE;
			p.hint_string = p.class_name;
		} else if (ClassDB::is_parent_class(p.class_name, "Resource")) {
			p.hint = PROPERTY_HINT_RESOURCE_TYPE;
			p.hint_string = p.class_name;
		}
	}
	return p;
}

// Iterates a declaration table: a sequence of { name = …, … } entries or bare
// names (kept in order), or a map name -> declaration (sorted by name).
// Calls f(name, index of the declaration), and header(index) for a sequence
// entry without a name.
template <typename F, typename H>
void each_declaration(lua_State *L, int index, F &&f, H &&header) {
	index = lua_absindex(L, index);
	int n = lua_objlen(L, index);
	if (n > 0) {
		for (int i = 1; i <= n; i++) {
			lua_rawgeti(L, index, i);
			int entry = lua_gettop(L);
			if (lua_istable(L, entry)) {
				lua_rawgetfield(L, entry, "name");
				if (lua_isstring(L, -1)) {
					StringName name(lua_tostring(L, -1));
					lua_pop(L, 1);
					f(name, entry);
				} else {
					lua_pop(L, 1);
					header(entry);
				}
			} else if (lua_isstring(L, entry)) {
				// A bare name (signals = {"died"})
				StringName name(lua_tostring(L, entry));
				lua_newtable(L);
				f(name, lua_gettop(L));
			}
			lua_settop(L, entry - 1);
		}
		return;
	}
	std::vector<String> names;
	lua_pushnil(L);
	while (lua_next(L, index)) {
		if (lua_type(L, -2) == LUA_TSTRING) {
			names.push_back(String::utf8(lua_tostring(L, -2)));
		}
		lua_pop(L, 1);
	}
	std::sort(names.begin(), names.end(), [](const String &a, const String &b) { return a < b; });
	for (const String &name : names) {
		CharString key = name.utf8();
		lua_rawgetfield(L, index, key.get_data());
		int entry = lua_gettop(L);
		f(StringName(name), entry);
		lua_settop(L, entry - 1);
	}
}

template <typename F>
void each_declaration(lua_State *L, int index, F &&f) {
	each_declaration(L, index, f, [](int) {});
}

bool is_constant_name(const char *name) {
	if (!(name[0] >= 'A' && name[0] <= 'Z')) {
		return false;
	}
	for (const char *c = name; *c; c++) {
		if (!((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_')) {
			return false;
		}
	}
	return true;
}

} // namespace

// ---------------------------------------------------------------- script

LuauScript::Routes *LuauScript::routes_for(const void *class_info) {
	if (Routes **existing = routes.getptr(class_info)) {
		return *existing;
	}
	return routes.insert(class_info, new Routes())->value;
}

static void clear_methods(LuauScript *script, lua_State *L) {
	if (L) {
		for (auto &[name, method] : script->methods) {
			lua_unref(L, method.ref);
			if (method.defaults_ref != LUA_NOREF) {
				lua_unref(L, method.defaults_ref);
			}
		}
	}
	script->methods.clear();
	script->methods_by_ptr.clear();
	script->get_method = script->set_method = script->notification_method = nullptr;
	script->property_list_method = script->validate_property_method = script->to_string_method = nullptr;
	script->can_revert_method = script->get_revert_method = nullptr;
}

static void unregister_class_table(LuauScript *script) {
	for (auto it = scripts_by_class.begin(); it != scripts_by_class.end(); ++it) {
		if (it->value == script) {
			scripts_by_class.erase(it->key);
			return;
		}
	}
}

LuauScript::~LuauScript() {
	for (auto &[cls, table] : routes) {
		delete table;
	}
	if (LuauScript **registered = scripts_by_path.getptr(get_path())) {
		if (*registered == this) {
			scripts_by_path.erase(get_path());
		}
	}
	unregister_class_table(this);
	lua_State *L = state();
	if (L == nullptr) {
		return;
	}
	clear_methods(this, L);
	if (class_ref != LUA_NOREF) {
		lua_unref(L, class_ref);
	}
}

void *LuauScript::_instance_create(Object *p_for_object) const {
	if (state() == nullptr) {
		return nullptr;
	}
	watch_main_loop();
	Instance *instance = new Instance();
	instance->script = Ref<LuauScript>(const_cast<LuauScript *>(this));
	instance->owner = p_for_object->_owner;
	instance->owner_id = p_for_object->get_instance_id();
	instance->cls = class_info_of(instance->owner);
	instance->routes = const_cast<LuauScript *>(this)->routes_for(instance->cls);
	create_self_table(state(), instance);
	instances.insert(instance->owner, instance);
	return gdextension_interface::script_instance_create3(&instance_info, instance);
}

bool LuauScript::_instance_has(Object *p_object) const {
	Instance **instance = instances.getptr(p_object->_owner);
	return instance && (*instance)->script.ptr() == this;
}

void LuauScript::retry_pending_base() const {
	if (!valid && pending_base != StringName() && ProjectSettings::get_singleton()->get_global_class_list().size() > 0) {
		const_cast<LuauScript *>(this)->_reload(true);
	}
}

bool LuauScript::_can_instantiate() const {
	retry_pending_base();
	// In the editor only tool scripts run; others get placeholders
	return valid && (tool || !Engine::get_singleton()->is_editor_hint());
}

bool LuauScript::_inherits_script(const Ref<Script> &p_script) const {
	for (const LuauScript *s = this; s; s = s->base_script.ptr()) {
		if (s == p_script.ptr()) {
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------- reloading
// (docs/adr/0036)

// Moves the contents of the table at `from` (a module's new version) into
// the one at `into` (the version everyone holds). `from` then forwards to
// `into`, so the new code's own references to its table (`M.count += 1`
// inside M's functions) read and write the same table as everyone else.
static void merge_into(lua_State *L, int into, int from) {
	bool frozen = lua_getreadonly(L, into);
	lua_setreadonly(L, into, false);
	// Fields the new version dropped
	lua_pushnil(L);
	while (lua_next(L, into)) {
		lua_pop(L, 1);
		lua_pushvalue(L, -1);
		if (lua_rawget(L, from) == LUA_TNIL) {
			lua_pushvalue(L, -2);
			lua_pushnil(L);
			lua_rawset(L, into);  // allowed while traversing: the key exists
		}
		lua_pop(L, 1);
	}
	lua_pushnil(L);
	while (lua_next(L, from)) {
		lua_pushvalue(L, -2);
		lua_insert(L, -2);
		lua_rawset(L, into);
	}
	if (lua_getmetatable(L, from)) {
		lua_setmetatable(L, into);
	} else {
		lua_pushnil(L);
		lua_setmetatable(L, into);
	}
	lua_setreadonly(L, into, frozen || lua_getreadonly(L, from));
	lua_setreadonly(L, from, false);
	lua_cleartable(L, from);
	lua_createtable(L, 0, 2);
	lua_pushvalue(L, into);
	lua_setfield(L, -2, "__index");
	lua_pushvalue(L, into);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, from);
}

// Live instances of a reloaded script keep their fields. Their caches of
// script functions are emptied, and properties the new version added get
// their defaults.
static void refresh_instances(lua_State *L, LuauScript *script) {
	for (const KeyValue<GDExtensionObjectPtr, Instance *> &E : instances) {
		Instance *instance = E.value;
		if (instance->script.ptr() != script) {
			continue;
		}
		lua_getref(L, instance->cache_ref);
		lua_cleartable(L, -1);
		lua_pop(L, 1);
		lua_getref(L, instance->table_ref);
		for (const LuauScript::PropertyDef &property : script->properties) {
			if (property.has_accessors() || property.default_value.get_type() == Variant::NIL) {
				continue;
			}
			push_string_name(L, property.name);
			if (lua_rawget(L, -2) == LUA_TNIL) {
				push_string_name(L, property.name);
				push_variant(L, property.default_value);
				lua_rawset(L, -4);
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
}

// Scripts that required `path` ran with its old version: run them again
static void reload_dependents(const String &path) {
	HashSet<String> *users = dependents.getptr(path);
	if (users == nullptr) {
		return;
	}
	Vector<String> list;
	for (const String &user : *users) {
		list.push_back(user);
	}
	for (const String &user : list) {
		if (LuauScript **script = scripts_by_path.getptr(user)) {
			(*script)->_reload(true);
		}
	}
}

Error LuauScript::_reload(bool p_keep_state) {
	lua_State *L = ensure_state();
	if (L == nullptr) {
		return ERR_UNAVAILABLE;
	}
	String path = get_path();
	if (reloading_paths.has(path)) {
		return OK;  // already reloading higher up (dependents of dependents)
	}
	if (path.begins_with("res://")) {
		scripts_by_path[path] = this;  // watched even if this load fails
	}

	// Run the new code first: if it fails, the loaded version stays
	String code = source;
	if (path.get_extension() == "fnl") {
		String lua;
		if (!compile_fennel(L, source, path, lua)) {
			report_message(lua);
			return ERR_PARSE_ERROR;
		}
		code = lua;
	}
	if (!load_chunk(L, code, "@" + path)) {
		report_message(String::utf8(lua_tostring(L, -1)));  // a syntax error
		lua_pop(L, 1);
		return ERR_PARSE_ERROR;
	}
	loading.push_back(this);
	bool ran = lua_pcall(L, 0, 1, ERROR_HANDLER) == LUA_OK;
	loading.pop_back();
	if (!ran) {
		lua_pop(L, 1);  // reported by the handler
		return ERR_PARSE_ERROR;
	}
	bool reload = class_ref != LUA_NOREF;
	reloading_paths.insert(path);

	// A module that isn't a table (a function, a value): only require uses it
	if (!lua_istable(L, -1)) {
		if (class_ref != LUA_NOREF) {
			unregister_class_table(this);
			lua_unref(L, class_ref);
		}
		class_ref = lua_ref(L, -1);
		lua_pop(L, 1);
		valid = false;
		if (reload) {
			reload_dependents(path);
		}
		reloading_paths.erase(path);
		return OK;
	}

	// Reloading keeps the table everyone already holds (other modules'
	// locals, derived scripts' `extends`): the new one's contents move into it
	if (reload) {
		lua_getref(L, class_ref);
		if (lua_istable(L, -1)) {
			merge_into(L, lua_gettop(L), lua_gettop(L) - 1);
			lua_remove(L, -2);  // the old table takes the new one's place
		} else {
			lua_pop(L, 1);
		}
	}
	int table = lua_gettop(L);

	valid = false;
	clear_methods(this, L);
	properties.clear();
	listed.clear();
	property_index.clear();
	signals.clear();
	signal_index.clear();
	constants.clear();
	base_script.unref();
	global_name = StringName();
	tool = false;
	icon_path = String();
	for (auto &[cls, table] : routes) {
		table->by_atom.clear();  // instances keep their pointers
	}

	// Read first: the editor's scan registers the class even when its base
	// isn't known yet
	global_name = field_string(L, table, "class_name");
	icon_path = field_string(L, table, "icon");
	lua_rawgetfield(L, table, "tool");
	tool = lua_toboolean(L, -1);
	lua_pop(L, 1);
	pending_base = StringName();

	// extends: a native class name, a script's class table, or a Luau
	// script's class_name (docs/adr/0049), required by its path
	lua_rawgetfield(L, table, "extends");
	if (lua_isstring(L, -1) && !ClassDB::class_exists(StringName(lua_tostring(L, -1)))) {
		String name = String::utf8(lua_tostring(L, -1));
		String base_path, language;
		TypedArray<Dictionary> classes = ProjectSettings::get_singleton()->get_global_class_list();
		for (int i = 0; i < classes.size(); i++) {
			Dictionary entry = classes[i];
			if (String(entry["class"]) == name) {
				base_path = entry["path"];
				language = entry["language"];
				break;
			}
		}
		if (base_path.is_empty() && Engine::get_singleton()->is_editor_hint()) {
			// Not registered yet, or not at all: the scan registers this class
			// with that base, and using the script loads it again
			pending_base = StringName(name);
			UtilityFunctions::print_verbose("Luau: '" + name + "' isn't a known class yet: " + get_path());
			reloading_paths.erase(path);
			lua_settop(L, table - 1);
			return ERR_PARSE_ERROR;
		}
		if (base_path.is_empty() || language != "Luau") {
			UtilityFunctions::push_error(base_path.is_empty()
							? "'extends': no native class or class_name '" + name + "': " + get_path()
							: "'extends': " + name + " is a " + language + " class; a Luau script extends native classes and Luau scripts: " + get_path());
			reloading_paths.erase(path);
			lua_settop(L, table - 1);
			return ERR_PARSE_ERROR;
		}
		lua_pop(L, 1);
		lua_getglobal(L, "require");
		lua_pushstring(L, base_path.utf8().get_data());
		loading.push_back(this);  // reloading the base reloads this script
		bool required = lua_pcall(L, 1, 1, ERROR_HANDLER) == LUA_OK;
		loading.pop_back();
		if (!required) {
			reloading_paths.erase(path);
			lua_settop(L, table - 1);  // reported by the handler
			return ERR_PARSE_ERROR;
		}
	}
	if (lua_istable(L, -1)) {
		LuauScript **base = scripts_by_class.getptr(lua_topointer(L, -1));
		if (base == nullptr) {
			UtilityFunctions::push_error("'extends' must be a native class name or a required script: " + get_path());
			reloading_paths.erase(path);
			lua_settop(L, table - 1);
			return ERR_PARSE_ERROR;
		}
		base_script = Ref<LuauScript>(*base);
		base_type = base_script->base_type;
		// Inherited functions and fields through the class table
		lua_newtable(L);
		lua_pushvalue(L, -2);
		lua_setfield(L, -2, "__index");
		lua_setmetatable(L, table);
	} else {
		base_type = lua_isstring(L, -1) ? StringName(lua_tostring(L, -1)) : StringName("RefCounted");
	}
	lua_pop(L, 1);

	// Inherited declarations first
	if (base_script.is_valid()) {
		properties = base_script->properties;
		signals = base_script->signals;
		constants = base_script->constants.duplicate();
		for (const auto &[name, method] : base_script->methods) {
			MethodDef copy = method;
			lua_getref(L, method.ref);
			copy.ref = lua_ref(L, -1);
			lua_pop(L, 1);
			if (method.defaults_ref != LUA_NOREF) {
				lua_getref(L, method.defaults_ref);
				copy.defaults_ref = lua_ref(L, -1);
				lua_pop(L, 1);
			}
			methods.insert(name, copy);
		}
	}

	// Own methods, with their parameter counts (without self), and constants
	lua_pushnil(L);
	while (lua_next(L, table)) {
		if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TFUNCTION) {
			MethodDef method;
			method.name = StringName(lua_tostring(L, -2));
			lua_Debug ar;
			if (lua_getinfo(L, -1, "a", &ar)) {
				method.nparams = ar.nparams > 0 ? ar.nparams - 1 : 0;
				method.vararg = ar.isvararg;
			}
			method.ref = lua_ref(L, -1);
			if (MethodDef *inherited = methods.getptr(method.name)) {
				lua_unref(L, inherited->ref);
				if (inherited->defaults_ref != LUA_NOREF) {
					lua_unref(L, inherited->defaults_ref);
				}
				methods.erase(method.name);
			}
			methods.insert(method.name, method);
		} else if (lua_type(L, -2) == LUA_TSTRING && is_constant_name(lua_tostring(L, -2))) {
			constants[String::utf8(lua_tostring(L, -2))] = to_variant(L, -1);
		}
		lua_pop(L, 1);
	}

	// defaults = { method = { values for the rightmost parameters } }
	lua_rawgetfield(L, table, "defaults");
	if (lua_istable(L, -1)) {
		lua_pushnil(L);
		while (lua_next(L, -2)) {
			if (lua_type(L, -2) == LUA_TSTRING) {
				if (MethodDef *method = methods.getptr(StringName(lua_tostring(L, -2)))) {
					Variant values = to_variant(L, -1);
					method->defaults = values.get_type() == Variant::ARRAY ? (Array)values : Array();
					if (method->defaults_ref != LUA_NOREF) {
						lua_unref(L, method->defaults_ref);
						method->defaults_ref = LUA_NOREF;
					}
					if (!method->defaults.is_empty()) {
						method->defaults_ref = lua_ref(L, -1);
					}
				}
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);

	// types = { method = { args = { "int", … }, ret = "float" } }
	lua_rawgetfield(L, table, "types");
	if (lua_istable(L, -1)) {
		lua_pushnil(L);
		while (lua_next(L, -2)) {
			if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
				if (MethodDef *method = methods.getptr(StringName(lua_tostring(L, -2)))) {
					int decl = lua_gettop(L);
					StringName ignored;
					String ret = field_string(L, decl, "ret");
					if (!ret.is_empty() && parse_type(ret, method->ret_type, ignored)) {
						method->has_ret_type = method->ret_type != Variant::NIL;
					}
					lua_rawgetfield(L, decl, "args");
					if (lua_istable(L, -1)) {
						int n = lua_objlen(L, -1);
						method->arg_types.clear();
						for (int i = 1; i <= n; i++) {
							lua_rawgeti(L, -1, i);
							Variant::Type type = Variant::NIL;
							if (lua_isstring(L, -1)) {
								parse_type(String::utf8(lua_tostring(L, -1)), type, ignored);
							}
							method->arg_types.push_back(type);
							lua_pop(L, 1);
						}
					}
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);

	// static = { "make", … }: functions called without self
	lua_rawgetfield(L, table, "static");
	if (lua_istable(L, -1)) {
		each_declaration(L, -1, [&](const StringName &name, int) {
			if (MethodDef *method = methods.getptr(name)) {
				method->is_static = true;
				lua_getref(L, method->ref);
				lua_Debug ar;
				if (lua_getinfo(L, -1, "a", &ar)) {
					method->nparams = ar.nparams;  // no self to leave out
				}
				lua_pop(L, 1);
			}
		});
	}
	lua_pop(L, 1);

	// exports (inspector, saved) and properties (script variables). Exports
	// can be in inspector sections (docs/adr/0051): a property's own
	// category/group/subgroup, or, in a sequence, header entries such as
	// { group = "Movement" } for the entries after them.
	for (int pass = 0; pass < 2; pass++) {
		lua_rawgetfield(L, table, pass == 0 ? "exports" : "properties");
		if (lua_istable(L, -1)) {
			bool sequence = lua_objlen(L, -1) > 0;
			Vector<PropertyDef> declared;
			String category, group, subgroup;
			each_declaration(L, -1, [&](const StringName &name, int entry) {
				PropertyDef property = parse_property(L, name, entry, pass == 0);
				if (property.category.is_empty() && property.group.is_empty() && property.subgroup.is_empty()) {
					property.category = category;
					property.group = group;
					property.subgroup = subgroup;
				}
				declared.push_back(property);
			}, [&](int entry) {
				String c = field_string(L, entry, "category"), g = field_string(L, entry, "group"), s = field_string(L, entry, "subgroup");
				lua_rawgetfield(L, entry, "category");
				bool has_category = !lua_isnil(L, -1);
				lua_rawgetfield(L, entry, "group");
				bool has_group = !lua_isnil(L, -1);
				lua_rawgetfield(L, entry, "subgroup");
				bool has_subgroup = !lua_isnil(L, -1);
				lua_pop(L, 3);
				if (has_category) {
					category = c;
					group = subgroup = String();
				}
				if (has_group) {
					group = g;
					subgroup = String();
				}
				if (has_subgroup) {
					subgroup = s;
				}
			});
			if (!sequence) {
				// A map's names are sorted; keep each section together (unsectioned first)
				std::stable_sort(declared.ptrw(), declared.ptrw() + declared.size(), [](const PropertyDef &a, const PropertyDef &b) {
					if (a.category != b.category) return a.category < b.category;
					if (a.group != b.group) return a.group < b.group;
					return a.subgroup < b.subgroup;
				});
			}
			for (const PropertyDef &property : declared) {
				for (int i = 0; i < properties.size(); i++) {
					if (properties[i].name == property.name) {
						properties.remove_at(i);  // redeclared: the latest wins
						break;
					}
				}
				properties.push_back(property);
			}
		}
		lua_pop(L, 1);
	}

	// signals = { name = { "arg", "arg: type" }, … } or { "name", … }
	lua_rawgetfield(L, table, "signals");
	if (lua_istable(L, -1)) {
		each_declaration(L, -1, [&](const StringName &name, int entry) {
			SignalDef signal;
			signal.name = name;
			int n = lua_istable(L, entry) ? lua_objlen(L, entry) : 0;
			for (int i = 1; i <= n; i++) {
				lua_rawgeti(L, entry, i);
				String arg = lua_isstring(L, -1) ? String::utf8(lua_tostring(L, -1)) : String();
				lua_pop(L, 1);
				Variant::Type type = Variant::NIL;
				int colon = arg.find(":");
				if (colon >= 0) {
					StringName ignored;
					parse_type(arg.substr(colon + 1).strip_edges(), type, ignored);
					arg = arg.substr(0, colon).strip_edges();
				}
				signal.args.push_back(arg);
				signal.arg_types.push_back(type);
			}
			for (int i = 0; i < signals.size(); i++) {
				if (signals[i].name == name) {
					signals.remove_at(i);
					break;
				}
			}
			signals.push_back(signal);
		});
	}
	lua_pop(L, 1);

	for (int i = 0; i < properties.size(); i++) {
		property_index[name_ptr(properties[i].name)] = i;
	}
	// Section headers where an export's category, group or subgroup changes
	// (an empty group after a group ends it, as @export_group(""))
	listed.clear();
	String category, group, subgroup;
	auto header = [&](const String &name, uint32_t usage) {
		PropertyDef h;
		h.name = StringName(name);
		h.header_usage = usage;
		listed.push_back(h);
	};
	for (const PropertyDef &p : properties) {
		if (p.exported) {
			if (p.category != category) {
				category = p.category;
				group = subgroup = String();
				if (!category.is_empty()) {
					header(category, PROPERTY_USAGE_CATEGORY);
				}
			}
			if (p.group != group) {
				group = p.group;
				subgroup = String();
				header(group, PROPERTY_USAGE_GROUP);
			}
			if (p.subgroup != subgroup) {
				subgroup = p.subgroup;
				header(subgroup, PROPERTY_USAGE_SUBGROUP);
			}
		}
		listed.push_back(p);
	}
	for (int i = 0; i < signals.size(); i++) {
		signal_index[signals[i].name] = i;
	}
	for (const auto &[name, method] : methods) {
		methods_by_ptr.insert(name_ptr(name), &method);
	}
	// Accessors resolved once (inherited properties' too: their pointers
	// pointed into the base's methods)
	for (int i = 0; i < properties.size(); i++) {
		PropertyDef &p = properties.write[i];
		p.getter_method = p.getter.is_empty() ? nullptr : methods.getptr(p.getter);
		p.setter_method = p.setter.is_empty() ? nullptr : methods.getptr(p.setter);
	}
	get_method = methods.getptr("_get");
	set_method = methods.getptr("_set");
	notification_method = methods.getptr("_notification");
	property_list_method = methods.getptr("_get_property_list");
	validate_property_method = methods.getptr("_validate_property");
	to_string_method = methods.getptr("_to_string");
	can_revert_method = methods.getptr("_property_can_revert");
	get_revert_method = methods.getptr("_property_get_revert");

	// Register the class table (for scripts extending this one)
	unregister_class_table(this);
	if (class_ref != LUA_NOREF) {
		lua_unref(L, class_ref);
	}
	class_ref = lua_ref(L, table);
	scripts_by_class.insert(lua_topointer(L, table), this);
	lua_settop(L, table - 1);
	valid = true;
	update_placeholders();
	bool has_static = !constants.is_empty();
	for (const KeyValue<StringName, MethodDef> &E : methods) {
		has_static = has_static || E.value.is_static;
	}
	if (has_static) {
		ensure_static_instance();
	}
	if (reload) {
		refresh_instances(L, this);
		reload_dependents(path);
	}
	reloading_paths.erase(path);
	return OK;
}

TypedArray<Dictionary> LuauScript::property_list() const {
	TypedArray<Dictionary> list;
	for (const PropertyDef &p : listed) {
		Dictionary d;
		d["name"] = p.name;
		d["type"] = p.type;
		d["class_name"] = p.class_name;
		d["hint"] = p.hint;
		d["hint_string"] = p.hint_string;
		d["usage"] = property_usage(p);
		list.push_back(d);
	}
	return list;
}

void LuauScript::update_placeholders() {
	if (placeholders.is_empty()) {
		return;
	}
	Array list = property_list();
	Dictionary values;
	for (const PropertyDef &p : properties) {
		if (p.exported) {
			values[p.name] = p.default_value;
		}
	}
	for (void *placeholder : placeholders) {
		gdextension_interface::placeholder_script_instance_update(placeholder, list._native_ptr(), values._native_ptr());
	}
}

void *LuauScript::_placeholder_instance_create(Object *p_for_object) const {
	retry_pending_base();
	LuauScript *self = const_cast<LuauScript *>(this);
	void *placeholder = gdextension_interface::placeholder_script_instance_create(
			LuauLanguage::get_singleton()->_owner, _owner, p_for_object->_owner);
	self->placeholders.insert(placeholder);
	self->update_placeholders();
	return placeholder;
}

bool LuauScript::_has_property_default_value(const StringName &p_property) const {
	return find_property(p_property) != nullptr;
}

Variant LuauScript::_get_property_default_value(const StringName &p_property) const {
	const PropertyDef *property = find_property(p_property);
	return property ? property->default_value : Variant();
}

static Dictionary method_info(const LuauScript::MethodDef &method) {
	Dictionary d;
	d["name"] = method.name;
	Array args;
	for (int i = 0; i < method.nparams; i++) {
		Dictionary arg;
		arg["name"] = "arg" + itos(i + 1);
		Variant::Type type = i < method.arg_types.size() ? method.arg_types[i] : Variant::NIL;
		arg["type"] = type;
		arg["usage"] = type == Variant::NIL ? PROPERTY_USAGE_NIL_IS_VARIANT : PROPERTY_USAGE_DEFAULT;
		args.push_back(arg);
	}
	d["args"] = args;
	d["default_args"] = method.defaults;
	Dictionary ret;
	ret["type"] = method.ret_type;
	// Untyped: any value, not void (GDScript checks this)
	ret["usage"] = method.has_ret_type ? PROPERTY_USAGE_DEFAULT : PROPERTY_USAGE_NIL_IS_VARIANT;
	d["return"] = ret;
	d["flags"] = METHOD_FLAGS_DEFAULT | (method.is_static ? METHOD_FLAG_STATIC : 0);
	return d;
}

Variant LuauScript::_get_script_method_argument_count(const StringName &p_method) const {
	const MethodDef *method = find_method(p_method);
	return method ? Variant(method->nparams) : Variant();
}

Dictionary LuauScript::_get_method_info(const StringName &p_method) const {
	const MethodDef *method = find_method(p_method);
	return method ? method_info(*method) : Dictionary();
}

TypedArray<Dictionary> LuauScript::_get_script_method_list() const {
	TypedArray<Dictionary> list;
	for (const auto &[name, method] : methods) {
		list.push_back(method_info(method));
	}
	return list;
}

TypedArray<Dictionary> LuauScript::_get_script_signal_list() const {
	TypedArray<Dictionary> list;
	for (const SignalDef &signal : signals) {
		Dictionary d;
		d["name"] = signal.name;
		Array args;
		for (int i = 0; i < signal.args.size(); i++) {
			Dictionary arg;
			arg["name"] = signal.args[i];
			arg["type"] = signal.arg_types[i];
			args.push_back(arg);
		}
		d["args"] = args;
		list.push_back(d);
	}
	return list;
}

TypedArray<StringName> LuauScript::_get_members() const {
	TypedArray<StringName> members;
	for (const PropertyDef &p : properties) {
		members.push_back(p.name);
	}
	for (const SignalDef &s : signals) {
		members.push_back(s.name);
	}
	return members;
}

void LuauScript::_bind_methods() {
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "new", &LuauScript::_new);
}

Variant LuauScript::_new(const Variant **, GDExtensionInt, GDExtensionCallError &error) {
	error.error = GDEXTENSION_CALL_OK;
	Variant object = ClassDB::instantiate(base_type);
	if (Object *o = object) {
		o->set_script(Ref<LuauScript>(this));
	}
	return object;
}

ScriptLanguage *LuauScript::_get_language() const {
	return LuauLanguage::get_singleton();
}

// ---------------------------------------------------------------- language

void LuauLanguage::_init() {
	ensure_state();
}

void LuauLanguage::_finish() {
	required_scripts.clear();
	dependents.clear();
	close_state();
}

// ---- Reloading from disk (docs/adr/0036)

// Reads the script's file again and reloads it if the text changed
static bool reload_from_disk(LuauScript *script) {
	String path = script->get_path();
	if (!path.begins_with("res://") || !FileAccess::file_exists(path)) {
		return false;
	}
	script->source_mtime = FileAccess::get_modified_time(path);
	String text = FileAccess::get_file_as_string(path);
	if (text.is_empty() || text == script->source) {
		return false;
	}
	script->source = text;
	Error error = script->_reload(true);
	UtilityFunctions::print_verbose("Luau: reloaded " + path + (error == OK ? "" : " (failed: the previous version stays)"));
	return true;
}

void LuauLanguage::_reload_all_scripts() {
	Vector<LuauScript *> scripts;
	for (const KeyValue<String, LuauScript *> &E : scripts_by_path) {
		scripts.push_back(E.value);
	}
	for (LuauScript *script : scripts) {
		reload_from_disk(script);
	}
}

void LuauLanguage::_reload_scripts(const Array &p_scripts, bool) {
	for (int i = 0; i < p_scripts.size(); i++) {
		Ref<LuauScript> script = p_scripts[i];
		if (script.is_valid()) {
			reload_from_disk(script.ptr());
		}
	}
}

// The editor's call for a tool script it changed: its source may already
// hold the new text (the script editor sets it before saving), so it reloads
// even when the file matches
void LuauLanguage::_reload_tool_script(const Ref<Script> &p_script, bool) {
	Ref<LuauScript> script = p_script;
	if (script.is_valid() && !reload_from_disk(script.ptr())) {
		script->_reload(true);
	}
}

// The file watcher: in a running game (debug builds, not the editor, which
// reloads through the hooks above), loaded scripts are checked twice a
// second and reloaded when their file changes. Modification times have
// one-second resolution, so files changed in the last two seconds are
// compared by content.
static bool watching_files() {
	static int enabled = -1;
	if (enabled < 0) {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		Variant setting = settings->get_setting("luau/hot_reload/watch_files", true);
		enabled = (bool)setting && OS::get_singleton()->is_debug_build() && !Engine::get_singleton()->is_editor_hint();
	}
	return enabled == 1;
}

void LuauLanguage::_frame() {
	if (!watching_files() || state() == nullptr) {
		return;
	}
	static std::chrono::steady_clock::time_point next_poll;
	auto now = std::chrono::steady_clock::now();
	if (now < next_poll) {
		return;
	}
	next_poll = now + std::chrono::milliseconds(500);
	uint64_t wall = (uint64_t)std::time(nullptr);
	Vector<LuauScript *> changed;
	for (const KeyValue<String, LuauScript *> &E : scripts_by_path) {
		uint64_t mtime = FileAccess::get_modified_time(E.key);
		if (mtime != E.value->source_mtime || mtime + 2 >= wall) {
			changed.push_back(E.value);
		}
	}
	for (LuauScript *script : changed) {
		reload_from_disk(script);
	}
}

PackedStringArray LuauLanguage::_get_reserved_words() const {
	return PackedStringArray({ "and", "break", "continue", "do", "else", "elseif", "end", "export", "false", "for",
			"function", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "type", "typeof",
			"until", "while" });
}

Ref<Script> LuauLanguage::_make_template(const String &, const String &, const String &p_base_class_name) const {
	Ref<LuauScript> script;
	script.instantiate();
	script->source = "local Script = { extends = \"" + p_base_class_name + "\" }\n\nfunction Script:_ready()\nend\n\nreturn Script\n";
	return script;
}

// ---------------------------------------------------------------- validation (docs/adr/0047)
//
// The script editor validates as you type: syntax errors from Luau's parser
// (every one, with its column), then compile errors, for .luau files and for
// the Lua that Fennel makes from .fnl files (whose lines match the Fennel
// source). Functions assigned to fields at the top level (`function T:f()`,
// `T.f = function`, Fennel's `(fn T.f [])`) are listed for the members panel.

static void add_error(Array &errors, int line, int column, const String &message) {
	Dictionary error;
	error["line"] = line;
	error["column"] = column;
	error["message"] = message.get_slicec('\n', 0).strip_edges();
	errors.push_back(error);
}

// "path:12:3: message" or ":12: message": line, column (left as it was if
// absent) and the message. Returns false when the text has no location.
static bool error_location(const String &text, int &r_line, int &r_column, String &r_message) {
	for (int i = 0; i < text.length(); i++) {
		if (text[i] != ':' || i + 1 >= text.length() || !is_digit(text[i + 1])) {
			continue;
		}
		int end = i + 1;
		while (end < text.length() && is_digit(text[end])) {
			end++;
		}
		if (end >= text.length() || text[end] != ':') {
			continue;
		}
		r_line = text.substr(i + 1, end - i - 1).to_int();
		int rest = end + 1;
		int column_end = rest;
		while (column_end < text.length() && is_digit(text[column_end])) {
			column_end++;
		}
		if (column_end > rest && column_end < text.length() && text[column_end] == ':') {
			r_column = text.substr(rest, column_end - rest).to_int();
			rest = column_end + 1;
		}
		r_message = text.substr(rest).strip_edges();
		return true;
	}
	return false;
}

static void list_functions(Luau::AstStatBlock *root, PackedStringArray &r_functions) {
	auto add = [&](Luau::AstExpr *target, Luau::AstExprFunction *function) {
		if (Luau::AstExprIndexName *name = target->as<Luau::AstExprIndexName>()) {
			r_functions.push_back(String::utf8(name->index.value) + ":" + itos(function->location.begin.line + 1));
		}
	};
	for (Luau::AstStat *stat : root->body) {
		if (Luau::AstStatFunction *function = stat->as<Luau::AstStatFunction>()) {
			add(function->name, function->func);
		} else if (Luau::AstStatAssign *assign = stat->as<Luau::AstStatAssign>()) {
			for (size_t i = 0; i < assign->vars.size && i < assign->values.size; i++) {
				if (Luau::AstExprFunction *value = assign->values.data[i]->as<Luau::AstExprFunction>()) {
					add(assign->vars.data[i], value);
				}
			}
		}
	}
}

// Validates Luau source: errors into r_errors, top-level functions into
// r_functions
static void validate_luau(const String &source, Array &r_errors, PackedStringArray &r_functions) {
	CharString code = source.utf8();
	Luau::Allocator allocator;
	Luau::AstNameTable names(allocator);
	Luau::ParseResult parsed = Luau::Parser::parse(code.get_data(), code.length(), names, allocator);
	for (const Luau::ParseError &error : parsed.errors) {
		const Luau::Location &at = error.getLocation();
		add_error(r_errors, at.begin.line + 1, at.begin.column + 1, String::utf8(error.getMessage().c_str()));
	}
	if (!parsed.errors.empty()) {
		return;
	}
	list_functions(parsed.root, r_functions);
	String error = compile_error(source);
	if (!error.is_empty()) {
		int line = 1, column = 1;
		String message = error;
		error_location(error, line, column, message);
		add_error(r_errors, line, column, message);
	}
}

Dictionary LuauLanguage::validate_script(const String &p_source, const String &p_path) const {
	Array errors;
	PackedStringArray functions;
	if (p_path.get_extension() == "fnl") {
		String lua;
		if (!compile_fennel(ensure_state(), p_source, p_path, lua)) {
			int line = 1, column = 0;
			String message = lua;
			error_location(lua, line, column, message);
			add_error(errors, line, column + 1, message);  // Fennel counts columns from 0
		} else {
			Array lua_errors;
			validate_luau(lua, lua_errors, functions);
			for (int i = 0; i < lua_errors.size(); i++) {
				Dictionary error = lua_errors[i];  // Fennel made Lua that doesn't compile: say so
				error["message"] = "in the Lua compiled from Fennel: " + String(error["message"]);
				errors.push_back(error);
			}
		}
	} else {
		validate_luau(p_source, errors, functions);
	}
	Dictionary result;
	result["valid"] = errors.is_empty();
	result["errors"] = errors;
	result["functions"] = functions;
	return result;
}

Dictionary LuauLanguage::_validate(const String &p_script, const String &p_path, bool, bool, bool, bool) const {
	return validate_script(p_script, p_path);
}

void LuauLanguage::_bind_methods() {
	ClassDB::bind_method(D_METHOD("validate_script", "source", "path"), &LuauLanguage::validate_script);
}

Object *LuauLanguage::_create_script() const {
	return memnew(LuauScript);
}

Dictionary LuauLanguage::_get_global_class_name(const String &p_path) const {
	Dictionary result;
	Ref<LuauScript> script = ResourceLoader::get_singleton()->load(p_path);
	if (script.is_valid()) {
		script->retry_pending_base();
	}
	bool pending = script.is_valid() && script->pending_base != StringName();
	if (script.is_null() || (!script->valid && !pending) || script->global_name.is_empty()) {
		return result;
	}
	result["name"] = script->global_name;
	if (pending) {
		result["base_type"] = script->pending_base;  // registered before its base (docs/adr/0049)
	} else {
		result["base_type"] = script->base_script.is_valid() && !script->base_script->global_name.is_empty()
				? script->base_script->global_name
				: script->base_type;
	}
	result["icon_path"] = script->icon_path;
	result["is_abstract"] = false;
	result["is_tool"] = script->tool;
	return result;
}

// ---------------------------------------------------------------- saver and loader

bool LuauSaver::_recognize(const Ref<Resource> &p_resource) const {
	return Ref<LuauScript>(p_resource).is_valid();
}

PackedStringArray LuauSaver::_get_recognized_extensions(const Ref<Resource> &p_resource) const {
	return _recognize(p_resource) ? PackedStringArray({ "luau", "fnl" }) : PackedStringArray();
}

Error LuauSaver::_save(const Ref<Resource> &p_resource, const String &p_path, uint32_t) {
	Ref<LuauScript> script = p_resource;
	ERR_FAIL_COND_V(script.is_null(), ERR_INVALID_PARAMETER);
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(file.is_null(), FileAccess::get_open_error(), "Can't save " + p_path);
	file->store_string(script->_get_source_code());
	file->close();
	if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF) {
		return ERR_CANT_CREATE;
	}
	// Running instances (tool scripts in the editor, or the game) take the
	// saved version, keeping their state; a failed load keeps the old one
	if (script->get_path() == p_path || script->get_path().is_empty()) {
		script->source_mtime = FileAccess::get_modified_time(p_path);
		script->_reload(true);
	}
	return OK;
}

Variant LuauLoader::_load(const String &p_path, const String &p_original_path, bool, int32_t) const {
	Ref<LuauScript> script;
	script.instantiate();
	script->set_path(p_original_path.is_empty() ? p_path : p_original_path);
	script->source = FileAccess::get_file_as_string(p_path);
	script->source_mtime = FileAccess::get_modified_time(p_path);
	script->_reload(false);
	return script;
}

} // namespace luau
