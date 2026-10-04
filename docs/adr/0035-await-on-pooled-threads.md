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
- **Calls from Godot cost ~2–4 ns more.** `lua_resume` on a pooled thread
  costs a little more than `lua_pcall` on the main thread. In isolation (Luau
  alone, a no-op function) the difference is 1.5–2 ns, and none when nested
  inside a running coroutine. The pool itself is an intrusive free list,
  inlined into the callers: profiling showed a `std::vector` behind calls into
  another file costing as much as the resume. Interleaved A/B against the
  build before `await`, 5 rounds of 5 repeats, ns per op:

  | Case | Before | After |
  |---|---:|---:|
  | `call_noop` | 37.0 | 39.5 |
  | `call_args6` | 110.5 | 111.6 |
  | `echo_string` | 117.6 | 121.1 |
  | `api_signal_emit` | 89.2 | 91.7 |
  | `callable_from_script` | 50.7 | 47.6 |
  | `notification_into_script` | 42.9 | 39.3 |

  The last two got faster: Lua Callables now write their result in place,
  and `_notification` no longer reorders its arguments for `lua_pcall`.
  Lua-to-Lua calls and engine calls from Lua are unaffected.
- **Awaiting is cheaper than GDScript's:** `api_await_signal` (start a
  coroutine, await a signal, emit it) costs 236 ns against GDScript's 597.
- **The caller gets `nil`.** Godot gets `nil` back from a method that
  suspends. Unlike a GDScript coroutine, it can't be awaited from GDScript
  (`todo/await-from-gdscript.md`).
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
