# Default arguments for methods called from Godot

**Area:** Script features

## What

Declared default values for trailing arguments, used when Godot (or GDScript) calls a script method with fewer arguments.

## Approach

Declarations read once at load. `call_func` pushes the stored default for each missing argument, keeping one registry reference per value. No cost when all arguments are passed.

## Done when

The benchmark cases below run.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.

## Benchmark cases

- `call_default_args`

GDScript and godot-luau-script numbers for these are in
[`docs/comparisons/godot-luau-script.md`](../docs/comparisons/godot-luau-script.md#feature-cases).
