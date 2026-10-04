// Coroutines and `await` (docs/adr/0035). Calls from Godot into scripts run
// on threads from a pool instead of lua_pcall on the main thread, so any
// method can suspend. A thread leaves the pool only while it is suspended.
// `await(signal)` connects a one-shot Callable that resumes the thread with
// the signal's arguments.
#include "internal.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <lualib.h>

#include <vector>


using namespace godot;

namespace luau {

Coroutine *idle_threads = nullptr;

namespace {

std::vector<Coroutine *> all_threads;  // idle or suspended, freed with the state
lua_State *main_thread = nullptr;
lua_State *running = nullptr;  // the thread now running, for lua_resume's `from`

// Resumes `thread` (fresh with a function and arguments, or suspended) with
// `nargs` values on its stack. Errors are reported and leave it reset.
int resume_thread(lua_State *thread, int nargs) {
	lua_State *outer = running;
	running = thread;
	int status = lua_resume(thread, outer ? outer : main_thread, nargs);
	running = outer;
	if (status == LUA_OK || status == LUA_YIELD) {
		return status;
	}
	UtilityFunctions::push_error(String::utf8(lua_tostring(thread, -1)));
	lua_resetthread(thread);
	return status;
}

// After a thread finished, failed or was abandoned: pooled ones go back
void reset_and_release(lua_State *thread) {
	if (lua_status(thread) != LUA_OK) {
		lua_resetthread(thread);
	}
	if (Coroutine *co = static_cast<Coroutine *>(lua_getthreaddata(thread))) {
		release_thread(co);
	}
}

} // namespace

Coroutine *new_thread() {
	lua_State *L = state();
	main_thread = L;
	Coroutine *co = new Coroutine();
	co->thread = lua_newthread(L);
	co->ref = lua_ref(L, -1);
	lua_pop(L, 1);
	lua_setthreaddata(co->thread, co);
	all_threads.push_back(co);
	return co;
}

int run_thread(Coroutine *co, int nargs) {
	int status = resume_thread(co->thread, nargs);
	if (status != LUA_OK && status != LUA_YIELD) {
		release_thread(co);  // reset by resume_thread
	}
	return status;
}

void clear_threads() {
	// The threads themselves die with the state
	for (Coroutine *co : all_threads) {
		delete co;
	}
	all_threads.clear();
	idle_threads = nullptr;
	running = nullptr;
	main_thread = nullptr;
}

namespace {

const char AWAIT_TOKEN = 0;

// The one-shot Callable `await` connects: resumes the suspended thread
struct Resumer {
	lua_State *thread;
	int ref;  // the thread object, for user coroutines without a pool ref
	uint64_t generation;
	bool resumed = false;
};

bool alive(const Resumer *r) {
	return state() != nullptr && state_generation() == r->generation;
}

bool owner_freed(lua_State *thread) {
	Coroutine *co = static_cast<Coroutine *>(lua_getthreaddata(thread));
	return co != nullptr && co->owner != 0 && ObjectDB::get_instance(co->owner) == nullptr;
}

void resume(void *userdata, const GDExtensionConstVariantPtr *args, GDExtensionInt argc, GDExtensionVariantPtr, GDExtensionCallError *r_error) {
	Resumer *r = static_cast<Resumer *>(userdata);
	r_error->error = GDEXTENSION_CALL_OK;
	if (r->resumed || !alive(r) || lua_status(r->thread) != LUA_YIELD) {
		return;
	}
	r->resumed = true;
	if (owner_freed(r->thread)) {
		// As in GDScript: a method of a freed object doesn't continue
		reset_and_release(r->thread);
		return;
	}
	for (GDExtensionInt i = 0; i < argc; i++) {
		push_variant(r->thread, *static_cast<const Variant *>(args[i]));
	}
	int status = resume_thread(r->thread, (int)argc);
	if (status != LUA_YIELD) {
		reset_and_release(r->thread);  // results go nowhere: the caller has moved on
	}
}

void free_resumer(void *userdata) {
	Resumer *r = static_cast<Resumer *>(userdata);
	if (alive(r)) {
		// Never resumed (the emitter was freed or the signal disconnected):
		// the coroutine is abandoned
		if (!r->resumed && lua_status(r->thread) == LUA_YIELD) {
			reset_and_release(r->thread);
		}
		lua_unref(state(), r->ref);
	}
	delete r;
}

uint32_t hash_resumer(void *userdata) {
	uintptr_t p = (uintptr_t)userdata;
	return (uint32_t)(p ^ (p >> 32));
}

GDExtensionBool equal_resumer(void *a, void *b) {
	return a == b;
}

void resumer_to_string(void *, GDExtensionBool *r_is_valid, GDExtensionStringPtr r_out) {
	*static_cast<String *>(r_out) = "<Luau await>";
	*r_is_valid = true;
}

Callable make_resumer(lua_State *L) {
	Resumer *r = new Resumer();
	r->thread = L;
	lua_pushthread(L);
	r->ref = lua_ref(L, -1);
	lua_pop(L, 1);
	r->generation = state_generation();

	GDExtensionCallableCustomInfo2 info = {};
	info.callable_userdata = r;
	info.token = (void *)&AWAIT_TOKEN;
	info.call_func = resume;
	info.free_func = free_resumer;
	info.hash_func = hash_resumer;
	info.equal_func = equal_resumer;
	info.to_string_func = resumer_to_string;
	Callable callable;
	callable.~Callable();  // constructed by the engine below
	gdextension_interface::callable_custom_create2(callable._native_ptr(), &info);
	return callable;
}

// await(signal) -> the signal's arguments, once it is emitted. Also accepts
// an object with a `completed` signal (a GDScript function state). Anything
// else is returned at once, as GDScript's await does.
int lua_await(lua_State *L) {
	Variant value = to_variant(L, 1);
	Signal signal;
	if (value.get_type() == Variant::SIGNAL) {
		signal = value;
	} else if (value.get_type() == Variant::OBJECT) {
		Object *object = value;
		if (object != nullptr && object->has_signal("completed")) {
			signal = Signal(object, "completed");
		}
	}
	if (signal.is_null()) {
		return lua_gettop(L);
	}
	if (L == lua_mainthread(L) || !lua_isyieldable(L)) {
		luaL_error(L, "await can only be used in code called from Godot (a method, signal handler or Callable), not in a getter, setter or _get/_set");
	}
	Error error = (Error)signal.connect(make_resumer(L), Object::CONNECT_ONE_SHOT);
	if (error != OK) {
		luaL_error(L, "await: can't connect to the signal (error %d)", (int)error);
	}
	return lua_yield(L, 0);
}

// spawn(f, ...): runs f on its own thread until it finishes or awaits, then
// returns. Lua calls share their caller's coroutine, so this is how to start
// something that waits without suspending the caller (a GDScript call to a
// coroutine function does this implicitly).
int lua_spawn(lua_State *L) {
	luaL_checktype(L, 1, LUA_TFUNCTION);
	Coroutine *current = static_cast<Coroutine *>(lua_getthreaddata(L));
	int n = lua_gettop(L);
	Coroutine *co = acquire_thread(current != nullptr ? current->owner : 0);
	lua_xmove(L, co->thread, n);
	if (run_thread(co, n - 1) == LUA_OK) {
		release_thread(co);
	}
	return 0;
}

} // namespace

void open_coroutines(lua_State *L) {
	lua_pushcfunction(L, lua_await, "await");
	lua_setglobal(L, "await");
	lua_pushcfunction(L, lua_spawn, "spawn");
	lua_setglobal(L, "spawn");
}

} // namespace luau
