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
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
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

// Builtin methods with signatures (types as Variant::Type numbers)
constexpr int32_t VARIANT_TYPE_ANY = 1000;
constexpr int32_t VARIANT_TYPE_VOID = 1001;
constexpr int32_t VARIANT_TYPE_UNSUPPORTED = 1002;
enum : uint8_t { BM_VARARG = 1, BM_STATIC = 2, BM_DEFAULTS = 4 };

struct BuiltinMethodInfo {
	const char *type;
	const char *name;
	int64_t hash;
	int32_t ret;
	uint8_t argc;
	int32_t args[MAX_FAST_ARGS];
	uint8_t flags;
};

const BuiltinMethodInfo BUILTIN_METHODS[] = {
#include "builtin_method_data.inc"
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
	lua_pushfstring(L, "%s: %s", what, call_error_text(error).utf8().get_data());
	return -1;
}

} // namespace

// ---------------------------------------------------------------- builtin method pointers (ADR 0029)
//
// Methods on builtin values are called through cached method pointers with
// native argument and result layouts, instead of a lookup by name per call.
// The receiver is the value inside its Variant: inline types (Rect2, Color,
// Array, String…) at the data offset, heap types (Transform2D, Basis…) behind
// the pointer stored there. Packed arrays (behind engine-internal storage),
// varargs and calls with fewer arguments than declared use variant_call.

namespace {

bool builtin_layout_ok = false;
// Packed arrays: the Variant holds a pointer to a reference-counted holder,
// with the array at this offset (checked at startup; off if it fails)
bool packed_layout_ok = false;
constexpr size_t PACKED_ARRAY_OFFSET = 16;  // after the holder's vtable and reference count

enum class BaseKind : uint8_t { NONE, INLINE, HEAP, PACKED };

BaseKind base_kind(Variant::Type type) {
	switch (type) {
		case Variant::VECTOR2I: case Variant::RECT2: case Variant::RECT2I: case Variant::VECTOR3I:
		case Variant::VECTOR4: case Variant::VECTOR4I: case Variant::PLANE: case Variant::QUATERNION:
		case Variant::COLOR: case Variant::RID: case Variant::STRING: case Variant::STRING_NAME:
		case Variant::NODE_PATH: case Variant::CALLABLE: case Variant::SIGNAL: case Variant::DICTIONARY:
		case Variant::ARRAY:
			return BaseKind::INLINE;
		case Variant::TRANSFORM2D: case Variant::AABB: case Variant::BASIS: case Variant::TRANSFORM3D:
		case Variant::PROJECTION:
			return BaseKind::HEAP;
		default:
			if (type >= Variant::PACKED_BYTE_ARRAY && type < Variant::VARIANT_MAX) {
				return packed_layout_ok ? BaseKind::PACKED : BaseKind::NONE;
			}
			return BaseKind::NONE;
	}
}

// Types whose values live inside the Variant's data with no destructor:
// results of these are written straight into a new userdata
bool is_plain_inline(int32_t type) {
	switch (type) {
		case Variant::VECTOR2I: case Variant::RECT2: case Variant::RECT2I: case Variant::VECTOR3I:
		case Variant::VECTOR4: case Variant::VECTOR4I: case Variant::PLANE: case Variant::QUATERNION:
		case Variant::COLOR: case Variant::RID:
			return true;
		default:
			return false;
	}
}

// A pointer to the typed value inside a Variant (receiver or argument)
void *typed_pointer(const Variant *value) {
	unsigned char *data = reinterpret_cast<unsigned char *>(const_cast<Variant *>(value)->_native_ptr()) + VARIANT_DATA;
	switch (base_kind(type_of(*value))) {
		case BaseKind::HEAP:
			return *reinterpret_cast<void **>(data);
		case BaseKind::PACKED:
			return *reinterpret_cast<unsigned char **>(data) + PACKED_ARRAY_OFFSET;
		default:
			return data;
	}
}

struct BuiltinMethod {
	GDExtensionPtrBuiltInMethod function = nullptr;
	const BuiltinMethodInfo *info = nullptr;
};

// Per type, per name atom: resolved on first use (null info: not usable)
std::vector<BuiltinMethod *> builtin_methods[Variant::VARIANT_MAX];

const BuiltinMethodInfo *find_builtin_method(const char *type, const char *name) {
	return find_sorted(BUILTIN_METHODS, [type, name](const BuiltinMethodInfo &m) {
		int c = strcmp(m.type, type);
		return c != 0 ? c : strcmp(m.name, name);
	});
}

const BuiltinMethod &builtin_method(Variant::Type type, int atom) {
	std::vector<BuiltinMethod *> &table = builtin_methods[type];
	if ((size_t)atom >= table.size()) {
		table.resize(atom + 1, nullptr);
	}
	if (table[atom] == nullptr) {
		BuiltinMethod *method = new BuiltinMethod();
		CharString name = String(atom_name(atom)).utf8();
		const BuiltinMethodInfo *info = find_builtin_method(type_name(type), name.get_data());
		bool usable = info && !(info->flags & (BM_VARARG | BM_STATIC)) && info->ret != VARIANT_TYPE_UNSUPPORTED;
		for (int i = 0; usable && i < info->argc && i < MAX_FAST_ARGS; i++) {
			usable = info->args[i] != VARIANT_TYPE_UNSUPPORTED;
		}
		if (usable && info->argc <= MAX_FAST_ARGS) {
			method->function = gdextension_interface::variant_get_ptr_builtin_method((GDExtensionVariantType)type, atom_name(atom)._native_ptr(), info->hash);
			method->info = method->function ? info : nullptr;
		}
		table[atom] = method;
	}
	return *table[atom];
}

// The engine-side argument type for the simple cases to_native handles
ArgType simple_arg(int32_t type) {
	switch (type) {
		case Variant::BOOL: return T_BOOL;
		case Variant::INT: return T_INT;
		case Variant::FLOAT: return T_FLOAT;
		case Variant::STRING: return T_STRING;
		case Variant::STRING_NAME: return T_STRING_NAME;
		case Variant::NODE_PATH: return T_NODE_PATH;
		case Variant::VECTOR2: return T_VECTOR2;
		case Variant::VECTOR3: return T_VECTOR3;
		case Variant::OBJECT: return T_OBJECT_REF;
		case VARIANT_TYPE_ANY: return T_VARIANT;
		default: return T_OTHER;
	}
}

// Per-type default constructor, from-type and destructor, for results that
// aren't plain inline values
struct TypeOps {
	GDExtensionPtrConstructor construct = nullptr;
	GDExtensionVariantFromTypeConstructorFunc to_variant = nullptr;
	GDExtensionPtrDestructor destroy = nullptr;
};
TypeOps type_ops[Variant::VARIANT_MAX];

const TypeOps &ops_for(int32_t type) {
	TypeOps &ops = type_ops[type];
	if (ops.to_variant == nullptr) {
		ops.construct = gdextension_interface::variant_get_ptr_constructor((GDExtensionVariantType)type, 0);
		ops.to_variant = gdextension_interface::get_variant_from_type_constructor((GDExtensionVariantType)type);
		ops.destroy = gdextension_interface::variant_get_ptr_destructor((GDExtensionVariantType)type);
	}
	return ops;
}

} // namespace

