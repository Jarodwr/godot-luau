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

// ---- Packed values (docs/adr/0030)
//
// Vector2i (two int32) and RID (a uint64) fit in a tagged light userdata's
// 8 bytes: no allocation, no garbage collection, and equality is Lua's own.
// Needs 64-bit pointers; elsewhere these stay Variant userdata.
#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
constexpr bool PACKED_VALUES = true;
#else
constexpr bool PACKED_VALUES = false;
#endif
enum : int { LUTAG_VECTOR2I = 1, LUTAG_RID = 2 };

inline void push_packed_vector2i(lua_State *L, int32_t x, int32_t y) {
	uint64_t bits = (uint64_t)(uint32_t)x | ((uint64_t)(uint32_t)y << 32);
	lua_pushlightuserdatatagged(L, (void *)(uintptr_t)bits, LUTAG_VECTOR2I);
}

inline void push_packed_rid(lua_State *L, uint64_t id) {
	lua_pushlightuserdatatagged(L, (void *)(uintptr_t)id, LUTAG_RID);
}

// The packed tag of the value at `index` (0 if it isn't a packed value)
inline int packed_tag(lua_State *L, int index) {
	return PACKED_VALUES && lua_type(L, index) == LUA_TLIGHTUSERDATA ? lua_lightuserdatatag(L, index) : 0;
}

inline uint64_t packed_bits(lua_State *L, int index, int tag) {
	return (uint64_t)(uintptr_t)lua_tolightuserdatatagged(L, index, tag);
}

inline void unpack_vector2i(uint64_t bits, int32_t r[2]) {
	r[0] = (int32_t)(uint32_t)bits;
	r[1] = (int32_t)(uint32_t)(bits >> 32);
}

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

// Whether the startup check confirmed Godot's Variant layout (values may be
// read and written as bytes)
bool variant_layout_checked();

// Sets __add, __sub, __mul, __div, __eq, __lt, __le on the table at `index`
void set_operator_metamethods(lua_State *L, int index);

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

// Changes whenever the Luau state is closed: callables and other holders of
// registry references check it before touching the state
uint64_t state_generation();

// Lua functions as Godot Callables (callable.cpp, docs/adr/0033)
Variant lua_function_to_callable(lua_State *L, int index);
// If `value` is a Callable made from a Lua function, pushes that function
bool push_lua_function_of(lua_State *L, const Variant &value);

// Coroutines (coroutine.cpp, docs/adr/0035). Calls from Godot run on pooled
// threads so they can `await`.
struct Coroutine {
	lua_State *thread;
	int ref;          // keeps the thread alive while it is pooled or suspended
	uint64_t owner;   // ObjectID of the object whose method runs, or 0
	uint32_t serial;  // changes on every acquire: tells a stale resumer apart
	bool awaiting;    // suspended in await (not coroutine.yield)
	size_t index;     // in the list of all pooled threads
	Coroutine *next;  // in the idle list
};
extern Coroutine *idle_threads;
extern int idle_count;
constexpr int MAX_IDLE_THREADS = 32;  // more are dropped: bursts don't stay
Coroutine *new_thread();
void drop_thread(Coroutine *co);
// An idle pooled thread with an empty stack. `owner` (an ObjectID, or 0): the
// object whose method runs on it; its suspended coroutines are dropped when
// it is freed. Inline: it's on every call from Godot.
inline Coroutine *acquire_thread(uint64_t owner) {
	Coroutine *co = idle_threads;
	if (co != nullptr) {
		idle_threads = co->next;
		idle_count--;
	} else {
		co = new_thread();
	}
	co->owner = owner;
	co->serial++;
	co->awaiting = false;
	return co;
}
// Runs the function and its `nargs` arguments pushed on the thread.
// LUA_OK: the results are on the thread's stack; the caller reads them, then
// calls release_thread. LUA_YIELD: suspended in `await`, which owns it from
// now on. Anything else: the error was reported and the thread released.
int run_thread(Coroutine *co, int nargs);
inline void release_thread(Coroutine *co) {
	lua_settop(co->thread, 0);
	if (idle_count < MAX_IDLE_THREADS) {
		co->next = idle_threads;
		idle_threads = co;
		idle_count++;
	} else {
		drop_thread(co);
	}
}
// Drops the suspended coroutines of a freed object (its script instance's
// free callback)
void cancel_coroutines_of(uint64_t owner);
void open_coroutines(lua_State *L);

// Script errors (errors.cpp, docs/adr/0037): reported as Godot script errors
// with file, line and a backtrace of the Luau frames from `level` down
void report_error(lua_State *thread, int level, const char *message);
// A message with a "res://file:line:" prefix (syntax and compile errors)
void report_message(const String &message);
// The handler for lua_pcall on the main thread that reports errors (the
// caller doesn't report again). It stays in the main thread's first stack
// slot, installed when the state opens, so a pcall passes its index and
// pushes nothing.
constexpr int ERROR_HANDLER = 1;
void install_error_handler(lua_State *L);
// A readable reason for a failed engine call
String call_error_text(const GDExtensionCallError &error);
void clear_threads();

// `value` converted to `type` where Lua's representation lost it (whole
// floats read as ints, Vector3 with z = 0 read as Vector2); NIL: unchanged
Variant coerce_to_type(const Variant &value, Variant::Type type);

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
