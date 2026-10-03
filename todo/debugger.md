# Debugger and profiler integration

**Area:** Editor and tooling

## What

Breakpoints, stepping, stack and locals in Godot's debugger; script functions in Godot's profiler.

## Approach

The `_debug_*` language callbacks using Luau's debug API (`lua_getinfo`, `lua_breakpoint`, single-step callbacks); `_profiling_*`.

## Done when

A breakpoint in a Luau method stops the game and shows locals.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