bool call_builtin_method(lua_State *L, const Variant *self, int atom, int first, int argc) {
	if (!builtin_layout_ok || atom < 0) {
		return false;
	}
	Variant::Type self_type = type_of(*self);
	if (base_kind(self_type) == BaseKind::NONE) {
		return false;
	}
	const BuiltinMethod &method = builtin_method(self_type, atom);
	const BuiltinMethodInfo *info = method.info;
	if (info == nullptr || argc != info->argc) {
		return false;
	}
	// Arguments
	NativeSlot slots[MAX_FAST_ARGS];
	GDExtensionConstTypePtr argv[MAX_FAST_ARGS];
	for (int i = 0; i < argc; i++) {
		int32_t type = info->args[i];
		argv[i] = slots[i].bytes;
		ArgType simple = simple_arg(type);
		if (simple != T_OTHER) {
			if (!to_native(L, first + i, simple, slots[i], argv[i])) {
				return false;
			}
			continue;
		}
		// Packed values (Vector2i, RID) into the slot
		int tag = packed_tag(L, first + i);
		if (tag == LUTAG_VECTOR2I && type == Variant::VECTOR2I) {
			unpack_vector2i(packed_bits(L, first + i, tag), reinterpret_cast<int32_t *>(slots[i].bytes));
			continue;
		}
		if (tag == LUTAG_RID && type == Variant::RID) {
			*reinterpret_cast<uint64_t *>(slots[i].bytes) = packed_bits(L, first + i, tag);
			continue;
		}
		// Other builtin types: a Godot value of exactly that type, used in place
		const Variant *held = borrow_variant(L, first + i);
		if (held == nullptr || type_of(*held) != (Variant::Type)type || base_kind((Variant::Type)type) == BaseKind::NONE) {
			return false;
		}
		argv[i] = typed_pointer(held);
	}
	void *base = typed_pointer(self);
	int32_t ret = info->ret;
	switch (ret) {
		case VARIANT_TYPE_VOID:
			method.function(base, argv, nullptr, argc);
			lua_pushnil(L);
			return true;
		case Variant::BOOL: {
			GDExtensionBool b = false;
			method.function(base, argv, &b, argc);
			lua_pushboolean(L, b);
			return true;
		}
		case Variant::INT: {
			int64_t v = 0;
			method.function(base, argv, &v, argc);
			lua_pushnumber(L, (double)v);
			return true;
		}
		case Variant::FLOAT: {
			double d = 0;
			method.function(base, argv, &d, argc);
			lua_pushnumber(L, d);
			return true;
		}
		case Variant::VECTOR2: {
			float v[2] = {};
			method.function(base, argv, v, argc);
			lua_pushvector(L, v[0], v[1], 0.0f);
			return true;
		}
		case Variant::VECTOR3: {
			float v[3] = {};
			method.function(base, argv, v, argc);
			lua_pushvector(L, v[0], v[1], v[2]);
			return true;
		}
		case Variant::STRING: {
			alignas(String) unsigned char s[sizeof(String)] = {};  // an empty String
			method.function(base, argv, s, argc);
			push_string(L, *reinterpret_cast<String *>(s));
			reinterpret_cast<String *>(s)->~String();
			return true;
		}
		case Variant::STRING_NAME: {
			alignas(StringName) unsigned char s[sizeof(StringName)] = {};  // an empty StringName
			method.function(base, argv, s, argc);
			push_string_name(L, *reinterpret_cast<StringName *>(s));
			reinterpret_cast<StringName *>(s)->~StringName();
			return true;
		}
		case Variant::OBJECT: {
			GDExtensionObjectPtr object = nullptr;
			method.function(base, argv, &object, argc);
			push_object(L, object);
			return true;
		}
		case Variant::VECTOR2I:
			if (PACKED_VALUES) {
				int32_t r[2] = {};
				method.function(base, argv, r, argc);
				push_packed_vector2i(L, r[0], r[1]);
				return true;
			}
			break;
		case Variant::RID:
			if (PACKED_VALUES) {
				uint64_t r = 0;
				method.function(base, argv, &r, argc);
				push_packed_rid(L, r);
				return true;
			}
			break;

		case VARIANT_TYPE_ANY: {
			alignas(Variant) unsigned char v[sizeof(Variant)] = {};  // a nil Variant
			method.function(base, argv, v, argc);
			Variant *value = reinterpret_cast<Variant *>(v);
			if (!push_plain_variant(L, *value)) {
				push_variant(L, *value);
			}
			if (needs_destroy(v)) {
				value->~Variant();
			}
			return true;
		}
		default:
			break;
	}
	if (is_plain_inline(ret)) {
		// Straight into the new userdata's Variant: type tag, then the value
		unsigned char *box = (unsigned char *)lua_newuserdatataggedwithmetatable(L, sizeof(Variant), TAG_VARIANT);
		memset(box, 0, sizeof(Variant));
		*reinterpret_cast<int32_t *>(box) = ret;
		method.function(base, argv, box + VARIANT_DATA, argc);
		return true;
	}
	// Anything else: construct, call, convert to a Variant in a new userdata
	const TypeOps &ops = ops_for(ret);
	if (ops.construct == nullptr || ops.to_variant == nullptr) {
		return false;
	}
	alignas(16) unsigned char value[64];
	ops.construct(value, nullptr);
	method.function(base, argv, value, argc);
	Variant *box = (Variant *)lua_newuserdatataggedwithmetatable(L, sizeof(Variant), TAG_VARIANT);
	ops.to_variant(box->_native_ptr(), value);
	if (ops.destroy) {
		ops.destroy(value);
	}
	return true;
}

