// Godot's API surface beyond objects (builtins.cpp)
#pragma once

#include "api.h"

namespace luau {

// Builtin type globals, vector and string methods, iteration (called from
// register_globals once the Variant userdata metatable exists)
void register_builtins(lua_State *L);
void clear_builtins();

// Pushes a utility function or global enum value named `name`; false if
// there's none
bool push_builtin_global(lua_State *L, const char *name);

// Calls a builtin method (name atom `atom`) on `self` through a cached method
// pointer, with Lua arguments [first, first + argc); pushes the result.
// False (nothing pushed) if this call needs variant_call instead.
bool call_builtin_method(lua_State *L, const Variant *self, int atom, int first, int argc);

// Pushes the table for an engine class: `new` and its constants
void push_class_table(lua_State *L, ClassInfo *cls);

// to_variant that also converts plain Lua tables: a sequence to an Array,
// anything else to a Dictionary (docs/adr/0027)
Variant table_aware_to_variant(lua_State *L, int index, int depth);

} // namespace luau
