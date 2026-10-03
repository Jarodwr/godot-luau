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

// Whether a Variant (given its bytes) needs its destructor
bool needs_destroy(const void *variant_bytes);

// One argument or result in its native ptrcall layout
struct NativeSlot {
	alignas(16) unsigned char bytes[sizeof(Variant)];
	ArgType constructed = T_VOID;  // which object to destroy, if any

	~NativeSlot() {
		switch (constructed) {
			case T_STRING: reinterpret_cast<String *>(bytes)->~String(); break;
			case T_STRING_NAME: reinterpret_cast<StringName *>(bytes)->~StringName(); break;
			case T_VARIANT:
				if (needs_destroy(bytes)) {
					reinterpret_cast<Variant *>(bytes)->~Variant();
				}
				break;
			case T_NODE_PATH: reinterpret_cast<NodePath *>(bytes)->~NodePath(); break;
			default: break;
		}
	}
};

constexpr int MAX_VARIANT_ARGS = 16;

// `vectors_as_3`: write vectors as Vector3 even when z = 0 (see
// call_with_vector_retry)
bool write_plain_variant(lua_State *L, int index, void *memory, bool vectors_as_3 = false);
// The Variant inside a Godot value's userdata (Variant or object box), to be
// used in place while it stays on the stack; null for other Lua values
const Variant *borrow_variant(lua_State *L, int index);

// Luau values as call arguments in raw storage: only the ones passed are
// constructed, plain values are written as bytes (docs/adr/0015)
struct VariantArgs {
	alignas(Variant) unsigned char storage[MAX_VARIANT_ARGS][sizeof(Variant)];
	const Variant *argv[MAX_VARIANT_ARGS];
	int count = 0;
	uint32_t borrowed = 0;  // bit i: argv[i] points into a userdata, not storage
	bool vectors_as_3 = false;

	VariantArgs() = default;
	VariantArgs(const VariantArgs &) = delete;
	~VariantArgs() {
		for (int i = 0; i < count; i++) {
			if (!(borrowed & (1u << i)) && needs_destroy(storage[i])) {
				reinterpret_cast<Variant *>(storage[i])->~Variant();
			}
		}
	}

	// Appends the Luau value at `index`. Godot values held in userdata are
	// used in place, not copied.
	void add(lua_State *L, int index) {
		if (const Variant *held = borrow_variant(L, index)) {
			argv[count] = held;
			borrowed |= 1u << count;
			count++;
			return;
		}
		void *slot = storage[count];
		if (!write_plain_variant(L, index, slot, vectors_as_3)) {
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
		reset();
		constructed = true;
		return storage;
	}
	// Destroys the value, if any (before reusing the storage)
	void reset() {
		if (constructed && needs_destroy(storage)) {
			get().~Variant();
		}
		constructed = false;
	}
	Variant &get() { return *reinterpret_cast<Variant *>(storage); }
};

// Pushes a plain Variant (nil, bool, number, vector, String) from its bytes;
// false for other types
bool push_plain_variant(lua_State *L, const Variant &value);

// Pushes a call result, moving it into a new userdata (no copy) when it
// becomes one; the result is left empty
void push_result(lua_State *L, VariantResult &result);

// Whether any of the Lua values [first, first + count) is a vector with z = 0
bool has_flat_vector(lua_State *L, int first, int count);

// Runs `call(args, result, error)` with the Lua values [first, first + argc)
// as arguments. Vector2 and Vector3 share Luau's vector, so a vector with
// z = 0 is passed as Vector2; if the call fails and there was one, it's
// retried with vectors as Vector3 (e.g. Basis(Vector3(0, 1, 0), 0.5)).
template <typename F>
void call_with_vector_retry(lua_State *L, int first, int argc, VariantResult &result, GDExtensionCallError &error, F &&call) {
	for (int attempt = 0; attempt < 2; attempt++) {
		{
			VariantArgs args;
			args.vectors_as_3 = attempt == 1;
			for (int i = 0; i < argc; i++) {
				args.add(L, first + i);
			}
			call(args, result, error);
		}
		if (error.error == GDEXTENSION_CALL_OK || attempt == 1 || !has_flat_vector(L, first, argc)) {
			return;
		}
	}
}

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
