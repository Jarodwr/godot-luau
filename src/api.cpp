#include "api.h"

#include "builtins.h"
#include "internal.h"
#include "script.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <luacode.h>
#ifdef GODOT_LUAU_CODEGEN
#include <luacodegen.h>
#endif
#include <lualib.h>

#include <cstring>
#include <deque>
#include <new>

using namespace godot;

namespace luau {

// ---------------------------------------------------------------- state

static lua_State *L_main = nullptr;

lua_State *state() {
	return L_main;
}

// ---------------------------------------------------------------- atoms

// A deque: names never move as atoms are added, so engine calls can be
// passed pointers to them while Lua code creates new strings (docs/adr/0019)
static std::deque<StringName> atoms;
constexpr size_t MAX_ATOM_LENGTH = 64;
constexpr size_t MAX_ATOMS = 32000;

static int16_t user_atom(lua_State *, const char *s, size_t length) {
	if (length > MAX_ATOM_LENGTH || atoms.size() >= MAX_ATOMS) {
		return -1;
	}
	atoms.push_back(StringName(String::utf8(s, (int)length)));
	return (int16_t)(atoms.size() - 1);
}

const StringName &atom_name(int atom) {
	return atoms[atom];
}

int string_atom(lua_State *L, int index) {
	int atom = -1;
	if (lua_tostringatom(L, index, &atom) == nullptr) {
		return -1;
	}
	return atom;
}

// The StringName of the string at `index`: by atom when it has one
StringName string_name_at(lua_State *L, int index) {
	int atom = string_atom(L, index);
	if (atom >= 0) {
		return atoms[atom];
	}
	size_t length;
	const char *s = lua_tolstring(L, index, &length);
	return StringName(String::utf8(s, (int)length));
}

// ---------------------------------------------------------------- method table

static const MethodInfo METHODS[] = {
#include "api_data.inc"
};
static const size_t METHOD_COUNT = sizeof(METHODS) / sizeof(METHODS[0]);

static const MethodInfo *find_method_info(const char *class_name, const char *method) {
	size_t lo = 0, hi = METHOD_COUNT;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int c = strcmp(METHODS[mid].class_name, class_name);
		if (c == 0) {
			c = strcmp(METHODS[mid].method, method);
		}
		if (c == 0) {
			return &METHODS[mid];
		}
		if (c < 0) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return nullptr;
}

static bool native_arg(ArgType t) {
	return t != T_OTHER && t != T_VOID;
}

static bool native_ret(ArgType t) {
	switch (t) {
		case T_VOID: case T_BOOL: case T_INT: case T_FLOAT: case T_STRING:
		case T_STRING_NAME: case T_VECTOR2: case T_VECTOR3: case T_VARIANT:
		case T_OBJECT:  // a plain pointer for non-RefCounted classes (docs/adr/0017)
			return true;
		default:
			return false;  // RefCounted results, NodePath, integer vectors, Rect2, Color… use call
	}
}

// `method` as declared by `class_name` or its nearest ancestor
static Method resolve_method(StringName class_name, const StringName &method) {
	Method result;
	CharString method_utf8 = String(method).utf8();
	while (!class_name.is_empty()) {
		CharString class_utf8 = String(class_name).utf8();
		if (const MethodInfo *info = find_method_info(class_utf8.get_data(), method_utf8.get_data())) {
			result.info = info;
			result.bind = gdextension_interface::classdb_get_method_bind(class_name._native_ptr(), method._native_ptr(), info->hash);
			result.ptrcall = result.bind && !(info->flags & (F_VARARG | F_OTHER)) && native_ret(info->ret);
			for (int i = 0; result.ptrcall && i < info->argc; i++) {
				result.ptrcall = native_arg(info->args[i]);
			}
			return result;
		}
		class_name = ClassDB::get_parent_class(class_name);
	}
	return result;
}

// ---------------------------------------------------------------- classes

static HashMap<StringName, ClassInfo *> class_infos;

ClassInfo *class_info(const StringName &class_name) {
	if (ClassInfo **info = class_infos.getptr(class_name)) {
		return *info;
	}
	ClassInfo *info = new ClassInfo();
	info->name = class_name;
	info->is_ref_counted = ClassDB::is_parent_class(class_name, "RefCounted");
	class_infos.insert(class_name, info);
	return info;
}

ClassInfo *class_info_of(GDExtensionObjectPtr object) {
	StringName class_name;
	gdextension_interface::object_get_class_name(object, gdextension_interface::library, class_name._native_ptr());
	return class_info(class_name);
}

void clear_class_infos() {
	for (auto &[name, info] : class_infos) {
		for (Member *member : info->by_atom) {
			delete member;
		}
		for (auto &[n, member] : info->by_name) {
			delete member;
		}
		delete info;
	}
	class_infos.clear();
	atoms.clear();
}

static Member *resolve_member(const StringName &class_name, const StringName &name) {
	Member *member = new Member();
	member->name = name;
	if (ClassDB::class_has_method(class_name, name)) {
		member->kind = MemberKind::METHOD;
		member->method = resolve_method(class_name, name);
	} else if (StringName getter = ClassDB::class_get_property_getter(class_name, name); !getter.is_empty()) {
		member->kind = MemberKind::PROPERTY;
		Method get = resolve_method(class_name, getter);
		if (get.info && get.info->argc == 0) {
			member->getter = get;  // indexed properties keep Object.get
		}
		StringName setter = ClassDB::class_get_property_setter(class_name, name);
		if (!setter.is_empty()) {
			Method set = resolve_method(class_name, setter);
			if (set.info && set.info->argc == 1) {
				member->setter = set;
			}
		}
	}
	return member;
}

const Member &ClassInfo::member(int atom) {
	if (atom < 0) {
		static Member unknown;
		return unknown;
	}
	if ((size_t)atom >= by_atom.size()) {
		by_atom.resize(atom + 1, nullptr);
	}
	if (by_atom[atom] == nullptr) {
		by_atom[atom] = resolve_member(name, atoms[atom]);
	}
	return *by_atom[atom];
}

const Member &ClassInfo::member(const StringName &member_name) {
	if (Member **member = by_name.getptr(member_name)) {
		return **member;
	}
	return *by_name.insert(member_name, resolve_member(name, member_name))->value;
}

// ---------------------------------------------------------------- calls


// ---- String conversions (docs/adr/0023)
//
// Godot's String is UTF-32 and Luau's strings are UTF-8, so every crossing
// re-encodes. These do it without temporaries: Godot → Lua encodes into a
// local stack buffer (one per call, so thread- and re-entrancy-safe), and
// Lua → Godot constructs the String directly in its destination.
constexpr size_t STRING_STACK_BUFFER = 512;

void push_string(lua_State *L, const String &s) {
	char buffer[STRING_STACK_BUFFER];
	GDExtensionInt length = gdextension_interface::string_to_utf8_chars(s._native_ptr(), buffer, sizeof(buffer));
	if ((size_t)length <= sizeof(buffer)) {
		lua_pushlstring(L, buffer, (size_t)length);
		return;
	}
	// Longer than the buffer: one allocation of the exact size
	std::vector<char> large((size_t)length);
	gdextension_interface::string_to_utf8_chars(s._native_ptr(), large.data(), length);
	lua_pushlstring(L, large.data(), (size_t)length);
}

// Constructs a String from the Lua string at `index` in uninitialized
// `memory`. ASCII (the common case: names, keys, most UI text) widens bytes
// without UTF-8 validation.
void construct_string(lua_State *L, int index, void *memory) {
	size_t length;
	const char *s = lua_tolstring(L, index, &length);
	bool ascii = true;
	for (size_t i = 0; i < length; i++) {
		if ((unsigned char)s[i] >= 0x80) {
			ascii = false;
			break;
		}
	}
	if (ascii) {
		gdextension_interface::string_new_with_latin1_chars_and_len(memory, s, (GDExtensionInt)length);
	} else {
		gdextension_interface::string_new_with_utf8_chars_and_len2(memory, s, (GDExtensionInt)length);
	}
}

// NodePaths for strings used as path arguments, by atom (docs/adr/0017)
static HashMap<int, NodePath> node_paths;
constexpr int MAX_NODE_PATHS = 4096;

// The cached NodePath for an atom, or null if the cache is full (entries are
// never evicted while the state lives: arguments point at them)
static const NodePath *node_path_for_atom(int atom) {
	if (const NodePath *path = node_paths.getptr(atom)) {
		return path;
	}
	if (node_paths.size() >= MAX_NODE_PATHS) {
		return nullptr;
	}
	return &node_paths.insert(atom, NodePath(String(atom_name(atom))))->value;
}

// Writes the Luau value at `index` as `type` into `slot`, or points `arg_ptr`
// at a cached value; false if it doesn't fit
bool to_native(lua_State *L, int index, ArgType type, NativeSlot &slot, GDExtensionConstTypePtr &arg_ptr) {
	switch (type) {
		case T_BOOL:
			*reinterpret_cast<GDExtensionBool *>(slot.bytes) = lua_toboolean(L, index);
			return true;
		case T_INT:
			if (lua_type(L, index) != LUA_TNUMBER) return false;
			*reinterpret_cast<int64_t *>(slot.bytes) = (int64_t)lua_tonumber(L, index);
			return true;
		case T_FLOAT:
			if (lua_type(L, index) != LUA_TNUMBER) return false;
			*reinterpret_cast<double *>(slot.bytes) = lua_tonumber(L, index);
			return true;
		case T_STRING: {
			if (lua_type(L, index) != LUA_TSTRING) return false;
			construct_string(L, index, slot.bytes);
			slot.constructed = T_STRING;
			return true;
		}
		case T_STRING_NAME: {
			if (lua_type(L, index) != LUA_TSTRING) return false;
			// Arguments are passed by pointer and never modified: point at the
			// atom's name instead of copying it (docs/adr/0019)
			int atom = string_atom(L, index);
			if (atom >= 0) {
				arg_ptr = &atom_name(atom);
				return true;
			}
			new (slot.bytes) StringName(string_name_at(L, index));
			slot.constructed = T_STRING_NAME;
			return true;
		}
		case T_VECTOR2:
		case T_VECTOR3:
		case T_VECTOR2I:
		case T_VECTOR3I: {
			if (type == T_VECTOR2I && packed_tag(L, index) == LUTAG_VECTOR2I) {
				unpack_vector2i(packed_bits(L, index, LUTAG_VECTOR2I), reinterpret_cast<int32_t *>(slot.bytes));
				return true;
			}
			const float *v = lua_tovector(L, index);
			if (v == nullptr) return false;
			int n = (type == T_VECTOR2 || type == T_VECTOR2I) ? 2 : 3;
			for (int i = 0; i < n; i++) {
				if (type == T_VECTOR2 || type == T_VECTOR3) {
					reinterpret_cast<float *>(slot.bytes)[i] = v[i];
				} else {
					reinterpret_cast<int32_t *>(slot.bytes)[i] = (int32_t)v[i];
				}
			}
			return true;
		}
		case T_NODE_PATH: {
			if (lua_type(L, index) != LUA_TSTRING) return false;
			int atom = string_atom(L, index);
			if (const NodePath *cached = atom >= 0 ? node_path_for_atom(atom) : nullptr) {
				arg_ptr = cached;  // nothing to construct (docs/adr/0017)
				return true;
			}
			size_t length;
			const char *s = lua_tolstring(L, index, &length);
			new (slot.bytes) NodePath(String::utf8(s, (int)length));
			slot.constructed = T_NODE_PATH;
			return true;
		}
		case T_OBJECT:
		case T_OBJECT_REF: {
			GDExtensionObjectPtr object = lua_isnil(L, index) ? nullptr : to_object(L, index);
			if (object == nullptr && !lua_isnil(L, index)) return false;
			*reinterpret_cast<GDExtensionObjectPtr *>(slot.bytes) = object;
			return true;
		}
		case T_VARIANT:
			// Godot values in place; plain values as bytes; others converted
			if (const Variant *held = borrow_variant(L, index)) {
				arg_ptr = held;
				return true;
			}
			if (!write_plain_variant(L, index, slot.bytes)) {
				new (slot.bytes) Variant(to_variant(L, index));
			}
			slot.constructed = T_VARIANT;  // destroyed only if it needs it
			return true;
		default: {
			// Rect2, Color…: from a Variant userdata of that exact type
			Variant value = to_variant(L, index);
			Variant::Type want = type == T_RECT2 ? Variant::RECT2 : Variant::COLOR;
			if (value.get_type() != want) return false;
			GDExtensionTypeFromVariantConstructorFunc from = gdextension_interface::get_variant_to_type_constructor((GDExtensionVariantType)want);
			from(slot.bytes, value._native_ptr());
			return true;
		}
	}
}

void prepare_return(ArgType type, NativeSlot &slot) {
	switch (type) {
		case T_OBJECT: *reinterpret_cast<GDExtensionObjectPtr *>(slot.bytes) = nullptr; break;
		case T_STRING: new (slot.bytes) String(); slot.constructed = T_STRING; break;
		case T_STRING_NAME:
			// Zeroed bytes are an empty StringName (null data): no constructor call
			memset(slot.bytes, 0, sizeof(StringName));
			slot.constructed = T_STRING_NAME;
			break;
		case T_VARIANT:
			// Zeroed bytes are a valid nil Variant: no constructor call
			memset(slot.bytes, 0, sizeof(Variant));
			slot.constructed = T_VARIANT;
			break;
		default: break;
	}
}


// Lua strings for StringNames returned by the engine, keyed by the interned
// data pointer (docs/adr/0007). The StringName copy keeps the pointer valid.
struct CachedName {
	StringName name;
	int ref;
};
static HashMap<const void *, CachedName> name_strings;
constexpr int MAX_NAME_STRINGS = 4096;

void push_string_name(lua_State *L, const StringName &name) {
	const void *key = *reinterpret_cast<const void *const *>(name._native_ptr());
	if (const CachedName *cached = name_strings.getptr(key)) {
		lua_getref(L, cached->ref);
		return;
	}
	push_string(L, String(name));
	if (key != nullptr && name_strings.size() < MAX_NAME_STRINGS) {
		name_strings.insert(key, { name, lua_ref(L, -1) });
	}
}

void push_native(lua_State *L, ArgType type, NativeSlot &slot) {
	switch (type) {
		case T_VOID: lua_pushnil(L); break;
		case T_BOOL: lua_pushboolean(L, *reinterpret_cast<GDExtensionBool *>(slot.bytes)); break;
		case T_INT: lua_pushnumber(L, (double)*reinterpret_cast<int64_t *>(slot.bytes)); break;
		case T_FLOAT: lua_pushnumber(L, *reinterpret_cast<double *>(slot.bytes)); break;
		case T_STRING: push_string(L, *reinterpret_cast<String *>(slot.bytes)); break;
		case T_STRING_NAME: push_string_name(L, *reinterpret_cast<StringName *>(slot.bytes)); break;
		case T_VECTOR2: {
			const float *v = reinterpret_cast<float *>(slot.bytes);
			lua_pushvector(L, v[0], v[1], 0.0f);
			break;
		}
		case T_VECTOR3: {
			const float *v = reinterpret_cast<float *>(slot.bytes);
			lua_pushvector(L, v[0], v[1], v[2]);
			break;
		}
		case T_VARIANT: push_variant(L, *reinterpret_cast<Variant *>(slot.bytes)); break;
		case T_OBJECT: push_object(L, *reinterpret_cast<GDExtensionObjectPtr *>(slot.bytes)); break;
		default: lua_pushnil(L); break;
	}
}

static void push_call_error(lua_State *L, const Method &method, const GDExtensionCallError &error) {
	lua_pushfstring(L, "%s.%s: %s", method.info ? method.info->class_name : "?", method.info ? method.info->method : "?",
			call_error_text(error).utf8().get_data());
}

// Through Variants, for anything ptrcall doesn't handle (varargs, defaults,
// object returns, unusual types)
// ---- Variant-route calls (docs/adr/0015)
//
// godot-cpp's Variant constructor and destructor are engine calls, so
// arguments live in raw storage: only the ones passed are constructed, plain
// values are written as bytes (nothing to destroy), and results are written
// by the engine into uninitialized storage.
bool needs_destroy(const void *variant_bytes);




static bool call_variant(lua_State *L, GDExtensionObjectPtr object, GDExtensionMethodBindPtr bind, const Method &method, int first, int argc) {
	if (argc > MAX_VARIANT_ARGS) {
		lua_pushstring(L, "too many arguments");
		return false;
	}
	VariantResult result;
	GDExtensionCallError error;
	call_with_vector_retry(L, first, argc, result, error, [&](VariantArgs &args, VariantResult &r, GDExtensionCallError &e) {
		gdextension_interface::object_method_bind_call(bind, object, args.pointers(), argc, r.uninitialized(), &e);
	});
	if (error.error != GDEXTENSION_CALL_OK) {
		push_call_error(L, method, error);
		return false;
	}
	push_result(L, result);
	return true;
}

// ---- Simple calls: at most one argument and a result of the common types
// skip the generic marshalling below (docs/adr/0012), e.g.
// Engine:get_frames_drawn() or node:set_visible(b).
union SimpleValue {
	float v[4];
	double d;
	int64_t i;
	GDExtensionBool b;
};

static bool is_simple(ArgType type) {
	return type == T_VECTOR2 || type == T_VECTOR3 || type == T_FLOAT || type == T_INT || type == T_BOOL;
}

static bool to_simple(lua_State *L, int index, ArgType type, SimpleValue &r_value) {
	switch (type) {
		case T_VECTOR2:
		case T_VECTOR3: {
			const float *v = lua_tovector(L, index);
			if (v == nullptr) return false;
			r_value.v[0] = v[0];
			r_value.v[1] = v[1];
			r_value.v[2] = v[2];
			return true;
		}
		case T_FLOAT:
			if (lua_type(L, index) != LUA_TNUMBER) return false;
			r_value.d = lua_tonumber(L, index);
			return true;
		case T_INT:
			if (lua_type(L, index) != LUA_TNUMBER) return false;
			r_value.i = (int64_t)lua_tonumber(L, index);
			return true;
		case T_BOOL:
			r_value.b = lua_toboolean(L, index);
			return true;
		default:
			return false;
	}
}

static void push_simple(lua_State *L, ArgType type, const SimpleValue &value) {
	switch (type) {
		case T_VECTOR2: lua_pushvector(L, value.v[0], value.v[1], 0.0f); break;
		case T_VECTOR3: lua_pushvector(L, value.v[0], value.v[1], value.v[2]); break;
		case T_FLOAT: lua_pushnumber(L, value.d); break;
		case T_INT: lua_pushnumber(L, (double)value.i); break;
		case T_BOOL: lua_pushboolean(L, value.b); break;
		default: lua_pushnil(L); break;
	}
}

static bool fast_call(lua_State *L, GDExtensionObjectPtr object, const Method &method, int first, int argc) {
	const MethodInfo *info = method.info;
	if (!method.ptrcall || argc != info->argc || argc > 1 || (info->ret != T_VOID && !is_simple(info->ret))) {
		return false;
	}
	SimpleValue arg, result;
	GDExtensionConstTypePtr argv[] = { &arg };
	if (argc == 1) {
		if (info->args[0] == T_STRING_NAME) {
			// A name with an atom: pass the atom's StringName (docs/adr/0019)
			int atom = lua_type(L, first) == LUA_TSTRING ? string_atom(L, first) : -1;
			if (atom < 0) {
				return false;
			}
			argv[0] = &atom_name(atom);
		} else if (!is_simple(info->args[0]) || !to_simple(L, first, info->args[0], arg)) {
			return false;
		}
	}
	gdextension_interface::object_method_bind_ptrcall(method.bind, object, argc ? argv : nullptr, info->ret == T_VOID ? nullptr : &result);
	push_simple(L, info->ret, result);
	return true;
}

bool call_method(lua_State *L, GDExtensionObjectPtr object, const Method &method, int first, int argc) {
	if (method.bind == nullptr) {
		lua_pushstring(L, "method has no bind");
		return false;
	}
	if (fast_call(L, object, method, first, argc)) {
		return true;
	}
	const MethodInfo *info = method.info;
	if (method.ptrcall && argc == info->argc) {
		NativeSlot slots[MAX_FAST_ARGS];
		GDExtensionConstTypePtr argv[MAX_FAST_ARGS];
		bool ok = true;
		for (int i = 0; i < argc && ok; i++) {
			argv[i] = slots[i].bytes;
			ok = to_native(L, first + i, info->args[i], slots[i], argv[i]);
		}
		if (ok) {
			NativeSlot ret;
			prepare_return(info->ret, ret);
			gdextension_interface::object_method_bind_ptrcall(method.bind, object, argv, ret.bytes);
			push_native(L, info->ret, ret);
			return true;
		}
		// A value didn't fit the declared type: let the engine convert or report
	}
	return call_variant(L, object, method.bind, method, first, argc);
}

// Object.get / Object.set / Object.call through their binds
static const Method &object_method(const char *name) {
	static HashMap<String, Method> methods;
	if (Method *m = methods.getptr(name)) {
		return *m;
	}
	return methods.insert(name, resolve_method(StringName("Object"), StringName(name)))->value;
}

void push_object_get(lua_State *L, GDExtensionObjectPtr object, const StringName &name) {
	Variant key = name;
	const Variant *argv[] = { &key };
	Variant result;
	GDExtensionCallError error;
	gdextension_interface::object_method_bind_call(object_method("get").bind, object, (const GDExtensionConstVariantPtr *)argv, 1, result._native_ptr(), &error);
	push_variant(L, result);
}

static void generic_set(lua_State *L, GDExtensionObjectPtr object, const StringName &name, int index) {
	Variant key = name;
	Variant value = to_variant(L, index);
	const Variant *argv[] = { &key, &value };
	Variant result;
	GDExtensionCallError error;
	gdextension_interface::object_method_bind_call(object_method("set").bind, object, (const GDExtensionConstVariantPtr *)argv, 2, result._native_ptr(), &error);
}

// Object.call(name, args...): script methods and anything not in the table
// Methods that aren't engine methods (other languages' scripts, anything not
// in the method table): `variant_call` on the object's Variant reaches
// Object::callp directly, with no Variant for the name and no vararg packing
// as through Object.call (docs/adr/0021)
static bool generic_call(lua_State *L, Variant &object, const StringName &name, int first, int argc) {
	if (argc > MAX_VARIANT_ARGS) {
		lua_pushstring(L, "too many arguments");
		return false;
	}
	VariantResult result;
	GDExtensionCallError error;
	call_with_vector_retry(L, first, argc, result, error, [&](VariantArgs &args, VariantResult &r, GDExtensionCallError &e) {
		gdextension_interface::variant_call(object._native_ptr(), name._native_ptr(), args.pointers(), argc, r.uninitialized(), &e);
	});
	if (error.error != GDEXTENSION_CALL_OK) {
		CharString n = String(name).utf8();
		lua_pushfstring(L, "error %d calling '%s'", (int)error.error, n.get_data());
		return false;
	}
	push_result(L, result);
	return true;
}

// Property accessors keep their own, narrower fast paths: routing them
// through fast_call measured ~3 ns slower per access (docs/adr/0012)
static bool fast_get(lua_State *L, GDExtensionObjectPtr object, const Method &getter) {
	if (!getter.ptrcall) {
		return false;
	}
	switch (getter.info->ret) {
		case T_VECTOR2: {
			float v[2];
			gdextension_interface::object_method_bind_ptrcall(getter.bind, object, nullptr, v);
			lua_pushvector(L, v[0], v[1], 0.0f);
			return true;
		}
		case T_VECTOR3: {
			float v[3];
			gdextension_interface::object_method_bind_ptrcall(getter.bind, object, nullptr, v);
			lua_pushvector(L, v[0], v[1], v[2]);
			return true;
		}
		case T_FLOAT: {
			double d;
			gdextension_interface::object_method_bind_ptrcall(getter.bind, object, nullptr, &d);
			lua_pushnumber(L, d);
			return true;
		}
		case T_INT: {
			int64_t i;
			gdextension_interface::object_method_bind_ptrcall(getter.bind, object, nullptr, &i);
			lua_pushnumber(L, (double)i);
			return true;
		}
		case T_BOOL: {
			GDExtensionBool b;
			gdextension_interface::object_method_bind_ptrcall(getter.bind, object, nullptr, &b);
			lua_pushboolean(L, b);
			return true;
		}
		default:
			return false;
	}
}

static bool fast_set(lua_State *L, GDExtensionObjectPtr object, const Method &setter, int index) {
	if (!setter.ptrcall) {
		return false;
	}
	SimpleValue value;
	switch (setter.info->args[0]) {
		case T_VECTOR2:
		case T_VECTOR3: {
			const float *v = lua_tovector(L, index);
			if (v == nullptr) return false;
			value.v[0] = v[0];
			value.v[1] = v[1];
			value.v[2] = v[2];
			break;
		}
		case T_FLOAT:
			if (lua_type(L, index) != LUA_TNUMBER) return false;
			value.d = lua_tonumber(L, index);
			break;
		case T_INT:
			if (lua_type(L, index) != LUA_TNUMBER) return false;
			value.i = (int64_t)lua_tonumber(L, index);
			break;
		case T_BOOL:
			value.b = lua_toboolean(L, index);
			break;
		default:
			return false;
	}
	GDExtensionConstTypePtr argv[] = { &value };
	gdextension_interface::object_method_bind_ptrcall(setter.bind, object, argv, nullptr);
	return true;
}

bool get_property(lua_State *L, GDExtensionObjectPtr object, const Member &member) {
	if (fast_get(L, object, member.getter)) {
		return true;
	}
	if (member.getter.bind) {
		return call_method(L, object, member.getter, 0, 0);
	}
	push_object_get(L, object, member.name);
	return true;
}

bool set_property(lua_State *L, GDExtensionObjectPtr object, const Member &member, int index) {
	index = lua_absindex(L, index);
	if (fast_set(L, object, member.setter, index)) {
		return true;
	}
	if (member.setter.bind) {
		bool ok = call_method(L, object, member.setter, index, 1);
		lua_pop(L, 1);
		return ok;
	}
	generic_set(L, object, member.name, index);
	return true;
}

// ---------------------------------------------------------------- objects

static int objects_ref = LUA_NOREF;  // weak table: object pointer → box

struct ObjectBox {
	Variant ref;  // keeps RefCounted objects alive
	GDExtensionObjectPtr object;
	uint64_t id;
	ClassInfo *cls;
	// Whether the object can be freed while this box holds it: not for
	// RefCounted objects (`ref` holds a reference) or singletons. Like
	// GDScript, only those skip the ObjectDB lookup (docs/adr/0013).
	bool can_be_freed;
};

// The object box at `index` (null if it isn't one); errors if its object
// was freed
static ObjectBox *checked_box(lua_State *L, int index) {
	ObjectBox *box = (ObjectBox *)lua_touserdatatagged(L, index, TAG_OBJECT);
	if (box && box->can_be_freed && gdextension_interface::object_get_instance_from_id(box->id) != box->object) {
		luaL_error(L, "attempt to use a freed object");
	}
	return box;
}

static GDExtensionObjectPtr checked_object(lua_State *L, int index) {
	ObjectBox *box = checked_box(L, index);
	return box ? box->object : nullptr;
}

void push_object(lua_State *L, GDExtensionObjectPtr object, bool never_freed, bool fresh) {
	if (object == nullptr) {
		lua_pushnil(L);
		return;
	}
	if (push_self_table(L, object)) {
		return;
	}
	// One box per live non-RefCounted object, in a weak-valued table keyed by
	// the object pointer (docs/adr/0016). The instance ID check catches an address reused
	// by a new object after the old one was freed.
	lua_getref(L, objects_ref);
	if (!fresh) {
		lua_pushlightuserdata(L, object);
		if (lua_rawget(L, -2) == LUA_TUSERDATA) {
			ObjectBox *cached = (ObjectBox *)lua_touserdatatagged(L, -1, TAG_OBJECT);
			if (cached && cached->id == gdextension_interface::object_get_instance_id(object)) {
				lua_remove(L, -2);
				return;
			}
		}
		lua_pop(L, 1);  // stale or missing; the objects table stays below
	}
	ObjectBox *box = (ObjectBox *)lua_newuserdatataggedwithmetatable(L, sizeof(ObjectBox), TAG_OBJECT);
	new (box) ObjectBox();
	static GDExtensionVariantFromTypeConstructorFunc from_object = gdextension_interface::get_variant_from_type_constructor(GDEXTENSION_VARIANT_TYPE_OBJECT);
	from_object(box->ref._native_ptr(), &object);
	box->object = object;
	box->id = gdextension_interface::object_get_instance_id(object);
	box->cls = class_info_of(object);
	box->can_be_freed = !never_freed && !box->cls->is_ref_counted;
	// RefCounted objects aren't cached: temporary ones (RefCounted.new() in a
	// loop) filled the table with dead entries between collections and made
	// creating them ~70 ns slower
	if (!box->cls->is_ref_counted) {
		lua_pushlightuserdata(L, object);
		lua_pushvalue(L, -2);
		lua_rawset(L, -4);
	}
	lua_remove(L, -2);  // the objects table
}

GDExtensionObjectPtr to_object(lua_State *L, int index) {
	switch (lua_type(L, index)) {
		case LUA_TUSERDATA:
			return checked_object(L, index);
		case LUA_TTABLE:
			return self_table_owner(L, index);
		default:
			return nullptr;
	}
}

static int object_namecall(lua_State *L) {
	int atom = -1;
	const char *name = lua_namecallatom(L, &atom);
	ObjectBox *box = checked_box(L, 1);
	GDExtensionObjectPtr object = box->object;
	const Member &member = atom >= 0 ? box->cls->member(atom) : box->cls->member(StringName(name));
	bool ok;
	if (member.kind == MemberKind::METHOD && member.method.bind) {
		ok = call_method(L, object, member.method, 2, lua_gettop(L) - 1);
	} else {
		ok = generic_call(L, box->ref, atom >= 0 ? atom_name(atom) : StringName(name), 2, lua_gettop(L) - 1);
	}
	if (!ok) {
		lua_error(L);
	}
	return 1;
}

// `obj.method` as a value: a function calling it on its first argument
static int member_method_call(lua_State *L) {
	const Member *member = (const Member *)lua_tolightuserdata(L, lua_upvalueindex(1));
	GDExtensionObjectPtr object = to_object(L, 1);
	if (object == nullptr) {
		luaL_error(L, "call methods with ':'");
	}
	if (!call_method(L, object, member->method, 2, lua_gettop(L) - 1)) {
		lua_error(L);
	}
	return 1;
}

void push_member_method(lua_State *L, const Member &member) {
	lua_pushlightuserdata(L, (void *)&member);
	lua_pushcclosurek(L, member_method_call, "engine method", 1, nullptr);
}

static int object_index(lua_State *L) {
	ObjectBox *box = checked_box(L, 1);
	GDExtensionObjectPtr object = box->object;
	int atom = string_atom(L, 2);
	if (lua_type(L, 2) != LUA_TSTRING) {
		lua_pushnil(L);
		return 1;
	}
	const Member &member = atom >= 0 ? box->cls->member(atom) : box->cls->member(string_name_at(L, 2));
	switch (member.kind) {
		case MemberKind::PROPERTY:
			if (!get_property(L, object, member)) lua_error(L);
			return 1;
		case MemberKind::METHOD:
			push_member_method(L, member);
			return 1;
		default:
			push_object_get(L, object, string_name_at(L, 2));
			return 1;
	}
}

static int object_newindex(lua_State *L) {
	ObjectBox *box = checked_box(L, 1);
	GDExtensionObjectPtr object = box->object;
	int atom = string_atom(L, 2);
	const Member &member = atom >= 0 ? box->cls->member(atom) : box->cls->member(string_name_at(L, 2));
	if (member.kind == MemberKind::PROPERTY) {
		if (!set_property(L, object, member, 3)) lua_error(L);
	} else {
		generic_set(L, object, string_name_at(L, 2), 3);
	}
	return 0;
}

static int object_eq(lua_State *L) {
	lua_pushboolean(L, to_object(L, 1) == to_object(L, 2));
	return 1;
}

static int object_tostring(lua_State *L) {
	ObjectBox *box = (ObjectBox *)lua_touserdatatagged(L, 1, TAG_OBJECT);
	CharString name = String(box->cls->name).utf8();
	lua_pushfstring(L, "<%s#%llu>", name.get_data(), (unsigned long long)box->id);
	return 1;
}

// ---------------------------------------------------------------- other Variants

const Variant *borrow_variant(lua_State *L, int index) {
	if (lua_type(L, index) != LUA_TUSERDATA) {
		return nullptr;
	}
	if (Variant *value = (Variant *)lua_touserdatatagged(L, index, TAG_VARIANT)) {
		return value;
	}
	if (ObjectBox *box = checked_box(L, index)) {
		return &box->ref;
	}
	return nullptr;
}

bool has_flat_vector(lua_State *L, int first, int count) {
	for (int i = 0; i < count; i++) {
		const float *v = lua_tovector(L, first + i);
		if (v && v[2] == 0.0f) {
			return true;
		}
	}
	return false;
}

void push_result(lua_State *L, VariantResult &result) {
	if (!result.constructed) {
		lua_pushnil(L);
		return;
	}
	Variant &value = result.get();
	if (push_plain_variant(L, value)) {
		result.reset();
		return;
	}
	switch (type_of(value)) {
		case Variant::STRING_NAME:
		case Variant::OBJECT:
		case Variant::CALLABLE:
			push_variant(L, value);  // become Lua values or need special handling
			result.reset();
			return;
		default: {
			// Userdata: move the bytes in (Variants relocate like godot-cpp's
			// own move), leaving nothing to destroy in the result
			Variant *box = (Variant *)lua_newuserdatataggedwithmetatable(L, sizeof(Variant), TAG_VARIANT);
			memcpy((void *)box, result.storage, sizeof(Variant));
			result.constructed = false;
			return;
		}
	}
}

template <Variant::Operator OP>
static int variant_operator(lua_State *L);

void set_operator_metamethods(lua_State *L, int index) {
	index = lua_absindex(L, index);
	lua_pushcfunction(L, variant_operator<Variant::OP_ADD>, "__add");
	lua_setfield(L, index, "__add");
	lua_pushcfunction(L, variant_operator<Variant::OP_SUBTRACT>, "__sub");
	lua_setfield(L, index, "__sub");
	lua_pushcfunction(L, variant_operator<Variant::OP_MULTIPLY>, "__mul");
	lua_setfield(L, index, "__mul");
	lua_pushcfunction(L, variant_operator<Variant::OP_DIVIDE>, "__div");
	lua_setfield(L, index, "__div");
	lua_pushcfunction(L, variant_operator<Variant::OP_EQUAL>, "__eq");
	lua_setfield(L, index, "__eq");
	lua_pushcfunction(L, variant_operator<Variant::OP_LESS>, "__lt");
	lua_setfield(L, index, "__lt");
	lua_pushcfunction(L, variant_operator<Variant::OP_LESS_EQUAL>, "__le");
	lua_setfield(L, index, "__le");
}

void push_variant_userdata(lua_State *L, const Variant &value) {
	Variant *box = (Variant *)lua_newuserdatataggedwithmetatable(L, sizeof(Variant), TAG_VARIANT);
	new (box) Variant(value);
}

static bool variant_bytes_ok = false;

bool variant_layout_checked() {
	return variant_bytes_ok;
}
// Whether a STRING Variant holds its String at the data offset (checked at
// startup with the rest of the layout)
static bool string_in_variant_ok = false;

// The type of a Variant, from its bytes when the layout check passed
Variant::Type type_of(const Variant &value) {
	return variant_bytes_ok ? (Variant::Type)*reinterpret_cast<const int32_t *>(value._native_ptr()) : value.get_type();
}

bool is_indexed_type(Variant::Type type) {
	return type == Variant::ARRAY || (type >= Variant::PACKED_BYTE_ARRAY && type < Variant::VARIANT_MAX);
}

// Arrays (and packed arrays) by integer index, dictionaries by key, through
// the interface's indexed/keyed accessors (docs/adr/0020). A missing key or
// an index out of range reads as nil.
static int variant_index(lua_State *L) {
	Variant *self = (Variant *)lua_touserdatatagged(L, 1, TAG_VARIANT);
	Variant::Type type = type_of(*self);
	if (lua_type(L, 2) == LUA_TNUMBER && is_indexed_type(type)) {
		GDExtensionBool valid, oob;
		VariantResult result;
		gdextension_interface::variant_get_indexed(self->_native_ptr(), (GDExtensionInt)lua_tonumber(L, 2), result.uninitialized(), &valid, &oob);
		if (valid && !oob) {
			push_variant(L, result.get());
		} else {
			lua_pushnil(L);
		}
		return 1;
	}
	if (type == Variant::DICTIONARY) {
		GDExtensionBool valid;
		VariantResult result;
		{
			VariantArgs key;
			key.add(L, 2);
			gdextension_interface::variant_get_keyed(self->_native_ptr(), key.argv[0]->_native_ptr(), result.uninitialized(), &valid);
		}
		if (valid) {
			push_variant(L, result.get());
		} else {
			lua_pushnil(L);
		}
		return 1;
	}
	bool valid = false;
	Variant result = self->get(to_variant(L, 2), &valid);
	push_variant(L, result);
	return 1;
}

static int variant_newindex(lua_State *L) {
	Variant *self = (Variant *)lua_touserdatatagged(L, 1, TAG_VARIANT);
	Variant::Type type = type_of(*self);
	// Builtin values (Color, Rect2, Transform2D…) are values: a Lua variable
	// holding one may share it with others (constants like Color.RED, table
	// fields), so changing it in place would change them all. Only reference
	// types (arrays, dictionaries) take writes. docs/adr/0024
	if (!is_indexed_type(type) && type != Variant::DICTIONARY) {
		luaL_error(L, "builtin values can't be modified in place; build a new one (e.g. Color(r, g, b))");
	}
	GDExtensionBool valid = false;
	{
		VariantArgs args;
		args.add(L, 3);
		if (lua_type(L, 2) == LUA_TNUMBER && is_indexed_type(type)) {
			GDExtensionBool oob;
			gdextension_interface::variant_set_indexed(self->_native_ptr(), (GDExtensionInt)lua_tonumber(L, 2), args.argv[0]->_native_ptr(), &valid, &oob);
			valid = valid && !oob;
		} else if (type == Variant::DICTIONARY) {
			args.add(L, 2);
			gdextension_interface::variant_set_keyed(self->_native_ptr(), args.argv[1]->_native_ptr(), args.argv[0]->_native_ptr(), &valid);
		} else {
			args.add(L, 2);
			gdextension_interface::variant_set(self->_native_ptr(), args.argv[1]->_native_ptr(), args.argv[0]->_native_ptr(), &valid);
		}
	}
	if (!valid) {
		luaL_error(L, "invalid assignment to a %s value", lua_typename(L, lua_type(L, 1)));
	}
	return 0;
}

static int variant_namecall(lua_State *L) {
	Variant *self = (Variant *)lua_touserdatatagged(L, 1, TAG_VARIANT);
	int atom = -1;
	const char *name = lua_namecallatom(L, &atom);
	int argc = lua_gettop(L) - 1;
	// A cached method pointer when the method and arguments allow it (ADR 0029)
	if (call_builtin_method(L, self, atom, 2, argc)) {
		return 1;
	}
	StringName uncached;
	const StringName &method = atom >= 0 ? atom_name(atom) : (uncached = StringName(name));
	if (argc > MAX_VARIANT_ARGS) {
		luaL_error(L, "too many arguments");
	}
	bool ok;
	{
		VariantResult result;
		GDExtensionCallError error;
		call_with_vector_retry(L, 2, argc, result, error, [&](VariantArgs &args, VariantResult &r, GDExtensionCallError &e) {
			gdextension_interface::variant_call(self->_native_ptr(), method._native_ptr(), args.pointers(), argc, r.uninitialized(), &e);
		});
		ok = error.error == GDEXTENSION_CALL_OK;
		if (ok) {
			push_result(L, result);
		} else {
			CharString n = String(method).utf8();
			lua_pushfstring(L, "error %d calling '%s'", (int)error.error, n.get_data());
		}
	}
	// Raised after the Godot values above are destroyed
	if (!ok) {
		lua_error(L);
	}
	return 1;
}

// Operands of Godot values are used in place; the result is moved into the
// new userdata (docs/adr/0024)
template <Variant::Operator OP>
static int variant_operator(lua_State *L) {
	if (call_validated_operator(L, OP)) {  // docs/adr/0028
		return 1;
	}
	GDExtensionBool valid = false;
	{
		VariantResult result;
		// A vector with z = 0 is a Vector2 first, then a Vector3 if that's
		// not a valid operation (Transform3D * Vector3(1, 0, 0))
		for (int attempt = 0; attempt < 2 && !valid; attempt++) {
			if (attempt == 1 && !has_flat_vector(L, 1, 2)) {
				break;
			}
			VariantArgs operands;
			operands.vectors_as_3 = attempt == 1;
			operands.add(L, 1);
			operands.add(L, 2);
			gdextension_interface::variant_evaluate((GDExtensionVariantOperator)OP, operands.argv[0]->_native_ptr(), operands.argv[1]->_native_ptr(), result.uninitialized(), &valid);
		}
		if (valid) {
			push_result(L, result);
		}
	}
	if (!valid) {
		luaL_error(L, "invalid operands");
	}
	return 1;
}

// callable(...) for engine Callables (method Callables, bound ones…)
static int variant_call_metamethod(lua_State *L) {
	Variant *self = (Variant *)lua_touserdatatagged(L, 1, TAG_VARIANT);
	if (type_of(*self) != Variant::CALLABLE) {
		luaL_error(L, "attempt to call a %s value", "Godot");
	}
	static const StringName call_name("call");
	int argc = lua_gettop(L) - 1;
	if (argc > MAX_VARIANT_ARGS) {
		luaL_error(L, "too many arguments");
	}
	bool ok;
	{
		VariantResult result;
		GDExtensionCallError error;
		call_with_vector_retry(L, 2, argc, result, error, [&](VariantArgs &args, VariantResult &r, GDExtensionCallError &e) {
			gdextension_interface::variant_call(self->_native_ptr(), call_name._native_ptr(), args.pointers(), argc, r.uninitialized(), &e);
		});
		ok = error.error == GDEXTENSION_CALL_OK;
		if (ok) {
			push_result(L, result);
		} else {
			lua_pushfstring(L, "invalid Callable call (error %d)", (int)error.error);
		}
	}
	if (!ok) {
		lua_error(L);
	}
	return 1;
}

static int variant_tostring(lua_State *L) {
	push_string(L, to_variant(L, 1).stringify());
	return 1;
}

// ---------------------------------------------------------------- conversions

// ---- Plain Variants read and written as bytes (docs/adr/0004)
//
// Godot's Variant is { int32 type; padding; union data } with the data at
// offset 8. Checked once at startup; if it doesn't hold, the engine calls are
// used instead.
// (declared above, near type_of)

static void check_variant_layout() {
	auto type_of = [](const Variant &v) { return *reinterpret_cast<const int32_t *>(v._native_ptr()); };
	auto data = [](const Variant &v) { return reinterpret_cast<const unsigned char *>(v._native_ptr()) + VARIANT_DATA; };
	Variant b = true, i = int64_t(-7), f = 2.5, v2 = Vector2(1.5f, -2.0f), v3 = Vector3(1.0f, 2.0f, 3.0f), nil;
	variant_bytes_ok = type_of(nil) == Variant::NIL && type_of(b) == Variant::BOOL && type_of(i) == Variant::INT
			&& type_of(f) == Variant::FLOAT && type_of(v2) == Variant::VECTOR2 && type_of(v3) == Variant::VECTOR3
			&& *reinterpret_cast<const bool *>(data(b)) == true
			&& *reinterpret_cast<const int64_t *>(data(i)) == -7
			&& *reinterpret_cast<const double *>(data(f)) == 2.5
			&& reinterpret_cast<const float *>(data(v2))[0] == 1.5f && reinterpret_cast<const float *>(data(v2))[1] == -2.0f
			&& reinterpret_cast<const float *>(data(v3))[2] == 3.0f;
	if (!variant_bytes_ok) {
		UtilityFunctions::push_warning("godot-luau: unexpected Variant layout; using slower conversions");
		return;
	}
	// A STRING Variant's data is its String: the buffer pointer must match a
	// String taken out of it (copies share the buffer)
	Variant sv = String("layout");
	String copy = sv;
	string_in_variant_ok = type_of(sv) == Variant::STRING
			&& *reinterpret_cast<const void *const *>(data(sv)) == *reinterpret_cast<const void *const *>(copy._native_ptr());
}

// Whether a Variant (given its bytes) needs its destructor: false for plain
// types when the layout check passed
bool needs_destroy(const void *variant_bytes) {
	if (!variant_bytes_ok) {
		return true;
	}
	switch (*reinterpret_cast<const int32_t *>(variant_bytes)) {
		case Variant::NIL: case Variant::BOOL: case Variant::INT: case Variant::FLOAT:
		case Variant::VECTOR2: case Variant::VECTOR2I: case Variant::RECT2: case Variant::RECT2I:
		case Variant::VECTOR3: case Variant::VECTOR3I: case Variant::VECTOR4: case Variant::VECTOR4I:
		case Variant::PLANE: case Variant::QUATERNION: case Variant::COLOR: case Variant::RID:
			return false;
		default:
			return true;
	}
}

// Pushes `value` from its bytes if it's a plain type; false otherwise
bool push_plain_variant(lua_State *L, const Variant &value) {
	if (!variant_bytes_ok) {
		return false;
	}
	const unsigned char *bytes = reinterpret_cast<const unsigned char *>(value._native_ptr());
	const unsigned char *data = bytes + VARIANT_DATA;
	switch (*reinterpret_cast<const int32_t *>(bytes)) {
		case Variant::NIL: lua_pushnil(L); return true;
		case Variant::BOOL: lua_pushboolean(L, *reinterpret_cast<const bool *>(data)); return true;
		case Variant::INT: lua_pushnumber(L, (double)*reinterpret_cast<const int64_t *>(data)); return true;
		case Variant::FLOAT: lua_pushnumber(L, *reinterpret_cast<const double *>(data)); return true;
		case Variant::VECTOR2: {
			const float *v = reinterpret_cast<const float *>(data);
			lua_pushvector(L, v[0], v[1], 0.0f);
			return true;
		}
		case Variant::VECTOR3: {
			const float *v = reinterpret_cast<const float *>(data);
			lua_pushvector(L, v[0], v[1], v[2]);
			return true;
		}
		case Variant::STRING:
			if (!string_in_variant_ok) return false;
			push_string(L, *reinterpret_cast<const String *>(data));  // no copy out of the Variant
			return true;
		case Variant::VECTOR2I:
			if (!PACKED_VALUES) return false;
			push_packed_vector2i(L, reinterpret_cast<const int32_t *>(data)[0], reinterpret_cast<const int32_t *>(data)[1]);
			return true;
		case Variant::RID:
			if (!PACKED_VALUES) return false;
			push_packed_rid(L, *reinterpret_cast<const uint64_t *>(data));
			return true;
		default: return false;
	}
}

// Writes the value at `index` as a plain Variant into uninitialized `memory`;
// false (nothing written) if it isn't one
bool write_plain_variant(lua_State *L, int index, void *memory, bool vectors_as_3) {
	if (!variant_bytes_ok) {
		return false;
	}
	unsigned char *bytes = static_cast<unsigned char *>(memory);
	unsigned char *data = bytes + VARIANT_DATA;
	int32_t type;
	switch (lua_type(L, index)) {
		case LUA_TNIL:
			type = Variant::NIL;
			break;
		case LUA_TBOOLEAN:
			type = Variant::BOOL;
			*reinterpret_cast<bool *>(data) = lua_toboolean(L, index);
			break;
		case LUA_TNUMBER: {
			double d = lua_tonumber(L, index);
			if (d == (double)(int64_t)d && d > -9007199254740992.0 && d < 9007199254740992.0) {
				type = Variant::INT;
				*reinterpret_cast<int64_t *>(data) = (int64_t)d;
			} else {
				type = Variant::FLOAT;
				*reinterpret_cast<double *>(data) = d;
			}
			break;
		}
		case LUA_TSTRING:
			if (!string_in_variant_ok) return false;
			construct_string(L, index, data);  // straight into the Variant (docs/adr/0023)
			type = Variant::STRING;
			break;
		case LUA_TVECTOR: {
			const float *v = lua_tovector(L, index);
			float *out = reinterpret_cast<float *>(data);
			out[0] = v[0];
			out[1] = v[1];
			if (v[2] == 0.0f && !vectors_as_3) {
				type = Variant::VECTOR2;
			} else {
				type = Variant::VECTOR3;
				out[2] = v[2];
			}
			break;
		}
		case LUA_TLIGHTUSERDATA: {
			int tag = packed_tag(L, index);
			if (tag == LUTAG_VECTOR2I) {
				type = Variant::VECTOR2I;
				unpack_vector2i(packed_bits(L, index, tag), reinterpret_cast<int32_t *>(data));
			} else if (tag == LUTAG_RID) {
				type = Variant::RID;
				*reinterpret_cast<uint64_t *>(data) = packed_bits(L, index, tag);
			} else {
				return false;
			}
			break;
		}
		default:
			return false;
	}
	*reinterpret_cast<int32_t *>(bytes) = type;
	return true;
}

void push_variant(lua_State *L, const Variant &value) {
	if (push_plain_variant(L, value)) {
		return;
	}
	switch (value.get_type()) {
		case Variant::NIL: lua_pushnil(L); break;
		case Variant::BOOL: lua_pushboolean(L, (bool)value); break;
		case Variant::INT: lua_pushnumber(L, (double)(int64_t)value); break;
		case Variant::FLOAT: lua_pushnumber(L, (double)value); break;
		case Variant::STRING: push_string(L, value); break;
		case Variant::STRING_NAME: push_string_name(L, (StringName)value); break;
		case Variant::VECTOR2: {
			Vector2 v = value;
			lua_pushvector(L, v.x, v.y, 0.0f);
			break;
		}
		case Variant::VECTOR3: {
			Vector3 v = value;
			lua_pushvector(L, v.x, v.y, v.z);
			break;
		}
		case Variant::OBJECT: {
			static GDExtensionTypeFromVariantConstructorFunc to_obj = gdextension_interface::get_variant_to_type_constructor(GDEXTENSION_VARIANT_TYPE_OBJECT);
			GDExtensionObjectPtr object = nullptr;
			to_obj(&object, (GDExtensionVariantPtr)value._native_ptr());
			push_object(L, object);
			break;
		}
		case Variant::CALLABLE:
			// Callables made from Lua functions are those functions again
			if (!push_lua_function_of(L, value)) {
				push_variant_userdata(L, value);
			}
			break;
		default:
			push_variant_userdata(L, value);
			break;
	}
}

Variant to_variant(lua_State *L, int index) {
	switch (lua_type(L, index)) {
		case LUA_TBOOLEAN:
			return (bool)lua_toboolean(L, index);
		case LUA_TNUMBER: {
			double d = lua_tonumber(L, index);
			if (d == (double)(int64_t)d && d > -9007199254740992.0 && d < 9007199254740992.0) {
				return (int64_t)d;
			}
			return d;
		}
		case LUA_TSTRING: {
			alignas(String) unsigned char memory[sizeof(String)];
			construct_string(L, index, memory);
			String *s = reinterpret_cast<String *>(memory);
			Variant value(*s);
			s->~String();
			return value;
		}
		case LUA_TFUNCTION:
			return lua_function_to_callable(L, index);
		case LUA_TLIGHTUSERDATA: {
			alignas(Variant) unsigned char bytes[sizeof(Variant)] = {};
			if (packed_tag(L, index) && write_plain_variant(L, index, bytes)) {
				return *reinterpret_cast<Variant *>(bytes);  // plain: nothing to destroy
			}
			return Variant();
		}
		case LUA_TVECTOR: {
			// Open question: Vector2 and Vector3 share Luau's vector
			const float *v = lua_tovector(L, index);
			if (v[2] == 0.0f) {
				return Vector2(v[0], v[1]);
			}
			return Vector3(v[0], v[1], v[2]);
		}
		case LUA_TUSERDATA: {
			if (ObjectBox *box = (ObjectBox *)lua_touserdatatagged(L, index, TAG_OBJECT)) {
				return box->ref;
			}
			if (Variant *value = (Variant *)lua_touserdatatagged(L, index, TAG_VARIANT)) {
				return *value;
			}
			return Variant();
		}
		case LUA_TTABLE: {
			GDExtensionObjectPtr object = self_table_owner(L, index);
			if (object == nullptr) {
				return table_aware_to_variant(L, index, 0);  // docs/adr/0027
			}
			if (object) {
				Variant result;
				static GDExtensionVariantFromTypeConstructorFunc from_object = gdextension_interface::get_variant_from_type_constructor(GDEXTENSION_VARIANT_TYPE_OBJECT);
				from_object(result._native_ptr(), &object);
				return result;
			}
			return Variant();
		}
		default:
			return Variant();
	}
}

void to_variant_into_nil(lua_State *L, int index, Variant *r_dest) {
	if (lua_type(L, index) <= LUA_TNIL) {
		return;  // nil, or nothing at that index (no result)
	}
	// Constructed in place (no move), then relocated: r_dest held nil, so
	// there's nothing to destroy there, and the buffer is never destroyed
	alignas(Variant) unsigned char buffer[sizeof(Variant)] = {};
	if (!write_plain_variant(L, index, buffer)) {
		new (buffer) Variant(to_variant(L, index));
	}
	memcpy((void *)r_dest, buffer, sizeof(Variant));
}

// ---------------------------------------------------------------- globals



static int print(lua_State *L) {
	String line;
	for (int i = 1; i <= lua_gettop(L); i++) {
		size_t length;
		const char *s = luaL_tolstring(L, i, &length);
		line += (i > 1 ? "\t" : "") + String::utf8(s, (int)length);
		lua_pop(L, 1);
	}
	UtilityFunctions::print(line);
	return 0;
}

// string:length(): number of characters, like Godot's String.length()
static int string_length(lua_State *L) {
	size_t length;
	const char *s = luaL_checklstring(L, 1, &length);
	int count = 0;
	for (size_t i = 0; i < length; i++) {
		count += (s[i] & 0xC0) != 0x80;
	}
	lua_pushinteger(L, count);
	return 1;
}


// Globals not defined by scripts: engine singletons (Engine, OS, Input…)
static int globals_index(lua_State *L) {
	if (lua_type(L, 2) != LUA_TSTRING) {
		return 0;
	}
	StringName name = string_name_at(L, 2);
	if (Engine::get_singleton()->has_singleton(name)) {
		push_object(L, gdextension_interface::global_get_singleton(name._native_ptr()), true);
	} else if (ClassDB::class_exists(name)) {
		push_class_table(L, class_info(name));  // `new` and constants
	} else if (!push_builtin_global(L, lua_tostring(L, 2))) {  // utilities, global enums
		return 0;
	}
	lua_pushvalue(L, 2);
	lua_pushvalue(L, -2);
	lua_rawset(L, 1);  // cache
	return 1;
}

void register_globals(lua_State *L) {
	// Object userdata
	lua_newtable(L);
	lua_pushcfunction(L, object_namecall, "__namecall");
	lua_setfield(L, -2, "__namecall");
	lua_pushcfunction(L, object_index, "__index");
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, object_newindex, "__newindex");
	lua_setfield(L, -2, "__newindex");
	lua_pushcfunction(L, object_eq, "__eq");
	lua_setfield(L, -2, "__eq");
	lua_pushcfunction(L, object_tostring, "__tostring");
	lua_setfield(L, -2, "__tostring");
	lua_setuserdatametatable(L, TAG_OBJECT);

