# Typed methods and properties

**Area:** Script features

## What

Argument, return and property types reported to Godot (method and property lists, editor hints, GDScript static checks), as GLS derives them from Luau type annotations.

## Approach

Metadata only, built once at load from declarations. At most a cheap per-argument conversion hint (int vs float) in `call_func`; no runtime lookups.

## Done when

The benchmark cases below run.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.

## Benchmark cases

- `call_typed`

GDScript and godot-luau-script numbers for these are in
[`docs/comparisons/godot-luau-script.md`](../docs/comparisons/godot-luau-script.md#feature-cases).
