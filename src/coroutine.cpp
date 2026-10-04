// Coroutines and `await` (docs/adr/0035). Calls from Godot into scripts run
// on threads from a pool instead of lua_pcall on the main thread, so any
// method can suspend. A thread leaves the pool only while it is suspended.
// `await(signal)` connects a one-shot Callable that resumes the thread with
// the signal's arguments.
//
// Every pooled thread is in one of three places, so none can be lost:
// - idle (in idle_threads, at most MAX_IDLE_THREADS);
// - running (on the C stack of a call from Godot);
// - suspended in await, owned by exactly one live resumer Callable.
// A suspended thread goes back to the pool when its resumer runs, when the
// resumer is freed without running, or when the object that owns it is freed.
// A thread that suspends any other way (coroutine.yield) is an error and goes
// back at once.
#include "internal.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <lualib.h>

#include <vector>

using namespace godot;

namespace luau {

Coroutine *idle_threads = nullptr;
int idle_count = 0;

namespace {

std::vector<Coroutine *> all_threads;  // idle, running or suspended
HashMap<uint64_t, int> waiting_by_owner;  // suspended coroutines per object
int live_resumers = 0;
lua_State *main_thread = nullptr;
lua_State *running = nullptr;  // the thread now running, for lua_resume's `from`

Coroutine *pooled(lua_State *thread) {
	return static_cast<Coroutine *>(lua_getthreaddata(thread));
}

void count_waiting(uint64_t owner, int delta) {
	if (owner == 0) {
		return;
	}
	int &count = waiting_by_owner[owner];
	count += delta;
	if (count <= 0) {
		waiting_by_owner.erase(owner);
	}
}

// A suspended or failed pooled thread back to the pool
void reset_and_release(Coroutine *co) {
	if (co->awaiting) {
		co->awaiting = false;
		count_waiting(co->owner, -1);
	}
	if (lua_status(co->thread) != LUA_OK) {
		lua_resetthread(co->thread);
	}
	release_thread(co);
}

// Resumes `thread` (fresh with a function and arguments, or suspended) with
// `nargs` values on its stack. Errors are reported. A pooled thread that
// failed, or suspended other than in await, is reset and released.
int resume_thread(lua_State *thread, int nargs) {
	lua_State *outer = running;
	running = thread;
	int status = lua_resume(thread, outer ? outer : main_thread, nargs);
	running = outer;
	Coroutine *co = pooled(thread);
	if (status == LUA_OK) {
		return status;
	}
	if (status == LUA_YIELD) {
		if (co == nullptr || co->awaiting) {
			return status;
		}
		UtilityFunctions::push_error("coroutine.yield can't suspend code called from Godot; use await(signal)");
		status = LUA_ERRRUN;
	} else {
		UtilityFunctions::push_error(String::utf8(lua_tostring(thread, -1)));
	}
	if (co != nullptr) {
		reset_and_release(co);
	} else {
		lua_resetthread(thread);
	}
	return status;
}

} // namespace

Coroutine *new_thread() {
	lua_State *L = state();
	main_thread = L;
	Coroutine *co = new Coroutine();
	co->thread = lua_newthread(L);
	co->ref = lua_ref(L, -1);
	lua_pop(L, 1);
	co->serial = 0;
	co->index = all_threads.size();
	lua_setthreaddata(co->thread, co);
	all_threads.push_back(co);
	return co;
}

void drop_thread(Coroutine *co) {
	Coroutine *last = all_threads.back();
	all_threads[co->index] = last;
	last->index = co->index;
	all_threads.pop_back();
	lua_setthreaddata(co->thread, nullptr);
	lua_unref(state(), co->ref);  // the thread is collected
	delete co;
}

int run_thread(Coroutine *co, int nargs) {
	return resume_thread(co->thread, nargs);
}

void cancel_coroutines_of(uint64_t owner) {
	if (!waiting_by_owner.has(owner)) {
		return;  // the common case: nothing suspended
	}
	std::vector<Coroutine *> cancel;
	for (Coroutine *co : all_threads) {
		if (co->owner == owner && co->awaiting) {
			cancel.push_back(co);
		}
	}
	for (Coroutine *co : cancel) {
		reset_and_release(co);  // its resumer finds the serial changed
	}
	waiting_by_owner.erase(owner);
}

void clear_threads() {
	// The threads themselves die with the state
	for (Coroutine *co : all_threads) {
		delete co;
	}
	all_threads.clear();
	waiting_by_owner.clear();
	idle_threads = nullptr;
	idle_count = 0;
	running = nullptr;
	main_thread = nullptr;
}

namespace {

const char AWAIT_TOKEN = 0;

// The one-shot Callable `await` connects: resumes the suspended thread
struct Resumer {
	lua_State *thread;
	int ref;  // the thread object (pooled threads may be dropped meanwhile)
	uint32_t serial;  // of the pooled thread when it awaited
	uint64_t generation;
	bool done = false;
};

bool alive(const Resumer *r) {
	return state() != nullptr && state_generation() == r->generation;
}

// The pooled thread this resumer may still resume, or null if it was
// cancelled or reused since. User coroutines (not pooled) have no Coroutine.
Coroutine *target(const Resumer *r) {
	Coroutine *co = pooled(r->thread);
	if (co == nullptr || co->serial != r->serial || !co->awaiting || lua_status(r->thread) != LUA_YIELD) {
		return nullptr;
	}
	return co;
}

void resume(void *userdata, const GDExtensionConstVariantPtr *args, GDExtensionInt argc, GDExtensionVariantPtr, GDExtensionCallError *r_error) {
	Resumer *r = static_cast<Resumer *>(userdata);
	r_error->error = GDEXTENSION_CALL_OK;
	if (r->done || !alive(r)) {
		return;
	}
	r->done = true;
	Coroutine *co = pooled(r->thread);
	if (co != nullptr) {
		if (target(r) == nullptr) {
			return;  // cancelled when its object was freed
		}
		co->awaiting = false;
		count_waiting(co->owner, -1);
		if (co->owner != 0 && ObjectDB::get_instance(co->owner) == nullptr) {
			reset_and_release(co);  // freed without the instance callback
			return;
		}
	} else if (lua_status(r->thread) != LUA_YIELD) {
		return;
	}
	for (GDExtensionInt i = 0; i < argc; i++) {
		push_variant(r->thread, *static_cast<const Variant *>(args[i]));
	}
	if (resume_thread(r->thread, (int)argc) == LUA_OK && co != nullptr) {
		release_thread(co);  // results go nowhere: the caller has moved on
	}
}

void free_resumer(void *userdata) {
	Resumer *r = static_cast<Resumer *>(userdata);
	live_resumers--;
	if (alive(r)) {
		// Never resumed (the emitter was freed or the signal disconnected):
		// the coroutine is abandoned
		if (!r->done) {
			if (Coroutine *co = target(r)) {
				reset_and_release(co);
			}
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

Callable make_resumer(lua_State *L, uint32_t serial) {
	Resumer *r = new Resumer();
	live_resumers++;
	r->thread = L;
	lua_pushthread(L);
	r->ref = lua_ref(L, -1);
	lua_pop(L, 1);
	r->serial = serial;
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
	Coroutine *co = pooled(L);
	Error error = (Error)signal.connect(make_resumer(L, co ? co->serial : 0), Object::CONNECT_ONE_SHOT);
	if (error != OK) {
		luaL_error(L, "await: can't connect to the signal (error %d)", (int)error);
	}
	if (co != nullptr) {
		co->awaiting = true;
		count_waiting(co->owner, 1);
	}
	return lua_yield(L, 0);
}

// spawn(f, ...): runs f on its own thread until it finishes or awaits, then
// returns. Lua calls share their caller's coroutine, so this is how to start
// something that waits without suspending the caller (a GDScript call to a
// coroutine function does this implicitly).
int lua_spawn(lua_State *L) {
	luaL_checktype(L, 1, LUA_TFUNCTION);
	Coroutine *current = pooled(L);
	int n = lua_gettop(L);
	Coroutine *co = acquire_thread(current != nullptr ? current->owner : 0);
	lua_xmove(L, co->thread, n);
	if (run_thread(co, n - 1) == LUA_OK) {
		release_thread(co);
	}
	return 0;
}

// __luau_thread_stats() -> { threads, idle, waiting, resumers, kb }: for the
// leak checks in demo/checks.gd. Runs a full collection first, so kb is the
// memory still reachable.
int lua_thread_stats(lua_State *L) {
	lua_gc(L, LUA_GCCOLLECT, 0);
	int waiting = 0;
	for (const KeyValue<uint64_t, int> &E : waiting_by_owner) {
		waiting += E.value;
	}
	lua_createtable(L, 0, 5);
	lua_pushinteger(L, lua_gc(L, LUA_GCCOUNT, 0));
	lua_setfield(L, -2, "kb");
	lua_pushinteger(L, (int)all_threads.size());
	lua_setfield(L, -2, "threads");
	lua_pushinteger(L, idle_count);
	lua_setfield(L, -2, "idle");
	lua_pushinteger(L, waiting);
	lua_setfield(L, -2, "waiting");
	lua_pushinteger(L, live_resumers);
	lua_setfield(L, -2, "resumers");
	return 1;
}

} // namespace

void open_coroutines(lua_State *L) {
	lua_pushcfunction(L, lua_await, "await");
	lua_setglobal(L, "await");
	lua_pushcfunction(L, lua_spawn, "spawn");
	lua_setglobal(L, "spawn");
	lua_pushcfunction(L, lua_thread_stats, "__luau_thread_stats");
	lua_setglobal(L, "__luau_thread_stats");
}

} // namespace luau