	lua_newtable(L);  // objects table, weak values
	lua_newtable(L);
	lua_pushstring(L, "v");
	lua_setfield(L, -2, "__mode");
	lua_setmetatable(L, -2);
	objects_ref = lua_ref(L, -1);
	lua_pop(L, 1);
	lua_setuserdatadtor(L, TAG_OBJECT, [](lua_State *, void *p) { ((ObjectBox *)p)->~ObjectBox(); });

	// Other Variants (Rect2, Color, Array…)
	lua_newtable(L);
	lua_pushcfunction(L, variant_index, "__index");
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, variant_newindex, "__newindex");
	lua_setfield(L, -2, "__newindex");
	lua_pushcfunction(L, variant_namecall, "__namecall");
	lua_setfield(L, -2, "__namecall");
	set_operator_metamethods(L, -1);
	lua_pushcfunction(L, variant_call_metamethod, "__call");
	lua_setfield(L, -2, "__call");
	lua_pushcfunction(L, variant_tostring, "__tostring");
	lua_setfield(L, -2, "__tostring");
	lua_setuserdatametatable(L, TAG_VARIANT);
	lua_setuserdatadtor(L, TAG_VARIANT, [](lua_State *, void *p) { ((Variant *)p)->~Variant(); });

	lua_pushcfunction(L, print, "print");
	lua_setglobal(L, "print");

	lua_getglobal(L, "string");
	lua_pushcfunction(L, string_length, "length");
	lua_setfield(L, -2, "length");
	lua_pop(L, 1);

	register_builtins(L);

	lua_pushvalue(L, LUA_GLOBALSINDEX);
	lua_newtable(L);
	lua_pushcfunction(L, globals_index, "__index");
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, -2);
	lua_pop(L, 1);
}

