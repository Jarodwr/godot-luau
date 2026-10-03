// Godot's API surface beyond objects: builtin types (constructors, constants,
// static and instance methods), methods on vectors and strings, utility
// functions, global enums, class constants, iteration and Lua table
// conversion. docs/adr/0024–0027.
#include "builtins.h"

#include "internal.h"
#include "script.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <lualib.h>

#include <cmath>
#include <cstring>
#include <deque>
#include <vector>

using namespace godot;

namespace luau {

// ---------------------------------------------------------------- data

namespace {

struct UtilityInfo {
	const char *name;
	int64_t hash;
	ArgType ret;
	uint8_t argc;
	ArgType args[MAX_FAST_ARGS];
	bool vararg;
};

const UtilityInfo UTILITIES[] = {
#include "utility_data.inc"
};

struct EnumValue {
	const char *name;
	int64_t value;
};

const EnumValue GLOBAL_ENUMS[] = {
#include "global_enum_data.inc"
};

enum BuiltinKind : uint8_t { B_CONSTANT, B_ENUM, B_METHOD, B_STATIC };

struct BuiltinMember {
	const char *type;
	const char *name;
	BuiltinKind kind;
	int64_t value;
};

const BuiltinMember BUILTIN_MEMBERS[] = {
#include "builtin_data.inc"
};

template <typename T, size_t N, typename Less>
const T *find_sorted(const T (&table)[N], Less less_than_key) {
	size_t lo = 0, hi = N;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int c = less_than_key(table[mid]);
		if (c == 0) {
			return &table[mid];
		}
		if (c < 0) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return nullptr;
}

const UtilityInfo *find_utility(const char *name) {
	return find_sorted(UTILITIES, [name](const UtilityInfo &u) { return strcmp(u.name, name); });
}

const EnumValue *find_global_enum(const char *name) {
	return find_sorted(GLOBAL_ENUMS, [name](const EnumValue &e) { return strcmp(e.name, name); });
}

const BuiltinMember *find_builtin_member(const char *type, const char *name) {
	return find_sorted(BUILTIN_MEMBERS, [type, name](const BuiltinMember &m) {
		int c = strcmp(m.type, type);
		return c != 0 ? c : strcmp(m.name, name);
	});
}

// Names that closures refer to: kept for the state's lifetime, never moved
std::deque<StringName> held_names;

const StringName *hold_name(const char *name) {
	held_names.emplace_back(name);
	return &held_names.back();
}

struct BuiltinType {
	const char *name;
	Variant::Type type;
};

const BuiltinType BUILTIN_TYPES[] = {
	{ "Vector2", Variant::VECTOR2 }, { "Vector2i", Variant::VECTOR2I }, { "Rect2", Variant::RECT2 },
	{ "Rect2i", Variant::RECT2I }, { "Vector3", Variant::VECTOR3 }, { "Vector3i", Variant::VECTOR3I },
	{ "Transform2D", Variant::TRANSFORM2D }, { "Vector4", Variant::VECTOR4 }, { "Vector4i", Variant::VECTOR4I },
	{ "Plane", Variant::PLANE }, { "Quaternion", Variant::QUATERNION }, { "AABB", Variant::AABB },
	{ "Basis", Variant::BASIS }, { "Transform3D", Variant::TRANSFORM3D }, { "Projection", Variant::PROJECTION },
	{ "Color", Variant::COLOR }, { "StringName", Variant::STRING_NAME }, { "NodePath", Variant::NODE_PATH },
	{ "RID", Variant::RID }, { "Callable", Variant::CALLABLE }, { "Signal", Variant::SIGNAL },
	{ "Dictionary", Variant::DICTIONARY }, { "Array", Variant::ARRAY },
	{ "PackedByteArray", Variant::PACKED_BYTE_ARRAY }, { "PackedInt32Array", Variant::PACKED_INT32_ARRAY },
	{ "PackedInt64Array", Variant::PACKED_INT64_ARRAY }, { "PackedFloat32Array", Variant::PACKED_FLOAT32_ARRAY },
	{ "PackedFloat64Array", Variant::PACKED_FLOAT64_ARRAY }, { "PackedStringArray", Variant::PACKED_STRING_ARRAY },
	{ "PackedVector2Array", Variant::PACKED_VECTOR2_ARRAY }, { "PackedVector3Array", Variant::PACKED_VECTOR3_ARRAY },
	{ "PackedColorArray", Variant::PACKED_COLOR_ARRAY }, { "PackedVector4Array", Variant::PACKED_VECTOR4_ARRAY },
};

const char *type_name(Variant::Type type) {
	for (const BuiltinType &t : BUILTIN_TYPES) {
		if (t.type == type) {
			return t.name;
		}
	}
	return "?";
}

// Pushes the result of a Variant-route call, or raises with a message naming
// `what`; godot values are destroyed before raising
int finish_call(lua_State *L, VariantResult &result, const GDExtensionCallError &error, const char *what) {
	if (error.error == GDEXTENSION_CALL_OK) {
		push_result(L, result);
		return 1;
	}
	lua_pushfstring(L, "invalid call to %s (error %d, argument %d)", what, (int)error.error, (int)error.argument);
	return -1;
}

} // namespace

// ---------------------------------------------------------------- builtin type globals (ADR 0024)

// Type(...): Godot picks the constructor matching the arguments
static int builtin_construct(lua_State *L) {
	Variant::Type type = (Variant::Type)lua_tointeger(L, lua_upvalueindex(1));
	int argc = lua_gettop(L) - 1;  // 1 is the type table
	if (argc > MAX_VARIANT_ARGS) {
		luaL_error(L, "too many arguments");
	}
	int status;
	{
		VariantResult result;
		GDExtensionCallError error;
		call_with_vector_retry(L, 2, argc, result, error, [&](VariantArgs &args, VariantResult &r, GDExtensionCallError &e) {
			gdextension_interface::variant_construct((GDExtensionVariantType)type, r.uninitialized(), args.pointers(), argc, &e);
		});
		status = finish_call(L, result, error, type_name(type));
	}
	if (status < 0) {
		lua_error(L);
	}
	return status;
}

static int vector2_construct(lua_State *L) {
	lua_pushvector(L, (float)luaL_optnumber(L, 2, 0), (float)luaL_optnumber(L, 3, 0), 0.0f);
	return 1;
}

static int vector3_construct(lua_State *L) {
	lua_pushvector(L, (float)luaL_optnumber(L, 2, 0), (float)luaL_optnumber(L, 3, 0), (float)luaL_optnumber(L, 4, 0));
	return 1;
}

// Type.method(...) for static methods
static int builtin_static_call(lua_State *L) {
	Variant::Type type = (Variant::Type)lua_tointeger(L, lua_upvalueindex(1));
	const StringName *name = (const StringName *)lua_tolightuserdata(L, lua_upvalueindex(2));
	int argc = lua_gettop(L);
	if (argc > MAX_VARIANT_ARGS) {
		luaL_error(L, "too many arguments");
	}
	int status;
	{
		VariantResult result;
		GDExtensionCallError error;
		call_with_vector_retry(L, 1, argc, result, error, [&](VariantArgs &args, VariantResult &r, GDExtensionCallError &e) {
			gdextension_interface::variant_call_static((GDExtensionVariantType)type, name->_native_ptr(), args.pointers(), argc, r.uninitialized(), &e);
		});
		status = finish_call(L, result, error, "static method");
	}
	if (status < 0) {
		lua_error(L);
	}
	return status;
}

// Type.method(value, ...) for instance methods: also what `value:method()`
// resolves to for values without their own metatable route
static int builtin_method_call(lua_State *L) {
	const StringName *name = (const StringName *)lua_tolightuserdata(L, lua_upvalueindex(1));
	int argc = lua_gettop(L) - 1;
	if (argc < 0 || argc > MAX_VARIANT_ARGS) {
		luaL_error(L, "call methods with ':'");
	}
	int status;
	{
		VariantResult result;
		GDExtensionCallError error;
		call_with_vector_retry(L, 1, argc + 1, result, error, [&](VariantArgs &args, VariantResult &r, GDExtensionCallError &e) {
			gdextension_interface::variant_call(const_cast<Variant *>(args.argv[0])->_native_ptr(), name->_native_ptr(), args.pointers() + 1, argc, r.uninitialized(), &e);
		});
		status = finish_call(L, result, error, "method");
	}
	if (status < 0) {
		lua_error(L);
	}
	return status;
}

// Missing keys on a type table: constants, enum values, static and instance
// methods. Resolved once and stored in the table.
static int builtin_type_index(lua_State *L) {
	Variant::Type type = (Variant::Type)lua_tointeger(L, lua_upvalueindex(1));
	if (lua_type(L, 2) != LUA_TSTRING) {
		return 0;
	}
	const char *key = lua_tostring(L, 2);
	const BuiltinMember *member = find_builtin_member(type_name(type), key);
	if (member == nullptr) {
		return 0;
	}
	switch (member->kind) {
		case B_CONSTANT: {
			VariantResult result;
			StringName name(key);
			gdextension_interface::variant_get_constant_value((GDExtensionVariantType)type, name._native_ptr(), result.uninitialized());
			push_variant(L, result.get());
			break;
		}
		case B_ENUM:
			lua_pushnumber(L, (double)member->value);
			break;
		case B_STATIC:
			lua_pushinteger(L, type);
			lua_pushlightuserdata(L, (void *)hold_name(key));
			lua_pushcclosurek(L, builtin_static_call, member->name, 2, nullptr);
			break;
		case B_METHOD:
			lua_pushlightuserdata(L, (void *)hold_name(key));
			lua_pushcclosurek(L, builtin_method_call, member->name, 1, nullptr);
			break;
	}
	lua_pushvalue(L, 2);
	lua_pushvalue(L, -2);
	lua_rawset(L, 1);
	return 1;
}

static void register_builtin_types(lua_State *L) {
	for (const BuiltinType &t : BUILTIN_TYPES) {
		lua_newtable(L);  // the type table
		lua_newtable(L);  // its metatable
		if (t.type == Variant::VECTOR2 || t.type == Variant::VECTOR3) {
			// Native Luau vectors (Vector2(x, y) is also compiled to a fast call)
			lua_pushcfunction(L, t.type == Variant::VECTOR2 ? vector2_construct : vector3_construct, t.name);
		} else {
			lua_pushinteger(L, t.type);
			lua_pushcclosurek(L, builtin_construct, t.name, 1, nullptr);
		}
		lua_setfield(L, -2, "__call");
		lua_pushinteger(L, t.type);
		lua_pushcclosurek(L, builtin_type_index, "__index", 1, nullptr);
		lua_setfield(L, -2, "__index");
		lua_setmetatable(L, -2);
		lua_setglobal(L, t.name);
	}
}

// ---------------------------------------------------------------- vector methods (ADR 0025)
//
// Vector2 and Vector3 are Luau vectors (Vector2 has z = 0). The common methods
// run here in C with Godot's float maths; the rest go to the engine as
// Vector2 when every vector involved has z = 0 and Vector2 has the method,
// else as Vector3.

namespace {

const float *check_vector(lua_State *L, int index) {
	const float *v = lua_tovector(L, index);
	if (v == nullptr) {
		luaL_typeerror(L, index, "vector");
	}
	return v;
}

int push_vec(lua_State *L, float x, float y, float z) {
	lua_pushvector(L, x, y, z);
	return 1;
}

int vec_length(lua_State *L) {
	const float *v = check_vector(L, 1);
	lua_pushnumber(L, std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
	return 1;
}

int vec_length_squared(lua_State *L) {
	const float *v = check_vector(L, 1);
	lua_pushnumber(L, v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	return 1;
}

int vec_normalized(lua_State *L) {
	const float *v = check_vector(L, 1);
	float l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	if (l == 0.0f) {
		return push_vec(L, 0, 0, 0);
	}
	l = std::sqrt(l);
	return push_vec(L, v[0] / l, v[1] / l, v[2] / l);
}

int vec_dot(lua_State *L) {
	const float *a = check_vector(L, 1);
	const float *b = check_vector(L, 2);
	lua_pushnumber(L, a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
	return 1;
}

int vec_distance_to(lua_State *L) {
	const float *a = check_vector(L, 1);
	const float *b = check_vector(L, 2);
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	lua_pushnumber(L, std::sqrt(x * x + y * y + z * z));
	return 1;
}

int vec_distance_squared_to(lua_State *L) {
	const float *a = check_vector(L, 1);
	const float *b = check_vector(L, 2);
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	lua_pushnumber(L, x * x + y * y + z * z);
	return 1;
}

int vec_direction_to(lua_State *L) {
	const float *a = check_vector(L, 1);
	const float *b = check_vector(L, 2);
	float x = b[0] - a[0], y = b[1] - a[1], z = b[2] - a[2];
	float l = x * x + y * y + z * z;
	if (l == 0.0f) {
		return push_vec(L, 0, 0, 0);
	}
	l = std::sqrt(l);
	return push_vec(L, x / l, y / l, z / l);
}

int vec_lerp(lua_State *L) {
	const float *a = check_vector(L, 1);
	const float *b = check_vector(L, 2);
	float w = (float)luaL_checknumber(L, 3);
	return push_vec(L, a[0] + w * (b[0] - a[0]), a[1] + w * (b[1] - a[1]), a[2] + w * (b[2] - a[2]));
}

int vec_abs(lua_State *L) {
	const float *v = check_vector(L, 1);
	return push_vec(L, std::fabs(v[0]), std::fabs(v[1]), std::fabs(v[2]));
}

int vec_floor(lua_State *L) {
	const float *v = check_vector(L, 1);
	return push_vec(L, std::floor(v[0]), std::floor(v[1]), std::floor(v[2]));
}

int vec_ceil(lua_State *L) {
	const float *v = check_vector(L, 1);
	return push_vec(L, std::ceil(v[0]), std::ceil(v[1]), std::ceil(v[2]));
}

int vec_round(lua_State *L) {
	const float *v = check_vector(L, 1);
	return push_vec(L, std::round(v[0]), std::round(v[1]), std::round(v[2]));
}

bool is_flat(const float *v) {
	return v[2] == 0.0f;
}

// Any other method: through the engine, as Vector2 or Vector3
int vec_generic(lua_State *L) {
	const StringName *name = (const StringName *)lua_tolightuserdata(L, lua_upvalueindex(1));
	bool vector2_has = lua_toboolean(L, lua_upvalueindex(2));
	int argc = lua_gettop(L) - 1;
	if (argc < 0 || argc > MAX_VARIANT_ARGS) {
		luaL_error(L, "call methods with ':'");
	}
	bool flat = vector2_has;
	for (int i = 1; flat && i <= argc + 1; i++) {
		const float *v = lua_tovector(L, i);
		flat = v == nullptr || is_flat(v);
	}
	int status;
	for (int attempt = 0;; attempt++) {
		VariantResult result;
		GDExtensionCallError error;
		{
			const float *v = check_vector(L, 1);
			Variant self = flat ? Variant(Vector2(v[0], v[1])) : Variant(Vector3(v[0], v[1], v[2]));
			VariantArgs args;
			for (int i = 0; i < argc; i++) {
				const float *a = lua_tovector(L, 2 + i);
				if (a && !flat) {
					// Vector arguments follow the receiver's type
					new (args.storage[args.count]) Variant(Vector3(a[0], a[1], a[2]));
					args.argv[args.count] = reinterpret_cast<const Variant *>(args.storage[args.count]);
					args.count++;
				} else {
					args.add(L, 2 + i);
				}
			}
			gdextension_interface::variant_call(self._native_ptr(), name->_native_ptr(), args.pointers(), argc, result.uninitialized(), &error);
		}
		// Called as Vector2 but not valid as one (e.g. rotated(axis, angle)):
		// try as Vector3
		if (error.error != GDEXTENSION_CALL_OK && flat && attempt == 0) {
			flat = false;
			continue;
		}
		status = finish_call(L, result, error, "vector method");
		break;
	}
	if (status < 0) {
		lua_error(L);
	}
	return status;
}

// Unknown names on the vector methods table
int vec_methods_index(lua_State *L) {
	if (lua_type(L, 2) != LUA_TSTRING) {
		return 0;
	}
	const char *key = lua_tostring(L, 2);
	const BuiltinMember *v2 = find_builtin_member("Vector2", key);
	const BuiltinMember *v3 = find_builtin_member("Vector3", key);
	bool v2_method = v2 && v2->kind == B_METHOD;
	bool v3_method = v3 && v3->kind == B_METHOD;
	if (!v2_method && !v3_method) {
		return 0;
	}
	// cross differs in kind (Vector2: a number, Vector3: a vector) and can't be
	// told apart when z = 0: it's always Vector3's, whose .z is the 2D result
	bool as_vector2 = v2_method && strcmp(key, "cross") != 0;
	lua_pushlightuserdata(L, (void *)hold_name(key));
	lua_pushboolean(L, as_vector2);
	lua_pushcclosurek(L, vec_generic, key, 2, nullptr);
	lua_pushvalue(L, 2);
	lua_pushvalue(L, -2);
	lua_rawset(L, 1);
	return 1;
}

} // namespace

static void register_vector_methods(lua_State *L) {
	lua_newtable(L);  // methods
	const luaL_Reg fast[] = {
		{ "length", vec_length }, { "length_squared", vec_length_squared }, { "normalized", vec_normalized },
		{ "dot", vec_dot }, { "distance_to", vec_distance_to }, { "distance_squared_to", vec_distance_squared_to },
		{ "direction_to", vec_direction_to }, { "lerp", vec_lerp }, { "abs", vec_abs }, { "floor", vec_floor },
		{ "ceil", vec_ceil }, { "round", vec_round }, { nullptr, nullptr },
	};
	for (const luaL_Reg *r = fast; r->name; r++) {
		lua_pushcfunction(L, r->func, r->name);
		lua_setfield(L, -2, r->name);
	}
	lua_newtable(L);
	lua_pushcfunction(L, vec_methods_index, "__index");
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, -2);

	lua_newtable(L);  // the vector type's metatable
	lua_insert(L, -2);
	lua_setfield(L, -2, "__index");
	lua_pushvector(L, 0, 0, 0);
	lua_insert(L, -2);
	lua_setmetatable(L, -2);
	lua_pop(L, 1);
}

// ---------------------------------------------------------------- Godot String methods on Lua strings

static int string_method_call(lua_State *L) {
	const StringName *name = (const StringName *)lua_tolightuserdata(L, lua_upvalueindex(1));
	int argc = lua_gettop(L) - 1;
	if (argc < 0 || argc > MAX_VARIANT_ARGS) {
		luaL_error(L, "call methods with ':'");
	}
	int status;
	{
		VariantResult result;
		GDExtensionCallError error;
		call_with_vector_retry(L, 1, argc + 1, result, error, [&](VariantArgs &args, VariantResult &r, GDExtensionCallError &e) {
			gdextension_interface::variant_call(const_cast<Variant *>(args.argv[0])->_native_ptr(), name->_native_ptr(), args.pointers() + 1, argc, r.uninitialized(), &e);
		});
		status = finish_call(L, result, error, "String method");
	}
	if (status < 0) {
		lua_error(L);
	}
	return status;
}

// Names missing from Lua's string library: Godot's String methods
static int string_library_index(lua_State *L) {
	if (lua_type(L, 2) != LUA_TSTRING) {
		return 0;
	}
	const char *key = lua_tostring(L, 2);
	const BuiltinMember *member = find_builtin_member("String", key);
	if (member == nullptr || member->kind != B_METHOD) {
		return 0;
	}
	lua_pushlightuserdata(L, (void *)hold_name(key));
	lua_pushcclosurek(L, string_method_call, key, 1, nullptr);
	lua_pushvalue(L, 2);
	lua_pushvalue(L, -2);
	lua_rawset(L, 1);
	return 1;
}

static void register_string_methods(lua_State *L) {
	lua_getglobal(L, "string");
	lua_newtable(L);
	lua_pushcfunction(L, string_library_index, "__index");
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, -2);
	lua_pop(L, 1);
}

// ---------------------------------------------------------------- utility functions (ADR 0026)

static std::vector<GDExtensionPtrUtilityFunction> utility_pointers;

// The common case: every argument a number, boolean or Variant holding a
// plain value, and a result of those types. Arguments are written straight
// into flat stack storage (no per-argument slots or destructors). Returns
// false (nothing pushed) to use the general path.
static bool fast_utility(lua_State *L, const UtilityInfo &info, GDExtensionPtrUtilityFunction function, int argc) {
	if (info.vararg || argc != info.argc) {
		return false;
	}
	alignas(16) unsigned char storage[MAX_FAST_ARGS][sizeof(Variant)];
	GDExtensionConstTypePtr argv[MAX_FAST_ARGS];
	for (int i = 0; i < argc; i++) {
		unsigned char *slot = storage[i];
		argv[i] = slot;
		int lt = lua_type(L, 1 + i);
		switch (info.args[i]) {
			case T_FLOAT:
				if (lt != LUA_TNUMBER) return false;
				*reinterpret_cast<double *>(slot) = lua_tonumber(L, 1 + i);
				break;
			case T_INT:
				if (lt != LUA_TNUMBER) return false;
				*reinterpret_cast<int64_t *>(slot) = (int64_t)lua_tonumber(L, 1 + i);
				break;
			case T_BOOL:
				*reinterpret_cast<GDExtensionBool *>(slot) = lua_toboolean(L, 1 + i);
				break;
			case T_VARIANT:
				// Numbers, booleans, nil and vectors as Variant bytes (nothing to
				// destroy); anything else takes the general path
				if (lt != LUA_TNUMBER && lt != LUA_TBOOLEAN && lt != LUA_TNIL && lt != LUA_TVECTOR) return false;
				if (!write_plain_variant(L, 1 + i, slot)) return false;
				break;
			default:
				return false;
		}
	}
	switch (info.ret) {
		case T_VOID:
			function(nullptr, argv, argc);
			lua_pushnil(L);
			return true;
		case T_FLOAT: {
			double d;
			function(&d, argv, argc);
			lua_pushnumber(L, d);
			return true;
		}
		case T_INT: {
			int64_t v;
			function(&v, argv, argc);
			lua_pushnumber(L, (double)v);
			return true;
		}
		case T_BOOL: {
			GDExtensionBool b;
			function(&b, argv, argc);
			lua_pushboolean(L, b);
			return true;
		}
		case T_VARIANT: {
			alignas(Variant) unsigned char result[sizeof(Variant)] = {};  // a nil Variant
			function(result, argv, argc);
			Variant *value = reinterpret_cast<Variant *>(result);
			if (!push_plain_variant(L, *value)) {
				push_variant(L, *value);
			}
			if (needs_destroy(result)) {
				value->~Variant();
			}
			return true;
		}
		default:
			return false;
	}
}

static int utility_call(lua_State *L) {
	size_t index = (size_t)lua_tointeger(L, lua_upvalueindex(1));
	const UtilityInfo &info = UTILITIES[index];
	GDExtensionPtrUtilityFunction function = utility_pointers[index];
	int argc = lua_gettop(L);
	if (fast_utility(L, info, function, argc)) {
		return 1;
	}
	if (info.vararg) {
		if (argc > MAX_VARIANT_ARGS) {
			luaL_error(L, "too many arguments");
		}
		// Variant arguments and result; a zeroed Variant is a valid nil
		alignas(Variant) unsigned char result[sizeof(Variant)] = {};
		{
			VariantArgs args;
			for (int i = 0; i < argc; i++) {
				args.add(L, 1 + i);
			}
			function(result, (const GDExtensionConstTypePtr *)args.argv, argc);
		}
		Variant *value = reinterpret_cast<Variant *>(result);
		push_variant(L, *value);
		if (needs_destroy(result)) {
			value->~Variant();
		}
		return 1;
	}
	if (argc != info.argc) {
		luaL_error(L, "%s expects %d arguments, got %d", info.name, (int)info.argc, argc);
	}
	bool ok = true;
	{
		NativeSlot slots[MAX_FAST_ARGS];
		GDExtensionConstTypePtr argv[MAX_FAST_ARGS];
		for (int i = 0; i < argc && ok; i++) {
			argv[i] = slots[i].bytes;
			ok = info.args[i] != T_OTHER && to_native(L, 1 + i, info.args[i], slots[i], argv[i]);
		}
		if (ok) {
			NativeSlot ret;
			prepare_return(info.ret, ret);
			function(info.ret == T_VOID ? nullptr : ret.bytes, argv, argc);
			if (info.ret == T_OBJECT_REF || info.ret == T_OBJECT) {
				push_object(L, *reinterpret_cast<GDExtensionObjectPtr *>(ret.bytes));
			} else {
				push_native(L, info.ret, ret);
			}
		}
	}
	if (!ok) {
		luaL_error(L, "invalid arguments to %s", info.name);
	}
	return 1;
}

// ---------------------------------------------------------------- engine classes

static int class_new(lua_State *L) {
	ClassInfo *cls = (ClassInfo *)lua_tolightuserdata(L, lua_upvalueindex(1));
	GDExtensionObjectPtr object = gdextension_interface::classdb_construct_object2(cls->name._native_ptr());
	push_object(L, object, false, true);
	return 1;
}

// Missing keys on a class table: integer constants and enum values
// (Node.NOTIFICATION_READY), resolved once and stored
static int class_table_index(lua_State *L) {
	ClassInfo *cls = (ClassInfo *)lua_tolightuserdata(L, lua_upvalueindex(1));
	if (lua_type(L, 2) != LUA_TSTRING) {
		return 0;
	}
	StringName name(lua_tostring(L, 2));
	if (!ClassDB::class_has_integer_constant(cls->name, name)) {
		return 0;
	}
	lua_pushnumber(L, (double)ClassDB::class_get_integer_constant(cls->name, name));
	lua_pushvalue(L, 2);
	lua_pushvalue(L, -2);
	lua_rawset(L, 1);
	return 1;
}

void push_class_table(lua_State *L, ClassInfo *cls) {
	lua_newtable(L);
	lua_pushlightuserdata(L, cls);
	lua_pushcclosurek(L, class_new, "new", 1, nullptr);
	lua_setfield(L, -2, "new");
	lua_newtable(L);
	lua_pushlightuserdata(L, cls);
	lua_pushcclosurek(L, class_table_index, "__index", 1, nullptr);
	lua_setfield(L, -2, "__index");
	lua_setmetatable(L, -2);
}

// ---------------------------------------------------------------- globals: utilities and enums

bool push_builtin_global(lua_State *L, const char *name) {
	if (const UtilityInfo *info = find_utility(name)) {
		size_t index = (size_t)(info - UTILITIES);
		if (utility_pointers[index] == nullptr) {
			StringName function(name);
			utility_pointers[index] = gdextension_interface::variant_get_ptr_utility_function(function._native_ptr(), info->hash);
			if (utility_pointers[index] == nullptr) {
				return false;
			}
		}
		lua_pushinteger(L, (int)index);
		lua_pushcclosurek(L, utility_call, info->name, 1, nullptr);
		return true;
	}
	if (const EnumValue *value = find_global_enum(name)) {
		lua_pushnumber(L, (double)value->value);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------- iteration and length (ADR 0027)

static Variant *variant_at(lua_State *L, int index) {
	return (Variant *)lua_touserdatatagged(L, index, TAG_VARIANT);
}

// Arrays: (index, value) with 0-based indices, like arr[i]
static int indexed_next(lua_State *L) {
	Variant *self = variant_at(L, lua_upvalueindex(1));
	GDExtensionInt i = (GDExtensionInt)lua_tointeger(L, lua_upvalueindex(2));
	GDExtensionBool valid, oob;
	VariantResult result;
	gdextension_interface::variant_get_indexed(self->_native_ptr(), i, result.uninitialized(), &valid, &oob);
	if (!valid || oob) {
		return 0;
	}
	lua_pushinteger(L, (int)(i + 1));
	lua_replace(L, lua_upvalueindex(2));
	lua_pushnumber(L, (double)i);
	push_variant(L, result.get());
	return 2;
}

// Dictionaries: (key, value), over the keys when iteration started
static int dictionary_next(lua_State *L) {
	Variant *self = variant_at(L, lua_upvalueindex(1));
	Variant *keys = variant_at(L, lua_upvalueindex(2));
	GDExtensionInt i = (GDExtensionInt)lua_tointeger(L, lua_upvalueindex(3));
	GDExtensionBool valid, oob;
	VariantResult key;
	gdextension_interface::variant_get_indexed(keys->_native_ptr(), i, key.uninitialized(), &valid, &oob);
	if (!valid || oob) {
		return 0;
	}
	lua_pushinteger(L, (int)(i + 1));
	lua_replace(L, lua_upvalueindex(3));
	VariantResult value;
	gdextension_interface::variant_get_keyed(self->_native_ptr(), key.get()._native_ptr(), value.uninitialized(), &valid);
	push_variant(L, key.get());
	push_variant(L, value.get());
	return 2;
}

static int variant_iter(lua_State *L) {
	Variant *self = variant_at(L, 1);
	Variant::Type type = type_of(*self);
	if (is_indexed_type(type)) {
		lua_pushvalue(L, 1);
		lua_pushinteger(L, 0);
		lua_pushcclosurek(L, indexed_next, "next", 2, nullptr);
		return 1;
	}
	if (type == Variant::DICTIONARY) {
		lua_pushvalue(L, 1);
		{
			VariantResult keys;
			GDExtensionCallError error;
			static const StringName keys_name("keys");
			gdextension_interface::variant_call(self->_native_ptr(), keys_name._native_ptr(), nullptr, 0, keys.uninitialized(), &error);
			push_variant_userdata(L, keys.get());
		}
		lua_pushinteger(L, 0);
		lua_pushcclosurek(L, dictionary_next, "next", 3, nullptr);
		return 1;
	}
	luaL_error(L, "attempt to iterate over a %s value", type_name(type));
	return 0;
}

static int variant_len(lua_State *L) {
	Variant *self = variant_at(L, 1);
	Variant::Type type = type_of(*self);
	if (!is_indexed_type(type) && type != Variant::DICTIONARY) {
		luaL_error(L, "attempt to get length of a %s value", type_name(type));
	}
	static const StringName size_name("size");
	VariantResult result;
	GDExtensionCallError error;
	gdextension_interface::variant_call(self->_native_ptr(), size_name._native_ptr(), nullptr, 0, result.uninitialized(), &error);
	push_variant(L, result.get());
	return 1;
}

// ---------------------------------------------------------------- Lua tables → Array / Dictionary (ADR 0027)

constexpr int MAX_TABLE_DEPTH = 32;

static Variant table_to_variant(lua_State *L, int index, int depth) {
	index = lua_absindex(L, index);
	if (depth > MAX_TABLE_DEPTH) {
		luaL_error(L, "table nested too deeply (or a cycle) to convert to Godot");
	}
	// A sequence (keys 1..n, nothing else) is an Array; anything else a
	// Dictionary. An empty table is an empty Array.
	int length = lua_objlen(L, index);
	int count = 0;
	lua_pushnil(L);
	while (lua_next(L, index)) {
		count++;
		lua_pop(L, 1);
	}
	if (count == length) {
		Array array;
		for (int i = 1; i <= length; i++) {
			lua_rawgeti(L, index, i);
			array.append(table_aware_to_variant(L, -1, depth + 1));
			lua_pop(L, 1);
		}
		return array;
	}
	Dictionary dictionary;
	lua_pushnil(L);
	while (lua_next(L, index)) {
		dictionary[table_aware_to_variant(L, -2, depth + 1)] = table_aware_to_variant(L, -1, depth + 1);
		lua_pop(L, 1);
	}
	return dictionary;
}

Variant table_aware_to_variant(lua_State *L, int index, int depth) {
	if (lua_type(L, index) == LUA_TTABLE && self_table_owner(L, index) == nullptr) {
		return table_to_variant(L, index, depth);
	}
	return to_variant(L, index);
}

// ---------------------------------------------------------------- registration

void register_builtins(lua_State *L) {
	utility_pointers.assign(sizeof(UTILITIES) / sizeof(UTILITIES[0]), nullptr);
	register_builtin_types(L);
	register_vector_methods(L);
	register_string_methods(L);

	lua_getuserdatametatable(L, TAG_VARIANT);
	lua_pushcfunction(L, variant_iter, "__iter");
	lua_setfield(L, -2, "__iter");
	lua_pushcfunction(L, variant_len, "__len");
	lua_setfield(L, -2, "__len");
	lua_pop(L, 1);
}

void clear_builtins() {
	held_names.clear();
	utility_pointers.clear();
}

} // namespace luau
