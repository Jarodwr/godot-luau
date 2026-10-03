# Builtin type constructors

**Area:** API surface

## What

`Color`, `Rect2`, `Rect2i`, `Transform2D`, `Transform3D`, `Basis`, `Quaternion`, `Plane`, `AABB`, `Projection`, `Array`, `Dictionary`, `NodePath`, `StringName`, `Callable`, `Signal`, `Vector2i`/`Vector3i`/`Vector4(i)`, packed arrays.

## Approach

Globals backed by the interface's builtin constructors (`variant_get_ptr_constructor`), producing Variant userdata (or native vectors for Vector2/3). Operators already go through `variant_evaluate` for Variant userdata.

## Done when

Benchmark cases `api_color_math`, `api_transform_xform` and `api_array_build` run.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