// ---------------------------------------------------------------- validated operators (ADR 0028)
//
// Binary operators involving Godot values go through the engine's validated
// evaluator for the exact operand types, cached per (operator, left, right),
// with operands as native values or pointers into their Variants and the
// result written in place. A vector with z = 0 is a Vector2 unless only
// Vector3 has the operator with the other operand (no retry needed).

namespace {

struct OperatorInfo {
	int32_t left;
	int32_t op;
	int32_t right;
	int32_t ret;
};

const OperatorInfo OPERATORS[] = {
#include "operator_data.inc"
};

struct OperatorEntry {
	GDExtensionPtrOperatorEvaluator function = nullptr;
	int32_t ret = 0;
	bool resolved = false;
};

constexpr int CACHED_OPS = 10;  // Variant::OP_EQUAL (0) .. OP_DIVIDE (9)
OperatorEntry operator_cache[CACHED_OPS][Variant::VARIANT_MAX][Variant::VARIANT_MAX];

const OperatorEntry &operator_entry(int op, int32_t left, int32_t right) {
	OperatorEntry &entry = operator_cache[op][left][right];
	if (!entry.resolved) {
		entry.resolved = true;
		const OperatorInfo *info = find_sorted(OPERATORS, [&](const OperatorInfo &o) {
			if (o.left != left) return o.left < left ? -1 : 1;
			if (o.op != op) return o.op < op ? -1 : 1;
			if (o.right != right) return o.right < right ? -1 : 1;
			return 0;
		});
		if (info) {
			entry.function = gdextension_interface::variant_get_ptr_operator_evaluator(
					(GDExtensionVariantOperator)op, (GDExtensionVariantType)left, (GDExtensionVariantType)right);
			entry.ret = info->ret;
		}
	}
	return entry;
}

struct Operand {
	int32_t type = -1;  // -1: not handled here
	const void *pointer = nullptr;
	bool flat_vector = false;
	alignas(16) unsigned char storage[16];
};

void classify(lua_State *L, int index, Operand &operand) {
	switch (lua_type(L, index)) {
		case LUA_TNUMBER: {
			double d = lua_tonumber(L, index);
			if (d == (double)(int64_t)d && d > -9007199254740992.0 && d < 9007199254740992.0) {
				operand.type = Variant::INT;
				*reinterpret_cast<int64_t *>(operand.storage) = (int64_t)d;
			} else {
				operand.type = Variant::FLOAT;
				*reinterpret_cast<double *>(operand.storage) = d;
			}
			operand.pointer = operand.storage;
			return;
		}
		case LUA_TBOOLEAN:
			operand.type = Variant::BOOL;
			*reinterpret_cast<GDExtensionBool *>(operand.storage) = lua_toboolean(L, index);
			operand.pointer = operand.storage;
			return;
		case LUA_TVECTOR: {
			const float *v = lua_tovector(L, index);
			memcpy(operand.storage, v, sizeof(float) * 3);
			operand.flat_vector = v[2] == 0.0f;
			operand.type = operand.flat_vector ? Variant::VECTOR2 : Variant::VECTOR3;
			operand.pointer = operand.storage;
			return;
		}
		case LUA_TUSERDATA: {
			unsigned char *held = (unsigned char *)lua_touserdatatagged(L, index, TAG_VARIANT);
			if (held == nullptr) {
				return;
			}
			// Type and pointer from the bytes, deciding the kind once
			Variant::Type type = (Variant::Type)*reinterpret_cast<const int32_t *>(held);
			unsigned char *data = held + VARIANT_DATA;
			switch (base_kind(type)) {
				case BaseKind::INLINE: operand.pointer = data; break;
				case BaseKind::HEAP: operand.pointer = *reinterpret_cast<void **>(data); break;
				case BaseKind::PACKED: operand.pointer = *reinterpret_cast<unsigned char **>(data) + PACKED_ARRAY_OFFSET; break;
				default: return;
			}
			operand.type = type;
			return;
		}
		case LUA_TLIGHTUSERDATA: {
			int tag = packed_tag(L, index);
			if (tag == LUTAG_VECTOR2I) {
				operand.type = Variant::VECTOR2I;
				unpack_vector2i(packed_bits(L, index, tag), reinterpret_cast<int32_t *>(operand.storage));
				operand.pointer = operand.storage;
			} else if (tag == LUTAG_RID) {
				operand.type = Variant::RID;
				*reinterpret_cast<uint64_t *>(operand.storage) = packed_bits(L, index, tag);
				operand.pointer = operand.storage;
			}
			return;
		}
		default:
			return;
	}
}

} // namespace

