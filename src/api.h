// Core of the Luau binding: the Luau state, name atoms, engine class members
// and conversions between Godot and Luau values.
#pragma once

#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <lua.h>

#include <cstdint>
#include <vector>

namespace luau {

using godot::StringName;
using godot::Variant;

// ---- The Luau state (one per language, main thread only)
lua_State *state();
// Compiles and loads a chunk (pushes the function, or an error message)
bool load_chunk(lua_State *L, const godot::String &source, const godot::String &chunkname);
// Fennel source → Lua source (or the error message in r_lua)
bool compile_fennel(lua_State *L, const godot::String &source, const godot::String &path, godot::String &r_lua);
void open_state();
void close_state();

// ---- Atoms: every short string in Luau gets a small integer id when it's
// created (lua_Callbacks::useratom), so names used as keys or method names
// map to engine names by array index instead of hashing.
const StringName &atom_name(int atom);  // the StringName for an atom
// The atom of the string at `index`, or -1 (not a string, or too many atoms)
int string_atom(lua_State *L, int index);

// ---- Engine methods, from the generated table (tools/gen_api_data.py)
enum ArgType : uint8_t {
	T_VOID, T_BOOL, T_INT, T_FLOAT, T_STRING, T_STRING_NAME,
	T_VECTOR2, T_VECTOR2I, T_VECTOR3, T_VECTOR3I, T_RECT2, T_COLOR,
	T_OBJECT, T_VARIANT, T_OTHER,
};
enum MethodFlags : uint8_t { F_VARARG = 1, F_STATIC = 2, F_DEFAULTS = 4, F_OTHER = 8 };
constexpr int MAX_FAST_ARGS = 8;

struct MethodInfo {
	const char *class_name;
	const char *method;
	int64_t hash;
	ArgType ret;
	uint8_t argc;
	ArgType args[MAX_FAST_ARGS];
	uint8_t flags;
};

struct Method {
	GDExtensionMethodBindPtr bind = nullptr;
	const MethodInfo *info = nullptr;
	// All arguments and the result have a native layout: called with ptrcall,
	// no Variants involved
	bool ptrcall = false;
};

// What a name means on an engine class
enum class MemberKind : uint8_t { UNKNOWN, METHOD, PROPERTY };
struct Member {
	MemberKind kind = MemberKind::UNKNOWN;
	Method method;  // METHOD
	Method getter;  // PROPERTY
	Method setter;  // PROPERTY
	StringName name;
};

// Per engine class: members indexed by atom, resolved on first use
struct ClassInfo {
	StringName name;
	bool is_ref_counted = false;
	std::vector<Member *> by_atom;
	godot::HashMap<StringName, Member *> by_name;  // names without atoms
	const Member &member(int atom);
	const Member &member(const StringName &name);  // for names without an atom
};
ClassInfo *class_info(const StringName &class_name);
ClassInfo *class_info_of(GDExtensionObjectPtr object);
void clear_class_infos();

// Calls `method` on `object` with Luau arguments [first, first + argc) and
// pushes the result. Returns false (pushing an error message) on failure.
bool call_method(lua_State *L, GDExtensionObjectPtr object, const Method &method, int first, int argc);
// Pushes `object`'s property through its getter / sets it from the value at `index`
bool get_property(lua_State *L, GDExtensionObjectPtr object, const Member &member);
bool set_property(lua_State *L, GDExtensionObjectPtr object, const Member &member, int index);

// Pushes a function calling `member`'s method on its first argument
void push_member_method(lua_State *L, const Member &member);
// Object.get(name), for names that aren't engine members
void push_object_get(lua_State *L, GDExtensionObjectPtr object, const StringName &name);

// ---- Values
void push_variant(lua_State *L, const Variant &value);
Variant to_variant(lua_State *L, int index);
// Writes the value at `index` into `r_dest`, which must hold nil (as Godot's
// return slots do). Moves bytes instead of using godot-cpp's Variant move,
// which swaps byte by byte out of line (docs/adr/0002).
void to_variant_into_nil(lua_State *L, int index, Variant *r_dest);
// Pushes an engine object as Luau userdata (or the script's self table)
// `never_freed`: engine singletons, which live until shutdown
void push_object(lua_State *L, GDExtensionObjectPtr object, bool never_freed = false);
// The engine object of the value at `index` (object userdata or self table),
// or null
GDExtensionObjectPtr to_object(lua_State *L, int index);

// Userdata tags
enum : int { TAG_OBJECT = 1, TAG_VARIANT = 2 };

// Registers the userdata metatables and globals (Vector2, Engine, …)
void register_globals(lua_State *L);

} // namespace luau
