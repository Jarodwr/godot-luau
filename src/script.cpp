#include "script.h"

#include "api.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <luacode.h>
#include <lualib.h>

namespace luau {

LuauLanguage *LuauLanguage::singleton = nullptr;

// ---------------------------------------------------------------- instances

// `self` for a Luau script instance is a table T (fields). T's metatable M
// sends misses to B (cached script functions and engine method binds); B's
// metatable resolves names not cached yet: engine properties are read through
// their getters on every access, methods are cached in B.
struct Instance {
	Ref<LuauScript> script;
	GDExtensionObjectPtr owner;
	ClassInfo *cls;
	LuauScript::Routes *routes;
	int table_ref = LUA_NOREF;  // T
	int cache_ref = LUA_NOREF;  // B
	int meta_ref = LUA_NOREF;   // M
};

static HashMap<GDExtensionObjectPtr, Instance *> instances;
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

// Whether the script defines a function named like the key at index 2
static bool is_script_function(lua_State *L, Instance *instance) {
	lua_getref(L, instance->script->class_ref);
	lua_pushvalue(L, 2);
	bool is_function = lua_rawget(L, -2) == LUA_TFUNCTION;
	lua_pop(L, 2);
	return is_function;
}

// The route of the string key at index 2 (copied: resolving members can run
// scripts, which may grow the table)
static LuauScript::Route route_of(lua_State *L, Instance *instance, int atom) {
	if (atom < 0) {
		LuauScript::Route route;
		route.kind = is_script_function(L, instance) ? LuauScript::ROUTE_SCRIPT : LuauScript::ROUTE_ENGINE;
		if (route.kind == LuauScript::ROUTE_ENGINE) {
			route.member = &instance->cls->member(StringName(lua_tostring(L, 2)));
		}
		return route;
	}
	std::vector<LuauScript::Route> &by_atom = instance->routes->by_atom;
	if ((size_t)atom >= by_atom.size()) {
		by_atom.resize(atom + 1);
	}
	if (by_atom[atom].kind == LuauScript::ROUTE_UNRESOLVED) {
		LuauScript::Route route;
		if (is_script_function(L, instance)) {
			route.kind = LuauScript::ROUTE_SCRIPT;
		} else {
			route.kind = LuauScript::ROUTE_ENGINE;
			route.member = &instance->cls->member(atom);
		}
		instance->routes->by_atom[atom] = route;
	}
	return instance->routes->by_atom[atom];
}

// B's __index: names neither in T nor cached in B
static int cache_index(lua_State *L) {
	Instance *instance = instance_in(L, 1);
	if (instance == nullptr) {
		luaL_error(L, "attempt to use a freed object");
	}
	if (lua_type(L, 2) != LUA_TSTRING) {
		lua_pushnil(L);
		return 1;
	}
	LuauScript::Route route = route_of(L, instance, string_atom(L, 2));
	if (route.kind == LuauScript::ROUTE_SCRIPT) {
		// Cache the script function in B
		lua_getref(L, instance->script->class_ref);
		lua_pushvalue(L, 2);
		lua_rawget(L, -2);
		lua_pushvalue(L, 2);
		lua_pushvalue(L, -2);
		lua_rawset(L, 1);
		return 1;
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
			push_object_get(L, instance->owner, member.name.is_empty() ? StringName(lua_tostring(L, 2)) : member.name);
			return 1;
	}
}

// T's __newindex: names not in T yet. Engine properties go to their setter;
// anything else becomes a field.
static int self_newindex(lua_State *L) {
	lua_getmetatable(L, 1);
	Instance *instance = instance_in(L, -1);
	lua_pop(L, 1);
	if (instance == nullptr) {
		luaL_error(L, "attempt to use a freed object");
	}
	if (lua_type(L, 2) == LUA_TSTRING) {
		LuauScript::Route route = route_of(L, instance, string_atom(L, 2));
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

	lua_pushlightuserdata(L, (void *)&INSTANCE_KEY);
	lua_pushlightuserdata(L, instance);
	lua_rawset(L, -3);
	lua_rawgetfield(L, LUA_REGISTRYINDEX, "luau.cache_meta");
	lua_setmetatable(L, -2);
	lua_pushvalue(L, -1);
	instance->cache_ref = lua_ref(L, -1);
	lua_pop(L, 1);
	lua_setfield(L, -2, "__index");  // M.__index = B

	lua_pushlightuserdata(L, (void *)&INSTANCE_KEY);
	lua_pushlightuserdata(L, instance);
	lua_rawset(L, -3);
	lua_pushcfunction(L, self_newindex, "__newindex");
	lua_setfield(L, -2, "__newindex");
	instance->meta_ref = lua_ref(L, -1);
	lua_setmetatable(L, -2);  // T's metatable = M

	instance->table_ref = lua_ref(L, -1);
	lua_pop(L, 1);
}

// ---- GDExtensionScriptInstanceInfo3 callbacks

static GDExtensionBool set_func(Instance *instance, GDExtensionConstStringNamePtr name, GDExtensionConstVariantPtr value) {
	lua_State *L = state();
	lua_getref(L, instance->table_ref);
	CharString key = String(*(const StringName *)name).utf8();
	lua_rawgetfield(L, -1, key.get_data());
	bool has = !lua_isnil(L, -1);
	lua_pop(L, 1);
	if (has) {
		push_variant(L, *(const Variant *)value);
		lua_rawsetfield(L, -2, key.get_data());
	}
	lua_pop(L, 1);
	return has;
}

static GDExtensionBool get_func(Instance *instance, GDExtensionConstStringNamePtr name, GDExtensionVariantPtr r_ret) {
	lua_State *L = state();
	lua_getref(L, instance->table_ref);
	CharString key = String(*(const StringName *)name).utf8();
	lua_rawgetfield(L, -1, key.get_data());
	bool has = !lua_isnil(L, -1);
	if (has) {
		*(Variant *)r_ret = to_variant(L, -1);
	}
	lua_pop(L, 2);
	return has;
}

static const GDExtensionPropertyInfo *get_property_list_func(Instance *, uint32_t *r_count) {
	*r_count = 0;
	return nullptr;
}

static void free_property_list_func(Instance *, const GDExtensionPropertyInfo *, uint32_t) {}

static const GDExtensionMethodInfo *get_method_list_func(Instance *, uint32_t *r_count) {
	*r_count = 0;
	return nullptr;
}

static void free_method_list_func(Instance *, const GDExtensionMethodInfo *, uint32_t) {}

static GDExtensionBool has_method_func(Instance *instance, GDExtensionConstStringNamePtr name) {
	return instance->script->find_method(*(const StringName *)name) != nullptr;
}

static void call_func(Instance *instance, GDExtensionConstStringNamePtr method, const GDExtensionConstVariantPtr *args,
		GDExtensionInt argc, GDExtensionVariantPtr r_ret, GDExtensionCallError *r_error) {
	const int *ref = instance->script->find_method(*(const StringName *)method);
	if (ref == nullptr) {
		r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
		return;
	}
	lua_State *L = state();
	lua_getref(L, *ref);
	lua_getref(L, instance->table_ref);
	for (GDExtensionInt i = 0; i < argc; i++) {
		push_variant(L, *(const Variant *)args[i]);
	}
	r_error->error = GDEXTENSION_CALL_OK;
	if (lua_pcall(L, (int)argc + 1, 1, 0) != LUA_OK) {
		UtilityFunctions::push_error(String::utf8(lua_tostring(L, -1)));
		lua_pop(L, 1);
		return;
	}
	to_variant_into_nil(L, -1, (Variant *)r_ret);
	lua_pop(L, 1);
}

static void notification_func(Instance *, int32_t, GDExtensionBool) {}

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
		for (int ref : { instance->cache_ref, instance->meta_ref }) {
			lua_getref(L, ref);
			lua_pushlightuserdata(L, (void *)&INSTANCE_KEY);
			lua_pushnil(L);
			lua_rawset(L, -3);
			lua_pop(L, 1);
			lua_unref(L, ref);
		}
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
	info.get_owner_func = (GDExtensionScriptInstanceGetOwner)get_owner_func;
	info.get_method_list_func = (GDExtensionScriptInstanceGetMethodList)get_method_list_func;
	info.free_method_list_func = (GDExtensionScriptInstanceFreeMethodList2)free_method_list_func;
	info.has_method_func = (GDExtensionScriptInstanceHasMethod)has_method_func;
	info.call_func = (GDExtensionScriptInstanceCall)call_func;
	info.notification_func = (GDExtensionScriptInstanceNotification2)notification_func;
	info.refcount_decremented_func = (GDExtensionScriptInstanceRefCountDecremented)refcount_decremented_func;
	info.get_script_func = (GDExtensionScriptInstanceGetScript)get_script_func;
	info.is_placeholder_func = (GDExtensionScriptInstanceIsPlaceholder)is_placeholder_func;
	info.get_language_func = (GDExtensionScriptInstanceGetLanguage)get_language_func;
	info.free_func = (GDExtensionScriptInstanceFree)free_func;
	return info;
}();

// ---------------------------------------------------------------- script

LuauScript::Routes *LuauScript::routes_for(const void *class_info) {
	if (Routes **existing = routes.getptr(class_info)) {
		return *existing;
	}
	return routes.insert(class_info, new Routes())->value;
}

LuauScript::~LuauScript() {
	for (auto &[cls, table] : routes) {
		delete table;
	}
	lua_State *L = state();
	if (L == nullptr) {
		return;
	}
	for (auto &[name, ref] : methods) {
		lua_unref(L, ref);
	}
	if (class_ref != LUA_NOREF) {
		lua_unref(L, class_ref);
	}
}

void *LuauScript::_instance_create(Object *p_for_object) const {
	Instance *instance = new Instance();
	instance->script = Ref<LuauScript>(const_cast<LuauScript *>(this));
	instance->owner = p_for_object->_owner;
	instance->cls = class_info_of(instance->owner);
	instance->routes = const_cast<LuauScript *>(this)->routes_for(instance->cls);
	create_self_table(state(), instance);
	instances.insert(instance->owner, instance);
	return gdextension_interface::script_instance_create3(&instance_info, instance);
}

Error LuauScript::_reload(bool p_keep_state) {
	if (state() == nullptr) {
		open_state();
	}
	lua_State *L = state();
	valid = false;
	for (auto &[name, ref] : methods) {
		lua_unref(L, ref);
	}
	methods.clear();
	methods_by_ptr.clear();
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
	lua_rawgetfield(L, -1, "extends");
	base_type = lua_isstring(L, -1) ? StringName(lua_tostring(L, -1)) : StringName("RefCounted");
	lua_pop(L, 1);

	lua_pushnil(L);
	while (lua_next(L, -2)) {
		if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TFUNCTION) {
			StringName name(lua_tostring(L, -2));
			int ref = lua_ref(L, -1);
			methods.insert(name, ref);
			methods_by_ptr.insert(name_ptr(name), ref);
		}
		lua_pop(L, 1);
	}
	if (class_ref != LUA_NOREF) {
		lua_unref(L, class_ref);
	}
	class_ref = lua_ref(L, -1);
	lua_pop(L, 1);
	valid = true;
	return OK;
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
	if (state() == nullptr) {
		open_state();
	}
	lua_State *L = state();
	lua_newtable(L);
	lua_pushcfunction(L, cache_index, "__index");
	lua_setfield(L, -2, "__index");
	lua_rawsetfield(L, LUA_REGISTRYINDEX, "luau.cache_meta");
}

void LuauLanguage::_finish() {
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
