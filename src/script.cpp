#include "script.h"

#include "api.h"
#include "internal.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <luacode.h>
#include <lualib.h>

#include <algorithm>
#include <vector>

namespace luau {

LuauLanguage *LuauLanguage::singleton = nullptr;

// Scripts by their class table, for `extends = require("res://…")`
static HashMap<const void *, LuauScript *> scripts_by_class;

// Scripts loaded through require: kept loaded while the Luau state lives, as
// package.loaded keeps modules (a script used as a base must outlive the
// require call)
static HashMap<String, Ref<LuauScript>> required_scripts;

// require("res://…") of a script: its class table (registered as
// __require_script, used by the prelude's require)
static int require_script(lua_State *L) {
	String path = String::utf8(luaL_checkstring(L, 1));
	Ref<LuauScript> script = ResourceLoader::get_singleton()->load(path);
	if (script.is_null() || !script->valid) {
		luaL_error(L, "can't load script '%s'", lua_tostring(L, 1));
	}
	required_scripts[path] = script;
	lua_getref(L, script->class_ref);
	return 1;
}

// Lua lets go of everything when the main loop is deleted. Godot finishes
// script languages in no fixed order, and a value from another language that
// Lua still holds (a GDScript lambda) must be released before that language
// finishes. After this the state stays closed.
static bool main_loop_gone = false;

static void main_loop_freed(void *, void *, void *) {
	main_loop_gone = true;
	required_scripts.clear();
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
		lua_pushcfunction(state(), require_script, "__require_script");
		lua_setglobal(state(), "__require_script");
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
	if (lua_pcall(L, nargs + 1, 1, 0) != LUA_OK) {
		UtilityFunctions::push_error(String::utf8(lua_tostring(L, -1)));
		lua_pop(L, 1);
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
			const LuauScript::MethodDef *getter = property->getter.is_empty() ? nullptr : instance->script->find_method(property->getter);
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
			const LuauScript::MethodDef *setter = property->setter.is_empty() ? nullptr : instance->script->find_method(property->setter);
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
	Variant::Type have = value.get_type();
	if (type == Variant::NIL || have == type || have == Variant::NIL) {
		push_variant(L, value);
	} else if (type == Variant::FLOAT && have == Variant::INT) {
		lua_pushnumber(L, (double)(int64_t)value);
	} else if (type == Variant::INT && have == Variant::FLOAT) {
		lua_pushnumber(L, (double)(int64_t)(double)value);
	} else {
		push_variant(L, coerce_to_type(value, type));
	}
}

static void to_typed(lua_State *L, int index, Variant::Type type, Variant *r_value) {
	if (lua_type(L, index) == LUA_TNUMBER && type == Variant::FLOAT) {
		*r_value = lua_tonumber(L, index);
	} else if (lua_type(L, index) == LUA_TNUMBER && type == Variant::INT) {
		*r_value = (int64_t)lua_tonumber(L, index);
	} else {
		*r_value = coerce_to_type(to_variant(L, index), type);
	}
}

static GDExtensionBool set_func(Instance *instance, GDExtensionConstStringNamePtr p_name, GDExtensionConstVariantPtr p_value) {
	lua_State *L = state();
	const StringName &name = *(const StringName *)p_name;
	const Variant &value = *(const Variant *)p_value;
	const LuauScript *script = instance->script.ptr();
	if (const LuauScript::PropertyDef *property = script->find_property(name)) {
		if (property->has_accessors()) {
			const LuauScript::MethodDef *setter = property->setter.is_empty() ? nullptr : script->find_method(property->setter);
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
			const LuauScript::MethodDef *getter = property->getter.is_empty() ? nullptr : script->find_method(property->getter);
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
		*(Variant *)r_ret = to_variant(L, -1);
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
				*(Variant *)r_ret = to_variant(L, -1);
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
	return property.exported ? (PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SCRIPT_VARIABLE) : PROPERTY_USAGE_SCRIPT_VARIABLE;
}

static const GDExtensionPropertyInfo *get_property_list_func(Instance *instance, uint32_t *r_count) {
	const Vector<LuauScript::PropertyDef> &properties = instance->script->properties;
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
			arg.usage = PROPERTY_USAGE_DEFAULT;
		}
		GDExtensionMethodInfo &info = storage->infos[i];
		info = {};
		info.name = storage->names[i]._native_ptr();
		info.return_value.type = (GDExtensionVariantType)method.ret_type;
		info.return_value.name = storage->empty._native_ptr();
		info.return_value.class_name = storage->empty._native_ptr();
		info.return_value.hint_string = storage->empty_hint._native_ptr();
		info.return_value.usage = method.has_ret_type ? PROPERTY_USAGE_DEFAULT : PROPERTY_USAGE_NIL_IS_VARIANT;
		info.flags = GDEXTENSION_METHOD_FLAGS_DEFAULT;
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
	lua_getref(L, instance->table_ref);
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
	if (run_thread(co, pushed + 1) != LUA_OK) {
		return;  // suspended in await (the caller gets nil), or failed
	}
	if (lua_gettop(L) > 0) {
		if (method->has_ret_type) {
			to_typed(L, 1, method->ret_type, (Variant *)r_ret);
		} else {
			to_variant_into_nil(L, 1, (Variant *)r_ret);
		}
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
		p.getter = field_string(L, index, "get");
		p.setter = field_string(L, index, "set");
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
// Calls f(name, index of the declaration).
template <typename F>
void each_declaration(lua_State *L, int index, F &&f) {
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

bool LuauScript::_can_instantiate() const {
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

Error LuauScript::_reload(bool p_keep_state) {
	lua_State *L = ensure_state();
	if (L == nullptr) {
		return ERR_UNAVAILABLE;
	}
	valid = false;
	clear_methods(this, L);
	properties.clear();
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

	String code = source;
	if (get_path().get_extension() == "fnl") {
		String lua;
		if (!compile_fennel(L, source, get_path(), lua)) {
			UtilityFunctions::push_error(lua);
			return ERR_PARSE_ERROR;
		}
		code = lua;
	}
	if (!load_chunk(L, code, "@" + get_path()) || lua_pcall(L, 0, 1, 0) != LUA_OK) {
		UtilityFunctions::push_error(String::utf8(lua_tostring(L, -1)));
		lua_pop(L, 1);
		return ERR_PARSE_ERROR;
	}
	if (!lua_istable(L, -1)) {
		UtilityFunctions::push_error("Luau script must return a table: " + get_path());
		lua_pop(L, 1);
		return ERR_PARSE_ERROR;
	}
	int table = lua_gettop(L);

	// extends: a native class name, or a script's class table
	lua_rawgetfield(L, table, "extends");
	if (lua_istable(L, -1)) {
		LuauScript **base = scripts_by_class.getptr(lua_topointer(L, -1));
		if (base == nullptr) {
			UtilityFunctions::push_error("'extends' must be a native class name or a required script: " + get_path());
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
	global_name = field_string(L, table, "class_name");
	icon_path = field_string(L, table, "icon");
	lua_rawgetfield(L, table, "tool");
	tool = lua_toboolean(L, -1);
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

	// exports (inspector, saved) and properties (script variables)
	for (int pass = 0; pass < 2; pass++) {
		lua_rawgetfield(L, table, pass == 0 ? "exports" : "properties");
		if (lua_istable(L, -1)) {
			each_declaration(L, -1, [&](const StringName &name, int entry) {
				PropertyDef property = parse_property(L, name, entry, pass == 0);
				for (int i = 0; i < properties.size(); i++) {
					if (properties[i].name == name) {
						properties.remove_at(i);  // redeclared: the latest wins
						break;
					}
				}
				properties.push_back(property);
			});
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
		property_index[properties[i].name] = i;
	}
	for (int i = 0; i < signals.size(); i++) {
		signal_index[signals[i].name] = i;
	}
	for (const auto &[name, method] : methods) {
		methods_by_ptr.insert(name_ptr(name), &method);
	}
	get_method = methods.getptr("_get");
	set_method = methods.getptr("_set");
	notification_method = methods.getptr("_notification");

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
	return OK;
}

TypedArray<Dictionary> LuauScript::property_list() const {
	TypedArray<Dictionary> list;
	for (const PropertyDef &p : properties) {
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
		arg["type"] = i < method.arg_types.size() ? method.arg_types[i] : Variant::NIL;
		args.push_back(arg);
	}
	d["args"] = args;
	d["default_args"] = method.defaults;
	Dictionary ret;
	ret["type"] = method.ret_type;
	d["return"] = ret;
	d["flags"] = METHOD_FLAGS_DEFAULT;
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
	close_state();
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

Dictionary LuauLanguage::_validate(const String &, const String &, bool, bool, bool, bool) const {
	Dictionary result;
	result["valid"] = true;
	return result;
}

Object *LuauLanguage::_create_script() const {
	return memnew(LuauScript);
}

Dictionary LuauLanguage::_get_global_class_name(const String &p_path) const {
	Dictionary result;
	Ref<LuauScript> script = ResourceLoader::get_singleton()->load(p_path);
	if (script.is_null() || !script->valid || script->global_name.is_empty()) {
		return result;
	}
	result["name"] = script->global_name;
	result["base_type"] = script->base_script.is_valid() && !script->base_script->global_name.is_empty()
			? script->base_script->global_name
			: script->base_type;
	result["icon_path"] = script->icon_path;
	result["is_abstract"] = false;
	result["is_tool"] = script->tool;
	return result;
}

// ---------------------------------------------------------------- loader

Variant LuauLoader::_load(const String &p_path, const String &p_original_path, bool, int32_t) const {
	Ref<LuauScript> script;
	script.instantiate();
	script->set_path(p_original_path.is_empty() ? p_path : p_original_path);
	script->source = FileAccess::get_file_as_string(p_path);
	script->_reload(false);
	return script;
}

} // namespace luau
