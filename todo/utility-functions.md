# Global utility functions

**Area:** API surface

## What

`lerp`, `clamp`, `randf`, `randi_range`, `deg_to_rad`, `print_rich`, `push_warning`, `is_instance_valid`, `str`, … (Godot's `@GlobalScope` functions).

## Approach

Globals backed by `variant_get_ptr_utility_function`, with hashes from `extension_api.json` (generated as data, like method binds).

## Done when

Benchmark case `api_utility_fn` runs.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.

## Benchmark cases

- `api_utility_mix`

GDScript and godot-luau-script numbers for these are in
[`docs/comparisons/godot-luau-script.md`](../docs/comparisons/godot-luau-script.md#feature-cases).