// loadstring(code, chunkname?): Luau compiles source only through its
// compiler library, which the VM doesn't include
static int loadstring(lua_State *L) {
	size_t length;
	const char *code = luaL_checklstring(L, 1, &length);
	const char *chunkname = luaL_optstring(L, 2, "=loadstring");
	size_t bytecode_size = 0;
	char *bytecode = luau_compile(code, length, nullptr, &bytecode_size);
	int status = luau_load(L, chunkname, bytecode, bytecode_size, 0);
	::free(bytecode);
	if (status != 0) {
		lua_pushnil(L);
		lua_insert(L, -2);
		return 2;  // nil, message
	}
	return 1;
}

// Scripts (and Fennel's output) compile with inlining and with `Vector2(x, y)`
// recognised as a vector constructor (docs/adr/0008)
static lua_CompileOptions *compile_options() {
	static lua_CompileOptions options = [] {
		lua_CompileOptions o = {};
		o.optimizationLevel = 2;
		o.debugLevel = 1;
		o.vectorCtor = "Vector2";
		return o;
	}();
	return &options;
}

bool load_chunk(lua_State *L, const String &source, const String &chunkname) {
	CharString code = source.utf8();
	CharString name = chunkname.utf8();
	size_t bytecode_size = 0;
	char *bytecode = luau_compile(code.get_data(), code.length(), compile_options(), &bytecode_size);
	int status = luau_load(L, name.get_data(), bytecode, bytecode_size, 0);
	::free(bytecode);
#ifdef GODOT_LUAU_CODEGEN
	if (status == 0 && luau_codegen_supported()) {
		luau_codegen_compile(L, -1);
	}
#endif
	return status == 0;
}

