# Awaiting Luau methods from GDScript

**Area:** Script features

## What

A Luau method that suspends in `await` returns `nil` to Godot. GDScript code
calling it can't `await` it, as it can a GDScript coroutine (which returns a
`GDScriptFunctionState` with a `completed` signal).

## Approach

When a call from Godot suspends, return an object with a `completed` signal,
emitted with the method's result when the coroutine finishes. Only on suspend:
calls that finish keep returning their result directly.

## Done when

`var x = await luau_node.load_level()` in GDScript waits for the Luau method
and receives its return value.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