// Vector2i arithmetic directly on packed values: Godot's int32 maths, with
// wrap-around like the engine's. Division stays with the engine (it reports
// division by zero).
static bool packed_vector2i_arith(lua_State *L, int op) {
	if (op != Variant::OP_ADD && op != Variant::OP_SUBTRACT && op != Variant::OP_MULTIPLY) {
		return false;
	}
	int ta = packed_tag(L, 1), tb = packed_tag(L, 2);
	int32_t a[2], b[2];
	if (ta == LUTAG_VECTOR2I && tb == LUTAG_VECTOR2I) {
		unpack_vector2i(packed_bits(L, 1, ta), a);
		unpack_vector2i(packed_bits(L, 2, tb), b);
	} else if (op == Variant::OP_MULTIPLY && ta == LUTAG_VECTOR2I && lua_type(L, 2) == LUA_TNUMBER) {
		double d = lua_tonumber(L, 2);
		if (d != (double)(int64_t)d) return false;  // Vector2i * float is a Vector2: the engine's
		unpack_vector2i(packed_bits(L, 1, ta), a);
		b[0] = b[1] = (int32_t)(int64_t)d;
	} else if (op == Variant::OP_MULTIPLY && tb == LUTAG_VECTOR2I && lua_type(L, 1) == LUA_TNUMBER) {
		double d = lua_tonumber(L, 1);
		if (d != (double)(int64_t)d) return false;
		unpack_vector2i(packed_bits(L, 2, tb), b);
		a[0] = a[1] = (int32_t)(int64_t)d;
	} else {
		return false;
	}
	uint32_t x, y;
	switch (op) {
		case Variant::OP_ADD: x = (uint32_t)a[0] + (uint32_t)b[0]; y = (uint32_t)a[1] + (uint32_t)b[1]; break;
		case Variant::OP_SUBTRACT: x = (uint32_t)a[0] - (uint32_t)b[0]; y = (uint32_t)a[1] - (uint32_t)b[1]; break;
		default: x = (uint32_t)a[0] * (uint32_t)b[0]; y = (uint32_t)a[1] * (uint32_t)b[1]; break;
	}
	push_packed_vector2i(L, (int32_t)x, (int32_t)y);
	return true;
}

// Elementwise arithmetic on builtin values held in Variant userdata, done in
// C with Godot's component types (float32 for Color/Vector4/Quaternion, int32
// for Vector3i/Vector4i). Covers what the engine also does elementwise; the
// rest (Quaternion * Quaternion, integer division, which reports division by
// zero) goes to the engine.
namespace {

struct Elementwise {
	Variant::Type type;
	int count;       // components
	bool integer;    // int32 components (else float32)
	bool by_value;   // same-type operands allowed for * and / (not for Quaternion)
};

const Elementwise *elementwise_info(int32_t type) {
	static const Elementwise color = { Variant::COLOR, 4, false, true };
	static const Elementwise vector4 = { Variant::VECTOR4, 4, false, true };
	static const Elementwise quaternion = { Variant::QUATERNION, 4, false, false };
	static const Elementwise vector3i = { Variant::VECTOR3I, 3, true, true };
	static const Elementwise vector4i = { Variant::VECTOR4I, 4, true, true };
	switch (type) {
		case Variant::COLOR: return &color;
		case Variant::VECTOR4: return &vector4;
		case Variant::QUATERNION: return &quaternion;
		case Variant::VECTOR3I: return &vector3i;
		case Variant::VECTOR4I: return &vector4i;
		default: return nullptr;
	}
}

// The Variant data of `held` if it holds exactly `type`, or null
inline const unsigned char *data_if(const unsigned char *held, int32_t type) {
	return held && *reinterpret_cast<const int32_t *>(held) == type ? held + VARIANT_DATA : nullptr;
}

} // namespace

static bool elementwise_arith(lua_State *L, int op) {
	if (op != Variant::OP_ADD && op != Variant::OP_SUBTRACT && op != Variant::OP_MULTIPLY && op != Variant::OP_DIVIDE) {
		return false;
	}
	// The value operand decides the type
	const unsigned char *held_a = (const unsigned char *)lua_touserdatatagged(L, 1, TAG_VARIANT);
	const unsigned char *held_b = (const unsigned char *)lua_touserdatatagged(L, 2, TAG_VARIANT);
	const unsigned char *any = held_a ? held_a : held_b;
	if (any == nullptr) {
		return false;
	}
	const Elementwise *info = elementwise_info(*reinterpret_cast<const int32_t *>(any));
	if (info == nullptr) {
		return false;
	}
	const unsigned char *a = data_if(held_a, info->type);
	const unsigned char *b = data_if(held_b, info->type);
	bool scalar_a = a == nullptr, scalar_b = b == nullptr;
	if (scalar_a && scalar_b) {
		return false;
	}
	double scalar = 0;
	if (scalar_a || scalar_b) {
		int index = scalar_a ? 1 : 2;
		if (lua_type(L, index) != LUA_TNUMBER) return false;
		scalar = lua_tonumber(L, index);
		// value * n, n * value, value / n only
		if (op == Variant::OP_ADD || op == Variant::OP_SUBTRACT) return false;
		if (op == Variant::OP_DIVIDE && scalar_b == false) return false;
		if (info->integer) {
			// integer vectors: only * by an integer (Vector3i * 1.5 is a Vector3)
			if (op != Variant::OP_MULTIPLY || scalar != (double)(int64_t)scalar) return false;
		}
	} else if ((op == Variant::OP_MULTIPLY || op == Variant::OP_DIVIDE) && !info->by_value) {
		return false;
	}
	if (info->integer && op == Variant::OP_DIVIDE) {
		return false;  // the engine reports division by zero
	}
	unsigned char *box = (unsigned char *)lua_newuserdatataggedwithmetatable(L, sizeof(Variant), TAG_VARIANT);
	*reinterpret_cast<int64_t *>(box) = info->type;  // type tag and padding
	unsigned char *out = box + VARIANT_DATA;
	if (info->count == 3) {
		reinterpret_cast<int32_t *>(out)[3] = 0;  // the rest of the data
	}
	for (int i = 0; i < info->count; i++) {
		if (info->integer) {
			uint32_t x = scalar_a ? (uint32_t)(int32_t)(int64_t)scalar : (uint32_t)reinterpret_cast<const int32_t *>(a)[i];
			uint32_t y = scalar_b ? (uint32_t)(int32_t)(int64_t)scalar : (uint32_t)reinterpret_cast<const int32_t *>(b)[i];
			uint32_t r = op == Variant::OP_ADD ? x + y : op == Variant::OP_SUBTRACT ? x - y : x * y;
			reinterpret_cast<int32_t *>(out)[i] = (int32_t)r;
		} else {
			float x = scalar_a ? (float)scalar : reinterpret_cast<const float *>(a)[i];
			float y = scalar_b ? (float)scalar : reinterpret_cast<const float *>(b)[i];
			float r;
			if (op == Variant::OP_DIVIDE && scalar_b && info->type == Variant::QUATERNION) {
				r = x * (1.0f / y);  // Godot's Quaternion / scalar multiplies by the reciprocal
			} else {
				r = op == Variant::OP_ADD ? x + y : op == Variant::OP_SUBTRACT ? x - y : op == Variant::OP_MULTIPLY ? x * y : x / y;
			}
			reinterpret_cast<float *>(out)[i] = r;
		}
	}
	return true;
}