// What Fennel expects of a Lua environment that Luau leaves to the host
static const char *PRELUDE = R"(
package = { preload = {}, loaded = {}, path = "", config = "/\n;\n?\n!\n-\n", searchers = {} }
-- Fennel's own modules. Scripts get godot-luau's require (script.cpp), which
-- also loads files.
function require(name)
	local loaded = package.loaded[name]
	if loaded ~= nil then return loaded end
	local loader = package.preload[name]
	if loader == nil then error("module '" .. tostring(name) .. "' not found") end
	local result = loader(name)
	if result == nil then result = true end
	package.loaded[name] = result
	return result
end
)";

static void load_fennel(lua_State *L) {
	String source = FileAccess::get_file_as_string("res://addons/godot_luau/fennel.lua");
	if (source.is_empty()) {
		return;  // no Fennel support
	}
	if (!load_chunk(L, source, "@fennel.lua") || lua_pcall(L, 0, 1, 0) != LUA_OK) {
		UtilityFunctions::push_error("Loading Fennel: " + String::utf8(lua_tostring(L, -1)));
		lua_pop(L, 1);
		return;
	}
	lua_getglobal(L, "package");
	lua_getfield(L, -1, "loaded");
	lua_pushvalue(L, -3);
	lua_setfield(L, -2, "fennel");
	lua_pop(L, 2);
	lua_rawsetfield(L, LUA_REGISTRYINDEX, "luau.fennel");
}

