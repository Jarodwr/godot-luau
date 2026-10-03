# Vector2 vs Vector3 in untyped contexts

**Area:** API surface

## What

Vector2 and Vector3 share Luau's `vector`. Where Godot accepts any type (Variant arguments, Array elements, metadata), a vector with z = 0 becomes Vector2.

## Current behaviour (ADR 0025)

Untyped calls and operators pass a vector with z = 0 as Vector2 and retry as
Vector3 if that fails; `cross` is always Vector3's. That works but costs a
second attempt (`api_transform3d_xform`), and values stored where Godot
accepts anything (arrays, metadata, untyped properties) still become Vector2.

## Approach

Options: a 4-wide Luau vector (`LUAU_VECTOR_FOUR_WIDE`) with the extra lane as a type tag; or Vector3 as userdata. Measure the cost of each.

## Done when

A Vector3 with z = 0 stored in an Array comes back as a Vector3.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
