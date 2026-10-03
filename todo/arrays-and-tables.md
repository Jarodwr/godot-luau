# Array / Dictionary iteration and Lua table conversion

**Area:** API surface

## What

`for _, v in arr do` over Godot Arrays and Dictionaries, `#arr`, and converting Lua tables to Array/Dictionary where Godot expects one (and back, on request).

## Approach

`__iter` on Array/Dictionary userdata. Table → Array when the target type is Array (native arguments) or for sequences in Variant context; a documented rule for dictionaries.

## Done when

Benchmark cases `api_array_iterate`, `api_dict_rw`, `api_array_table_in` and `ret_array` run.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.

## Benchmark cases

- `api_dict_iterate`

GDScript and godot-luau-script numbers for these are in
[`docs/comparisons/godot-luau-script.md`](../docs/comparisons/godot-luau-script.md#feature-cases).