bool call_validated_operator(lua_State *L, int op) {
	// Packed values (light userdata) or values in userdata: try the matching
	// direct path first
	if (PACKED_VALUES && (lua_type(L, 1) == LUA_TLIGHTUSERDATA || lua_type(L, 2) == LUA_TLIGHTUSERDATA)) {
		if (packed_vector2i_arith(L, op)) {
			return true;
		}
	} else if (elementwise_arith(L, op)) {
		return true;
	}
	if (!builtin_layout_ok || op < 0 || op >= CACHED_OPS) {
		return false;
	}
	Operand a, b;
	classify(L, 1, a);
	classify(L, 2, b);
	if (a.type < 0 || b.type < 0) {
		return false;
	}
	const OperatorEntry *entry = &operator_entry(op, a.type, b.type);
	// Vectors with z = 0: Vector2 first, else Vector3 (decided, not retried)
	if (entry->function == nullptr && (a.flat_vector || b.flat_vector)) {
		int32_t ta = a.flat_vector ? (int32_t)Variant::VECTOR3 : a.type;
		int32_t tb = b.flat_vector ? (int32_t)Variant::VECTOR3 : b.type;
		entry = &operator_entry(op, ta, tb);
	}
	if (entry->function == nullptr) {
		return false;
	}
	int32_t ret = entry->ret;
	switch (ret) {
		case Variant::BOOL: {
			GDExtensionBool r = false;
			entry->function(a.pointer, b.pointer, &r);
			lua_pushboolean(L, r);
			return true;
		}
		case Variant::INT: {
			int64_t r = 0;
			entry->function(a.pointer, b.pointer, &r);
			lua_pushnumber(L, (double)r);
			return true;
		}
		case Variant::FLOAT: {
			double r = 0;
			entry->function(a.pointer, b.pointer, &r);
			lua_pushnumber(L, r);
			return true;
		}
		case Variant::VECTOR2: {
			float r[2] = {};
			entry->function(a.pointer, b.pointer, r);
			lua_pushvector(L, r[0], r[1], 0.0f);
			return true;
		}
		case Variant::VECTOR3: {
			float r[3] = {};
			entry->function(a.pointer, b.pointer, r);
			lua_pushvector(L, r[0], r[1], r[2]);
			return true;
		}
		case Variant::VECTOR2I:
			if (PACKED_VALUES) {
				int32_t r[2] = {};
				entry->function(a.pointer, b.pointer, r);
				push_packed_vector2i(L, r[0], r[1]);
				return true;
			}
			break;
		case Variant::RID:
			if (PACKED_VALUES) {
				uint64_t r = 0;
				entry->function(a.pointer, b.pointer, &r);
				push_packed_rid(L, r);
				return true;
			}
			break;
		default:
			break;
	}
	if (is_plain_inline(ret)) {
		unsigned char *box = (unsigned char *)lua_newuserdatataggedwithmetatable(L, sizeof(Variant), TAG_VARIANT);
		memset(box, 0, sizeof(Variant));
		*reinterpret_cast<int32_t *>(box) = ret;
		entry->function(a.pointer, b.pointer, box + VARIANT_DATA);
		return true;
	}
	const TypeOps &ops = ops_for(ret);
	if (ops.construct == nullptr || ops.to_variant == nullptr) {
		return false;
	}
	alignas(16) unsigned char value[64];
	ops.construct(value, nullptr);
	entry->function(a.pointer, b.pointer, value);
	Variant *box = (Variant *)lua_newuserdatataggedwithmetatable(L, sizeof(Variant), TAG_VARIANT);
	ops.to_variant(box->_native_ptr(), value);
	if (ops.destroy) {
		ops.destroy(value);
	}
	return true;
}

