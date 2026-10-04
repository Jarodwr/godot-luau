# 0035. await: calls from Godot run on pooled threads

- **Status:** Accepted
- **Date:** 2026-10-04

## Context

Game code waits: for a timer, an animation, a signal from another node.
GDScript has `await`. In Luau only a coroutine can suspend. A method that
Godot calls with `lua_pcall` on the main thread can't yield at all.

GLS makes every call yieldable by creating a new thread (`lua_newthread`) per
call. That costs an allocation and garbage collection on every crossing, and
is a large part of its ~440 ns bare call.

## Decision

- **Pooled threads.** Every call from Godot into Luau runs on a thread taken
  from a pool and started with `lua_resume`:
  - script methods (`call_func`) and `_notification`;
  - Lua functions called as Callables (signal handlers, deferred calls).

  When the call finishes, the thread goes back to the pool. Threads are made
  only when the pool is empty, so in steady state a call allocates nothing.
  Getters, setters and `_get`/`_set` keep `lua_pcall`: Godot needs their
  result immediately.
- **`await(signal)`** suspends the current coroutine:
  - It connects a one-shot custom Callable to the signal, which resumes the
    thread with the signal's arguments. Those become `await`'s return values,
    several values for several arguments.
  - `await(object)` with a `completed` signal (a GDScript function state)
    waits for it and returns the GDScript function's result.
  - Any other value is returned at once, as GDScript does.
  - Outside a coroutine (a getter, or the main thread), `await` raises an
    error that says why.
- **Lua's semantics, not GDScript's.** A Lua call to a function that awaits
  runs on the caller's coroutine, so the caller suspends too, up to the call
  that came from Godot. GDScript instead returns to the caller at once.
  `spawn(f, ...)` runs `f` on its own thread until it finishes or awaits, for
  the GDScript behaviour.
- **Lifetimes:**
  - A coroutine records the object whose method started it. If that object is
    freed before the signal arrives, the coroutine is dropped instead of
    resumed (as in GDScript).
  - If the signal's emitter is freed first, the Callable is freed and the
    coroutine is dropped.
  - Dropped threads are reset (`lua_resetthread`) and returned to the pool.
  - When the Luau state closes, pending Callables do nothing.
- **No thread is lost.** Every pooled thread is idle, running a call, or
  suspended in `await` and owned by exactly one resumer Callable. A suspended
  thread goes back to the pool when its resumer runs, when the resumer is
  freed without running (emitter freed, signal disconnected), or when the
  object that owns it is freed.
  - **`coroutine.yield`** in code called from Godot would suspend a thread
    nothing can resume. It is reported as an error and the thread goes back
    at once.
  - **Freeing an object** drops its suspended coroutines immediately, so a
    method waiting on a long-lived signal doesn't keep the object's `self`
    table alive. Each thread carries a serial number, changed whenever it is
    reused, so a resumer that fires later does nothing.
  - **At most 32 idle threads** are kept. After a burst of simultaneous
    awaits, the rest are released for the garbage collector.
  - Luau's collector shrinks the stacks of idle threads and clears their
    unused slots every cycle, so a pooled thread neither stays large after
    deep recursion nor holds stale values.
- `lua_resume` is passed the thread currently running as `from`, so Luau's
  C-stack depth limit still applies across nested calls.

## Consequences

- **Any method can wait:**
  `await(self:get_tree():create_timer(1).timeout)`; signal handlers too, which
  are closures connected with `signal:connect(function() … end)`.
- **Calls from Godot cost ~3 ns more** than `lua_pcall` on the main thread
  did.
  - **Luau's share:** `lua_resume` alone costs 1.5–2 ns more than
    `lua_pcall` (Luau alone, a no-op function), and nothing more when nested
    inside a running coroutine.
  - **The rest is ours, trimmed by profiling:**
    - the idle pool is an intrusive free list, inlined into the callers (a
      `std::vector` behind out-of-line calls cost as much as the resume);
    - the resume and its success check are inline, and only yields and
      errors call out of line;
    - the result is read without asking for the stack size.
  - **How it was found:** a full-suite A/B against the build before `await`
    found +5–7 ns, and building each later commit showed all of it arrived
    with `await` itself.
  - Interleaved A/B after those changes, 3 rounds of 5 repeats, ns per op:

  | Case | Before `await` | Now |
  |---|---:|---:|
  | `call_noop` | 36.6 | 39.6 |
  | `call_add2` | 57.0 | 59.7 |
  | `echo_int` | 51.4 | 54.6 |
  | `echo_float` | 51.3 | 55.0 |
  | `call_typed` | 64.1 | 67.8 |
  | `ret_vector2` | 54.6 | 57.8 |
  | `echo_string` | 117.7 | 118.5 |
  | `callable_from_script` | 51.4 | 46.6 |
  | `notification_into_script` | 42.8 | 38.6 |

  `callable_from_script` and `notification_into_script` got faster: Lua
  Callables write their result in place, and `_notification` no longer
  reorders its arguments for `lua_pcall`. Lua-to-Lua calls, engine calls
  from Lua and the VM cases are unchanged (full-suite A/B).
- **Awaiting is cheaper than GDScript's:** `api_await_signal` (start a
  coroutine, await a signal, emit it) costs 236 ns against GDScript's 597.
- **The caller gets `nil`.** Godot gets `nil` back from a method that
  suspends. Unlike a GDScript coroutine, it can't be awaited from GDScript
  (later: a Signal it can await, [0038](0038-awaiting-luau-from-gdscript.md)).
- **Fennel** calls them the same way: `(await self.go)`, `(spawn f)`.
- **Leak checks** in `demo/checks.gd` use `__luau_thread_stats()` (thread
  counts, pending resumers, Lua memory after a full collection):
  - two rounds of 10,000 await cycles reuse the same 3 threads, with Lua
    memory flat between rounds;
  - 1,000 simultaneous awaits fall back to 32 threads once resumed;
  - `coroutine.yield` misuse and freeing a waiting object strand nothing.

  Godot reports no leaked objects at exit (`--verbose`).
- **`await` and `spawn` are new globals.** Scripts can still shadow them with
  locals.