bool compile_fennel(lua_State *L, const String &source, const String &path, String &r_lua) {
	lua_rawgetfield(L, LUA_REGISTRYINDEX, "luau.fennel");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		r_lua = "Fennel isn't available (addons/godot_luau/fennel.lua missing)";
		return false;
	}
	lua_getfield(L, -1, "compileString");
	lua_remove(L, -2);
	CharString code = source.utf8();
	lua_pushlstring(L, code.get_data(), code.length());
	lua_newtable(L);
	CharString filename = path.utf8();
	lua_pushstring(L, filename.get_data());
	lua_setfield(L, -2, "filename");
	lua_pushboolean(L, false);
	lua_setfield(L, -2, "allowedGlobals");
	// Lua lines match the .fnl lines, so errors point at the Fennel source
	lua_pushboolean(L, true);
	lua_setfield(L, -2, "correlate");
	bool ok = lua_pcall(L, 2, 1, 0) == LUA_OK;
	size_t length;
	const char *s = lua_tolstring(L, -1, &length);
	r_lua = s ? String::utf8(s, (int)length) : String("Fennel error");
	lua_pop(L, 1);
	return ok;
}

void open_state() {
	L_main = luaL_newstate();
	install_error_handler(L_main);
#ifdef GODOT_LUAU_CODEGEN
	if (luau_codegen_supported()) {
		luau_codegen_create(L_main);
	}
#endif
	lua_callbacks(L_main)->useratom = user_atom;
	check_variant_layout();
	luaL_openlibs(L_main);
	lua_pushcfunction(L_main, loadstring, "loadstring");
	lua_setglobal(L_main, "loadstring");
	if (load_chunk(L_main, PRELUDE, "=prelude")) {
		lua_call(L_main, 0, 0);
	}
	register_globals(L_main);
	open_coroutines(L_main);
	load_fennel(L_main);
	// Globals are set up: let loaded code cache global lookups and use the
	// builtin fast calls. Fennel only calls setfenv on its own macro
	// environments, so this stays set (docs/adr/0008).
	lua_setsafeenv(L_main, LUA_GLOBALSINDEX, true);
}

