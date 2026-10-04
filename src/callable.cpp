// Lua functions as Godot Callables (docs/adr/0033). A custom callable holds a
// registry reference to the function; when one comes back into Lua it is the
// function again. Calls and frees after the Luau state closed do nothing.
#include "internal.h"

#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace luau {

namespace {

const char TOKEN = 0;  // identifies our custom callables

struct LuaCallable {
	int ref;
	const void *function;  // identity: the same function is the same Callable
	uint64_t generation;   // the Luau state it belongs to
};

bool alive(const LuaCallable *c) {
	return state() != nullptr && state_generation() == c->generation;
}

void call(void *userdata, const GDExtensionConstVariantPtr *args, GDExtensionInt argc, GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
	LuaCallable *c = static_cast<LuaCallable *>(userdata);
	if (!alive(c)) {
		r_error->error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return;
	}
	// On a pooled thread, so the function can await (docs/adr/0035)
	Coroutine *co = acquire_thread(0);
	lua_State *L = co->thread;
	lua_getref(L, c->ref);
	for (GDExtensionInt i = 0; i < argc; i++) {
		push_variant(L, *static_cast<const Variant *>(args[i]));
	}
	r_error->error = GDEXTENSION_CALL_OK;
	int status = run_thread(co, (int)argc);
	if (status == LUA_YIELD) {
		*static_cast<Variant *>(r_return) = completion_signal(co);  // awaitable
		return;
	}
	if (status != LUA_OK) {
		return;
	}
	if (lua_gettop(L) > 0) {
		Variant *ret = static_cast<Variant *>(r_return);
		if (ret->get_type() == Variant::NIL) {
			to_variant_into_nil(L, 1, ret);  // built in place
		} else {
			*ret = to_variant(L, 1);
		}
	}
	release_thread(co);
}

GDExtensionBool is_valid(void *userdata) {
	return alive(static_cast<LuaCallable *>(userdata));
}

void free_callable(void *userdata) {
	LuaCallable *c = static_cast<LuaCallable *>(userdata);
	if (alive(c)) {
		lua_unref(state(), c->ref);
	}
	delete c;
}

uint32_t hash(void *userdata) {
	uintptr_t p = (uintptr_t) static_cast<LuaCallable *>(userdata)->function;
	return (uint32_t)(p ^ (p >> 32));
}

GDExtensionBool equal(void *a, void *b) {
	return static_cast<LuaCallable *>(a)->function == static_cast<LuaCallable *>(b)->function;
}

GDExtensionBool less_than(void *a, void *b) {
	return static_cast<LuaCallable *>(a)->function < static_cast<LuaCallable *>(b)->function;
}

void to_string(void *, GDExtensionBool *r_is_valid, GDExtensionStringPtr r_out) {
	*static_cast<String *>(r_out) = "<Luau function>";
	*r_is_valid = true;
}

GDExtensionInt argument_count(void *userdata, GDExtensionBool *r_is_valid) {
	LuaCallable *c = static_cast<LuaCallable *>(userdata);
	*r_is_valid = false;
	if (!alive(c)) {
		return 0;
	}
	lua_State *L = state();
	lua_getref(L, c->ref);
	lua_Debug ar;
	GDExtensionInt count = 0;
	if (lua_getinfo(L, -1, "a", &ar)) {
		count = ar.nparams;
		*r_is_valid = !ar.isvararg;
	}
	lua_pop(L, 1);
	return count;
}

} // namespace

Variant lua_function_to_callable(lua_State *L, int index) {
	LuaCallable *c = new LuaCallable();
	lua_pushvalue(L, index);
	c->ref = lua_ref(L, -1);
	lua_pop(L, 1);
	c->function = lua_topointer(L, index);
	c->generation = state_generation();

	GDExtensionCallableCustomInfo2 info = {};
	info.callable_userdata = c;
	info.token = (void *)&TOKEN;
	info.call_func = call;
	info.is_valid_func = is_valid;
	info.free_func = free_callable;
	info.hash_func = hash;
	info.equal_func = equal;
	info.less_than_func = less_than;
	info.to_string_func = to_string;
	info.get_argument_count_func = argument_count;
	Callable callable;
	callable.~Callable();  // constructed by the engine below
	gdextension_interface::callable_custom_create2(callable._native_ptr(), &info);
	return Variant(callable);
}

bool push_lua_function_of(lua_State *L, const Variant &value) {
	Callable callable = value;
	LuaCallable *c = static_cast<LuaCallable *>(gdextension_interface::callable_custom_get_userdata(callable._native_ptr(), (void *)&TOKEN));
	if (c == nullptr || !alive(c)) {
		return false;
	}
	lua_getref(L, c->ref);
	return true;
}

} // namespace luau
