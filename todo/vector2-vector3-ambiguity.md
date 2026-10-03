# Vector2 vs Vector3 in untyped contexts

**Area:** API surface

## What

Vector2 and Vector3 share Luau's `vector`. Where Godot accepts any type (Variant arguments, Array elements, metadata), a vector with z = 0 becomes Vector2.

## Approach

Options: a 4-wide Luau vector (`LUAU_VECTOR_FOUR_WIDE`) with the extra lane as a type tag; or Vector3 as userdata. Measure the cost of each.

## Done when

A Vector3 with z = 0 stored in an Array comes back as a Vector3.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
