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
- `lua_resume` is passed the thread currently running as `from`, so Luau's
  C-stack depth limit still applies across nested calls.

## Consequences

- **Any method can wait:**
  `await(self:get_tree():create_timer(1).timeout)`; signal handlers too, which
  are closures connected with `signal:connect(function() … end)`.
- **No measurable cost on calls:** `call_noop` 39 ns (38 before);
  `callable_from_script`, `api_signal_emit` and `process_nodes` are within
  noise. `notification_into_script` dropped from 58 to 41 ns, since the
  arguments no longer need reordering for `lua_pcall`.
- **Awaiting is cheaper than GDScript's:** `api_await_signal` (start a
  coroutine, await a signal, emit it) costs 236 ns against GDScript's 597.
- **The caller gets `nil`.** Godot gets `nil` back from a method that
  suspends. Unlike a GDScript coroutine, it can't be awaited from GDScript
  (`todo/await-from-gdscript.md`).
- **Fennel** calls them the same way: `(await self.go)`, `(spawn f)`.
- **`await` and `spawn` are new globals.** Scripts can still shadow them with
  locals.
