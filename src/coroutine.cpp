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

namespace {

// Thread data of pooled threads (user coroutines have none)
struct ThreadInfo {
	int ref;         // keeps the thread alive while the state lives
	uint64_t owner;  // ObjectID of the object whose method runs on it, or 0
};

std::vector<lua_State *> idle;
lua_State *running = nullptr;  // the thread now running, for lua_resume's `from`

void reset_and_release(lua_State *thread) {
	lua_resetthread(thread);
	if (lua_getthreaddata(thread) != nullptr) {
		idle.push_back(thread);
	}
}

} // namespace

lua_State *acquire_thread() {
	if (!idle.empty()) {
		lua_State *thread = idle.back();
		idle.pop_back();
		return thread;
	}
	lua_State *L = state();
	lua_State *thread = lua_newthread(L);
	ThreadInfo *info = new ThreadInfo{ lua_ref(L, -1), 0 };
	lua_pop(L, 1);
	lua_setthreaddata(thread, info);
	return thread;
}

void release_thread(lua_State *thread) {
	lua_settop(thread, 0);
	if (lua_getthreaddata(thread) != nullptr) {
		idle.push_back(thread);
	}
}

int run_thread(lua_State *thread, int nargs, uint64_t owner) {
	ThreadInfo *info = static_cast<ThreadInfo *>(lua_getthreaddata(thread));
	if (info != nullptr && lua_status(thread) == LUA_OK) {
		info->owner = owner;  // a fresh call
	}
	lua_State *from = running ? running : state();
	running = thread;
	int status = lua_resume(thread, from, nargs);
	running = from == state() ? nullptr : from;
	if (status == LUA_OK || status == LUA_YIELD) {
		return status;
	}
	UtilityFunctions::push_error(String::utf8(lua_tostring(thread, -1)));
	reset_and_release(thread);
	return status;
}

void clear_threads() {
	// The threads themselves die with the state
	for (lua_State *thread : idle) {
		delete static_cast<ThreadInfo *>(lua_getthreaddata(thread));
	}
	idle.clear();
	running = nullptr;
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
	ThreadInfo *info = static_cast<ThreadInfo *>(lua_getthreaddata(thread));
	return info != nullptr && info->owner != 0 && ObjectDB::get_instance(info->owner) == nullptr;
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
	if (run_thread(r->thread, (int)argc, 0) == LUA_OK) {
		release_thread(r->thread);  // results go nowhere: the caller has moved on
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
	ThreadInfo *info = static_cast<ThreadInfo *>(lua_getthreaddata(L));
	uint64_t owner = info != nullptr ? info->owner : 0;
	int n = lua_gettop(L);
	lua_State *thread = acquire_thread();
	lua_xmove(L, thread, n);
	if (run_thread(thread, n - 1, owner) == LUA_OK) {
		release_thread(thread);
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
