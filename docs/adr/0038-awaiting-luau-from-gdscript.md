# 0038. A suspended call returns a completion Signal, so GDScript can await it

- **Status:** Accepted
- **Date:** 2026-10-05

## Context

Since [0035](0035-await-on-pooled-threads.md), a Luau method called from
Godot can suspend in `await`; its caller got `nil`. GDScript code can't wait
for such a method, as it can for a GDScript coroutine:
`var level = await luau_node.load_level()`.

GDScript's `await` only waits on two things (`OPCODE_AWAIT` in
`gdscript_vm.cpp`): a `Signal`, or its own `GDScriptFunctionState`, which an
extension can't create. Any other value is returned at once.

## Decision

When a call from Godot suspends (script methods, and Lua functions called as
Callables), it returns `Signal(helper, "completed")`:
- **The helper** is a plain `Object` with a user signal
  `completed(result)`. It's created on the first suspension only; calls that
  finish return their value as before.
- **When the coroutine finishes** after its last resume, `completed` is
  emitted with the call's first result, and the helper is freed. The thread
  goes back to the pool first, because the awaiting GDScript code runs during
  the emission and may call back into Luau.
- **If the coroutine fails or is dropped,** the helper is freed without
  emitting. Dropped means its object was freed, or the signal it awaited went
  away. Whoever awaits the helper never resumes, as in GDScript when the
  awaited function's instance is freed.
- Luau's own `await` already accepts a `Signal`, so Lua code can wait on a
  call that went through Godot and suspended.

## Consequences

- **GDScript can wait on Luau methods:**
  `var doubled = await luau_node.wait_then_return(21)` waits until the
  method finishes and gets its result. Awaiting a method that doesn't suspend
  returns its value at once.
- **Callers that don't await** get a `Signal` instead of `nil`, as GDScript
  callers of a GDScript coroutine get a function state.
- **No cost for calls that don't suspend.** Suspending costs one `Object`
  allocation per call, freed when it finishes.
- **The result isn't coerced** to the method's declared return type
  ([0032](0032-script-declarations.md)), unlike a direct return.
- **Tests:** `demo/checks.gd` covers awaiting a result, a method that doesn't
  suspend, and freeing the helper after emitting, after a failure and when
  the owner is freed. Godot reports no leaked objects at exit.