static uint64_t generation = 1;

uint64_t state_generation() {
	return generation;
}

Variant coerce_to_type(const Variant &value, Variant::Type type) {
	Variant::Type have = value.get_type();
	if (type == Variant::NIL || have == type || have == Variant::NIL) {
		return value;
	}
	if (have == Variant::INT && type == Variant::FLOAT) {
		return (double)(int64_t)value;
	}
	if (have == Variant::FLOAT && type == Variant::INT) {
		return (int64_t)(double)value;
	}
	if (have == Variant::VECTOR2 && type == Variant::VECTOR3) {
		Vector2 v = value;
		return Vector3(v.x, v.y, 0);
	}
	// Anything else: Godot's own conversion constructor, when there is one
	Variant result;
	const Variant *args[] = { &value };
	GDExtensionCallError error;
	result.~Variant();
	gdextension_interface::variant_construct((GDExtensionVariantType)type, result._native_ptr(), (const GDExtensionConstVariantPtr *)args, 1, &error);
	return error.error == GDEXTENSION_CALL_OK ? result : value;
}

void close_state() {
	generation++;
	clear_threads();
	clear_builtins();
	name_strings.clear();
	node_paths.clear();
	if (L_main) {
		lua_close(L_main);
		L_main = nullptr;
	}
	clear_class_infos();
}

} // namespace luau