static void check_builtin_layout() {
	// An inline reference type: an Array's data is its Array (copies share
	// the same internal pointer). A heap type: the data points at the value.
	Array array;
	array.append(1);
	Variant av = array;
	Transform2D t(0.5, Vector2(3, 4));
	Variant tv = t;
	builtin_layout_ok = type_of(av) == Variant::ARRAY && type_of(tv) == Variant::TRANSFORM2D
			&& *reinterpret_cast<void **>(typed_pointer(&av)) == *reinterpret_cast<const void *const *>(array._native_ptr())
			&& *reinterpret_cast<const Transform2D *>(typed_pointer(&tv)) == t;
	if (!builtin_layout_ok) {
		return;
	}
	// Packed arrays: the candidate pointer must give the right size through
	// the size() method pointer, and share the array's data with a copy
	PackedFloat32Array packed;
	packed.push_back(1.5f);
	packed.push_back(2.5f);
	packed.push_back(3.5f);
	Variant pv = packed;
	static const StringName size_name("size");
	GDExtensionPtrBuiltInMethod size = gdextension_interface::variant_get_ptr_builtin_method(
			GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY, size_name._native_ptr(), 3173160232LL);
	if (type_of(pv) != Variant::PACKED_FLOAT32_ARRAY || size == nullptr) {
		return;
	}
	packed_layout_ok = true;  // so typed_pointer gives the candidate
	unsigned char *candidate = (unsigned char *)typed_pointer(&pv);
	int64_t count = -1;
	size(candidate, nullptr, &count, 0);
	// Both hold the same copy-on-write buffer (no write happened since)
	const void *const *a = reinterpret_cast<const void *const *>(candidate);
	const void *const *b = reinterpret_cast<const void *const *>(packed._native_ptr());
	packed_layout_ok = count == 3 && a[1] == b[1] && a[1] != nullptr;
}

// ---------------------------------------------------------------- builtin type globals (ADR 0024)

// Direct construction from numbers for plain types, matching Godot's
// component constructors: written straight into place, no engine call.
// False if the arguments don't fit (the engine picks the constructor).
static bool construct_from_numbers(lua_State *L, Variant::Type type, int argc) {
	double n[4];
	if (argc > 4) {
		return false;
	}
	for (int i = 0; i < argc; i++) {
		if (lua_type(L, 2 + i) != LUA_TNUMBER) {
			return false;
		}
		n[i] = lua_tonumber(L, 2 + i);
	}
	if (PACKED_VALUES && type == Variant::VECTOR2I && argc == 2) {
		push_packed_vector2i(L, (int32_t)n[0], (int32_t)n[1]);
		return true;
	}
	if (PACKED_VALUES && type == Variant::RID && argc == 0) {
		push_packed_rid(L, 0);
		return true;
	}
	int floats = 0, ints = 0;
	switch (type) {
		case Variant::COLOR: if (argc == 3 || argc == 4) floats = 4; break;
		case Variant::RECT2: case Variant::VECTOR4: case Variant::PLANE: case Variant::QUATERNION:
			if (argc == 4) floats = 4;
			break;
		case Variant::RECT2I: case Variant::VECTOR4I: if (argc == 4) ints = 4; break;
		case Variant::VECTOR3I: if (argc == 3) ints = 3; break;
		default: break;
	}
	if (floats == 0 && ints == 0) {
		return false;
	}
	unsigned char *box = (unsigned char *)lua_newuserdatataggedwithmetatable(L, sizeof(Variant), TAG_VARIANT);
	memset(box, 0, sizeof(Variant));
	*reinterpret_cast<int32_t *>(box) = type;
	unsigned char *data = box + VARIANT_DATA;
	for (int i = 0; i < floats; i++) {
		// Color(r, g, b) has alpha 1
		reinterpret_cast<float *>(data)[i] = i < argc ? (float)n[i] : 1.0f;
	}
	for (int i = 0; i < ints; i++) {
		reinterpret_cast<int32_t *>(data)[i] = (int32_t)n[i];
	}
	return true;
}

// Type(...): Godot picks the constructor matching the arguments
static int builtin_construct(lua_State *L) {
	Variant::Type type = (Variant::Type)lua_tointeger(L, lua_upvalueindex(1));
	int argc = lua_gettop(L) - 1;  // 1 is the type table
	if (variant_layout_checked() && construct_from_numbers(L, type, argc)) {
		return 1;
	}
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

static int string_method_call(lua_State *L);

// Common Godot String methods implemented directly on UTF-8 (no conversion to
// a Godot String, no engine call). Same results as Godot's for valid UTF-8;
// case conversion handles ASCII here and leaves other text to the engine.
namespace {

int str_begins_with(lua_State *L) {
	size_t n, m;
	const char *s = luaL_checklstring(L, 1, &n);
	const char *p = luaL_checklstring(L, 2, &m);
	lua_pushboolean(L, m <= n && memcmp(s, p, m) == 0);
	return 1;
}

int str_ends_with(lua_State *L) {
	size_t n, m;
	const char *s = luaL_checklstring(L, 1, &n);
	const char *p = luaL_checklstring(L, 2, &m);
	lua_pushboolean(L, m <= n && memcmp(s + n - m, p, m) == 0);
	return 1;
}

const char *find_bytes(const char *s, size_t n, const char *p, size_t m) {
	if (m == 0) {
		return s;
	}
	for (size_t i = 0; i + m <= n; i++) {
		if (s[i] == p[0] && memcmp(s + i, p, m) == 0) {
			return s + i;
		}
	}
	return nullptr;
}

int str_contains(lua_State *L) {
	size_t n, m;
	const char *s = luaL_checklstring(L, 1, &n);
	const char *p = luaL_checklstring(L, 2, &m);
	lua_pushboolean(L, find_bytes(s, n, p, m) != nullptr);
	return 1;
}

int str_is_empty(lua_State *L) {
	size_t n;
	luaL_checklstring(L, 1, &n);
	lua_pushboolean(L, n == 0);
	return 1;
}

bool is_ascii(const char *s, size_t n) {
	for (size_t i = 0; i < n; i++) {
		if ((unsigned char)s[i] >= 0x80) {
			return false;
		}
	}
	return true;
}

// to_upper / to_lower: ASCII here, other text through the engine
template <bool UPPER>
int str_case(lua_State *L) {
	size_t n;
	const char *s = luaL_checklstring(L, 1, &n);
	if (!is_ascii(s, n)) {
		lua_pushvalue(L, lua_upvalueindex(1));  // the engine-backed method
		lua_insert(L, 1);
		lua_call(L, lua_gettop(L) - 1, 1);
		return 1;
	}
	luaL_Strbuf buffer;
	char *out = luaL_buffinitsize(L, &buffer, n);
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		out[i] = UPPER ? (c >= 'a' && c <= 'z' ? c - 32 : c) : (c >= 'A' && c <= 'Z' ? c + 32 : c);
	}
	luaL_pushresultsize(&buffer, n);
	return 1;
}

// strip_edges(left = true, right = true): Godot strips characters <= 32
int str_strip_edges(lua_State *L) {
	size_t n;
	const char *s = luaL_checklstring(L, 1, &n);
	bool left = lua_isnoneornil(L, 2) || lua_toboolean(L, 2);
	bool right = lua_isnoneornil(L, 3) || lua_toboolean(L, 3);
	size_t begin = 0, end = n;
	while (left && begin < end && (unsigned char)s[begin] <= 32) begin++;
	while (right && end > begin && (unsigned char)s[end - 1] <= 32) end--;
	lua_pushlstring(L, s + begin, end - begin);
	return 1;
}

// replace(what, forwhat): every occurrence, left to right
int str_replace(lua_State *L) {
	size_t n, m, r;
	const char *s = luaL_checklstring(L, 1, &n);
	const char *what = luaL_checklstring(L, 2, &m);
	const char *with = luaL_checklstring(L, 3, &r);
	if (m == 0) {
		lua_pushvalue(L, 1);
		return 1;
	}
	luaL_Strbuf buffer;
	luaL_buffinit(L, &buffer);
	const char *at = s, *end = s + n;
	while (const char *found = find_bytes(at, (size_t)(end - at), what, m)) {
		luaL_addlstring(&buffer, at, (size_t)(found - at));
		luaL_addlstring(&buffer, with, r);
		at = found + m;
	}
	luaL_addlstring(&buffer, at, (size_t)(end - at));
	luaL_pushresult(&buffer);
	return 1;
}

} // namespace

