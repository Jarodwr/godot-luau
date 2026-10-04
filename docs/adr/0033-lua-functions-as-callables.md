# 0033. Lua functions are Godot Callables

- **Status:** Accepted
- **Date:** 2026-10-04

## Context

Signals, `Array.sort_custom`, tweens and deferred calls all take a
`Callable`. A Lua function passed to Godot used to arrive as `null`, so a
script could only connect named methods (`Callable(self, "on_hit")`). GLS has
the same limit. GDScript passes lambdas and bound methods freely.

Two more requirements:
- **Disconnecting.** `signal.disconnect(f)` and `is_connected(f)` must find
  the connection made with the same function, so converting the same function
  twice must give equal Callables.
- **Shutdown.** A Callable can outlive the Luau state, e.g. one stored in a
  resource.

## Decision

We will turn Lua functions into custom Callables (`callable_custom_create2`):
- **What a Callable holds:**
  - a registry reference to the function;
  - the function's address, as its identity for `==`, hashing and ordering;
  - the generation of the Luau state it was made in.
- **Calling it** pushes the arguments as Lua values and converts the result.
  Errors are reported with `push_error`, as for script methods.
- **Coming back.** When one of our Callables comes back into Lua it becomes
  the original function again, so `f(5)` works on a value that went through
  Godot.
- **After the state closes,** calls report an invalid instance, `is_valid` is
  false, and freeing doesn't touch Lua.
- **Godot's own Callables** in Lua are userdata with `__call`, so GDScript
  lambdas and method Callables are called like functions.

## Consequences

- **`signal:connect(function(x) … end)` works,** and so do disconnecting by the
  same function and closures that capture locals.
- **Calls are cheap:**
  - Godot calling a Lua function (`api_callable_call`): 6.3 ns against
    GDScript's 46;
  - `callable_from_script`: 49 against 46.
- **No object is attached** to the Callable. Godot can't disconnect it
  automatically when the script's node is freed, as it does for method
  Callables. Connections to other objects' signals made with closures last
  until disconnected or until that object is freed. `Callable(self, "name")`
  remains available when automatic disconnection matters.
- **The function stays alive** while Godot holds the Callable: the registry
  reference keeps it from being collected.
