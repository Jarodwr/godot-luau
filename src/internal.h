// Internals shared by the binding's source files (api.cpp, builtins.cpp):
// argument and result storage for engine calls, and value conversions.
#pragma once

#include "api.h"

#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>

#include <new>

namespace luau {

using godot::NodePath;
using godot::String;

// Godot's Variant: { int32 type; padding; union data }, data at offset 8.
// Read and written directly only when check_variant_layout() passed.
constexpr size_t VARIANT_DATA = 8;

// One argument or result in its native ptrcall layout
struct NativeSlot {
	alignas(16) unsigned char bytes[sizeof(Variant)];
	ArgType constructed = T_VOID;  // which object to destroy, if any

	~NativeSlot() {
		switch (constructed) {
			case T_STRING: reinterpret_cast<String *>(bytes)->~String(); break;
			case T_STRING_NAME: reinterpret_cast<StringName *>(bytes)->~StringName(); break;
			case T_VARIANT: reinterpret_cast<Variant *>(bytes)->~Variant(); break;
			case T_NODE_PATH: reinterpret_cast<NodePath *>(bytes)->~NodePath(); break;
			default: break;
		}
	}
};

constexpr int MAX_VARIANT_ARGS = 16;

bool write_plain_variant(lua_State *L, int index, void *memory);
bool needs_destroy(const void *variant_bytes);

// Luau values as call arguments in raw storage: only the ones passed are
// constructed, plain values are written as bytes (docs/adr/0015)
struct VariantArgs {
	alignas(Variant) unsigned char storage[MAX_VARIANT_ARGS][sizeof(Variant)];
	const Variant *argv[MAX_VARIANT_ARGS];
	int count = 0;

	VariantArgs() = default;
	VariantArgs(const VariantArgs &) = delete;
	~VariantArgs() {
		for (int i = 0; i < count; i++) {
			if (needs_destroy(storage[i])) {
				reinterpret_cast<Variant *>(storage[i])->~Variant();
			}
		}
	}

	// Appends the Luau value at `index`
	void add(lua_State *L, int index) {
		void *slot = storage[count];
		if (!write_plain_variant(L, index, slot)) {
			new (slot) Variant(to_variant(L, index));
		}
		argv[count] = reinterpret_cast<const Variant *>(slot);
		count++;
	}

	const GDExtensionConstVariantPtr *pointers() const { return (const GDExtensionConstVariantPtr *)argv; }
};

// A result the engine constructs (the interface's return pointers are
// uninitialized)
struct VariantResult {
	alignas(Variant) unsigned char storage[sizeof(Variant)];
	bool constructed = false;

	VariantResult() = default;
	VariantResult(const VariantResult &) = delete;
	~VariantResult() {
		if (constructed && needs_destroy(storage)) {
			get().~Variant();
		}
	}
	GDExtensionUninitializedVariantPtr uninitialized() {
		constructed = true;
		return storage;
	}
	Variant &get() { return *reinterpret_cast<Variant *>(storage); }
};

StringName string_name_at(lua_State *L, int index);
void push_string(lua_State *L, const String &s);
void construct_string(lua_State *L, int index, void *memory);
void push_string_name(lua_State *L, const StringName &name);
bool to_native(lua_State *L, int index, ArgType type, NativeSlot &slot, GDExtensionConstTypePtr &arg_ptr);
void prepare_return(ArgType type, NativeSlot &slot);
void push_native(lua_State *L, ArgType type, NativeSlot &slot);
void push_variant_userdata(lua_State *L, const Variant &value);
Variant::Type type_of(const Variant &value);
bool is_indexed_type(Variant::Type type);

} // namespace luau
