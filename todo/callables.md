# Callables from Lua functions

**Area:** Script features

## What

`Callable(function(x) ... end)` and passing Lua functions where Godot expects a Callable (`connect`, `call_deferred`, tweens, `Array.sort_custom`).

## Approach

A custom Callable (`callable_custom_create2`) holding a registry reference to the function, freed with the Callable. Calls convert arguments with the existing plain-value paths. Must not keep the Lua state alive (fennel-gdextension had a leak cycle here: its LuaCallable holds the state weakly).

## Done when

Benchmark cases `api_callable_call` and `callable_from_script` run. No leaked objects at exit.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