static void push_engine_string_method(lua_State *L, const char *name) {
	lua_pushlightuserdata(L, (void *)hold_name(name));
	lua_pushcclosurek(L, string_method_call, name, 1, nullptr);
}

static void register_string_methods(lua_State *L) {
	lua_getglobal(L, "string");
	const luaL_Reg direct[] = {
		{ "begins_with", str_begins_with }, { "ends_with", str_ends_with }, { "contains", str_contains },
		{ "is_empty", str_is_empty }, { "strip_edges", str_strip_edges }, { "replace", str_replace },
		{ nullptr, nullptr },
	};
	for (const luaL_Reg *r = direct; r->name; r++) {
		lua_pushcfunction(L, r->func, r->name);
		lua_setfield(L, -2, r->name);
	}
	push_engine_string_method(L, "to_upper");
	lua_pushcclosurek(L, str_case<true>, "to_upper", 1, nullptr);
	lua_setfield(L, -2, "to_upper");
	push_engine_string_method(L, "to_lower");
	lua_pushcclosurek(L, str_case<false>, "to_lower", 1, nullptr);
	lua_setfield(L, -2, "to_lower");
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

static Variant table_to_variant(lua_State *L, int index, int depth);

// The Lua value at `index` into `slot`, an element that holds nil (a fresh
// Array or Dictionary entry): plain values as bytes, without godot-cpp
// Variant temporaries (each an engine call, docs/adr/0040)
static void write_element(lua_State *L, int index, Variant *slot, int depth) {
	if (lua_type(L, index) == LUA_TTABLE && self_table_owner(L, index) == nullptr) {
		new (slot) Variant(table_to_variant(L, index, depth));  // slot held nil
	} else if (!write_plain_variant(L, index, slot)) {
		new (slot) Variant(to_variant(L, index));
	}
}

// Whether the table may be a sequence (keys 1..n and nothing else). Usually
// one lua_next: after key n, a sequence held in the array part has nothing.
// Lua's length can be any border, so 1..n may still have holes: the caller
// checks each element.
static bool is_sequence(lua_State *L, int index, int length) {
	if (length == 0) {
		lua_pushnil(L);
	} else {
		lua_pushinteger(L, length);
	}
	if (!lua_next(L, index)) {
		return true;
	}
	lua_pop(L, 2);
	// Something after n: count every key (elements may live in the hash part)
	int count = 0;
	lua_pushnil(L);
	while (lua_next(L, index)) {
		count++;
		lua_pop(L, 1);
	}
	return count == length;
}

static Variant table_to_variant(lua_State *L, int index, int depth) {
	index = lua_absindex(L, index);
	if (depth > MAX_TABLE_DEPTH) {
		luaL_error(L, "table nested too deeply (or a cycle) to convert to Godot");
	}
	// A sequence (keys 1..n, nothing else) is an Array; anything else a
	// Dictionary. An empty table is an empty Array.
	int length = lua_objlen(L, index);
	if (is_sequence(L, index, length)) {
		Array array;
		if (length == 0) {
			return array;
		}
		// Sized once, then each element written in place. Godot keeps the
		// elements contiguous; checked by the last element's address.
		array.resize(length);
		Variant *first = (Variant *)gdextension_interface::array_operator_index(array._native_ptr(), 0);
		Variant *last = (Variant *)gdextension_interface::array_operator_index(array._native_ptr(), length - 1);
		bool contiguous = variant_layout_checked() && last == first + (length - 1);
		bool holes = false;
		for (int i = 1; i <= length; i++) {
			if (lua_rawgeti(L, index, i) == LUA_TNIL) {
				lua_pop(L, 1);
				holes = true;  // {1, 2, nil, 4}: a Dictionary
				break;
			}
			Variant *slot = contiguous ? first + (i - 1) : (Variant *)gdextension_interface::array_operator_index(array._native_ptr(), i - 1);
			if (variant_layout_checked()) {
				write_element(L, -1, slot, depth + 1);  // the slot holds nil: nothing to destroy
			} else {
				*slot = table_aware_to_variant(L, -1, depth + 1);
			}
			lua_pop(L, 1);
		}
		if (!holes) {
			return array;
		}
	}
	Dictionary dictionary;
	lua_pushnil(L);
	while (lua_next(L, index)) {
		Variant key = table_aware_to_variant(L, -2, depth + 1);
		Variant *slot = (Variant *)gdextension_interface::dictionary_operator_index(dictionary._native_ptr(), key._native_ptr());
		*slot = table_aware_to_variant(L, -1, depth + 1);
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

// ---------------------------------------------------------------- packed values (ADR 0030)

namespace {

// The packed value at `index` as a plain Variant in `bytes` (no destructor)
bool packed_as_variant(lua_State *L, int index, unsigned char *bytes) {
	memset(bytes, 0, sizeof(Variant));
	return packed_tag(L, index) && write_plain_variant(L, index, bytes);
}

// value:method(...) on a packed value: through the cached method pointer,
// or variant_call
int packed_method_call(lua_State *L) {
	int atom = (int)lua_tointeger(L, lua_upvalueindex(1));
	alignas(Variant) unsigned char self[sizeof(Variant)];
	if (!packed_as_variant(L, 1, self)) {
		luaL_error(L, "call methods with ':'");
	}
	const Variant *value = reinterpret_cast<const Variant *>(self);
	int argc = lua_gettop(L) - 1;
	if (call_builtin_method(L, value, atom, 2, argc)) {
		return 1;
	}
	if (argc > MAX_VARIANT_ARGS) {
		luaL_error(L, "too many arguments");
	}
	int status;
	{
		VariantResult result;
		GDExtensionCallError error;
		call_with_vector_retry(L, 2, argc, result, error, [&](VariantArgs &args, VariantResult &r, GDExtensionCallError &e) {
			gdextension_interface::variant_call(const_cast<Variant *>(value)->_native_ptr(), atom_name(atom)._native_ptr(), args.pointers(), argc, r.uninitialized(), &e);
		});
		status = finish_call(L, result, error, "method");
	}
	if (status < 0) {
		lua_error(L);
	}
	return status;
}

// Fields (x, y) and methods; methods are cached per tag in the upvalue table
int packed_index(lua_State *L) {
	int tag = packed_tag(L, 1);
	if (lua_type(L, 2) != LUA_TSTRING || tag == 0) {
		return 0;
	}
	size_t length;
	const char *key = lua_tolstring(L, 2, &length);
	if (tag == LUTAG_VECTOR2I && length == 1 && (key[0] == 'x' || key[0] == 'y')) {
		int32_t v[2];
		unpack_vector2i(packed_bits(L, 1, tag), v);
		lua_pushnumber(L, key[0] == 'x' ? v[0] : v[1]);
		return 1;
	}
	lua_rawgeti(L, lua_upvalueindex(1), tag);  // the methods table for this tag
	lua_pushvalue(L, 2);
	if (lua_rawget(L, -2) != LUA_TNIL) {
		return 1;
	}
	lua_pop(L, 1);
	int atom = string_atom(L, 2);
	const BuiltinMember *member = find_builtin_member(tag == LUTAG_VECTOR2I ? "Vector2i" : "RID", key);
	if (atom < 0 || member == nullptr || member->kind != B_METHOD) {
		return 0;
	}
	lua_pushinteger(L, atom);
	lua_pushcclosurek(L, packed_method_call, key, 1, nullptr);
	lua_pushvalue(L, 2);
	lua_pushvalue(L, -2);
	lua_rawset(L, -4);
	return 1;
}

int packed_tostring(lua_State *L) {
	alignas(Variant) unsigned char bytes[sizeof(Variant)];
	if (!packed_as_variant(L, 1, bytes)) {
		lua_pushstring(L, "<lightuserdata>");
		return 1;
	}
	push_string(L, reinterpret_cast<const Variant *>(bytes)->stringify());
	return 1;
}

} // namespace

static void register_packed_values(lua_State *L) {
	if (!PACKED_VALUES) {
		return;
	}
	lua_newtable(L);  // the light userdata metatable
	lua_newtable(L);  // methods per tag
	lua_newtable(L);
	lua_rawseti(L, -2, LUTAG_VECTOR2I);
	lua_newtable(L);
	lua_rawseti(L, -2, LUTAG_RID);
	lua_pushcclosurek(L, packed_index, "__index", 1, nullptr);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, packed_tostring, "__tostring");
	lua_setfield(L, -2, "__tostring");
	set_operator_metamethods(L, -1);
	lua_pushlightuserdatatagged(L, nullptr, LUTAG_VECTOR2I);
	lua_insert(L, -2);
	lua_setmetatable(L, -2);  // sets the metatable shared by all light userdata
	lua_pop(L, 1);
}

// ---------------------------------------------------------------- registration

void register_builtins(lua_State *L) {
	utility_pointers.assign(sizeof(UTILITIES) / sizeof(UTILITIES[0]), nullptr);
	check_builtin_layout();
	register_builtin_types(L);
	register_vector_methods(L);
	register_string_methods(L);
	register_packed_values(L);

	lua_getuserdatametatable(L, TAG_VARIANT);
	lua_pushcfunction(L, variant_iter, "__iter");
	lua_setfield(L, -2, "__iter");
	lua_pushcfunction(L, variant_len, "__len");
	lua_setfield(L, -2, "__len");
	lua_pop(L, 1);
}

void clear_builtins() {
	for (std::vector<BuiltinMethod *> &table : builtin_methods) {
		for (BuiltinMethod *method : table) {
			delete method;
		}
		table.clear();
	}
	held_names.clear();
	utility_pointers.clear();
}

} // namespace luau
