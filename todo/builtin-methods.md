# Methods on builtin values

**Area:** API surface

## What

`v:length()`, `v:normalized()` on vectors; Godot `String` methods on Lua strings (`s:to_upper()`, `s:split(",")`); methods on Color, Array, Dictionary…

## Approach

A metatable for Luau's vector type whose `__index` maps names to Luau's builtin vector functions where they exist (fast) and to the engine's Vector2/Vector3 methods otherwise. Lua strings: fall back to Godot String methods in the string metatable for names Lua's string library doesn't have. Variant userdata already forward to `Variant::callp`; could use cached builtin method pointers (`variant_get_ptr_builtin_method`).

## Done when

Benchmark cases `api_vector2_method` and `api_string_method` run.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
