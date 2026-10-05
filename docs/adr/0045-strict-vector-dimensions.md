# 0045. Vector dimensions are strict at typed engine boundaries

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

After [0044](0044-vector2-and-vector3-as-luau-types.md), Vector2 and Vector3
are exact Luau types. Two leftovers from the "z = 0 means Vector2" days still
converted silently:
- **`Vector2(1, 2, 3)`:** the compiler turns `Vector2(...)` into Luau's vector
  constructor fast call (`vectorCtor`), which builds a 3D vector from three
  numbers. The script got a Vector3 named Vector2.
- **Typed engine arguments and properties** took either kind: a Vector2
  passed as a Vector3 became `(x, y, 0)`, and a Vector3 passed as a Vector2
  lost z. GDScript reports both, since Godot doesn't convert between them;
  such a value is a bug in the script.

Computed-key field access (`v["x"]`) was considered as well and left out: rare
in practice, and it would cost a fork change or slower vector method calls.

## Decision

- **The constructor fast call only takes two arguments.** The Luau fork gains
  a compile option, `vectorCtorArgs` (0 = any count, as upstream).
  godot-luau sets it to 2.
  - With it, a `Vector2(...)` call with another argument count calls the
    `Vector2` table, whose constructor takes `Vector2()` and `Vector2(x, y)`
    directly and gives everything else to Godot's own constructor.
  - So `Vector2(Vector2i(1, 2))` works, and `Vector2(1, 2, 3)` is an error:
    "no Vector2 constructor takes these arguments".
  - `Vector3` is the same: `Vector3()`, `Vector3(x, y, z)`, or Godot's forms
    (`Vector3(Vector3i)`).
- **Typed engine arguments and properties check the dimension.** When a
  vector goes into a typed Vector2/Vector2i or Vector3/Vector3i slot, the
  other dimension is an error: "Vector2 where a Vector3 is expected".
  - This covers engine method arguments (`to_native`), property writes (the
    setter fast path), and simple getters/setters.
  - Godot's own conversions stay: float vectors convert to Vector2i/Vector3i
    of the same dimension, and int ↔ float.
  - Builtin method arguments that go through the Variant call get Godot's
    own check ("has_point: argument 1 should be Vector2i").
- **Call errors use the readable messages of
  [0037](0037-script-errors.md)** in the two places that still printed
  "error N calling 'name'".

## Consequences

- **Wrong-dimension values fail at the call** with a clear message, instead
  of becoming the wrong value silently.
- **No cost:** `api_object_prop_set` 33.7–34.2 ns, as before (GDScript 26.6).
  The check is one type comparison on a path that already reads the vector.
- **Script-declared types aren't covered yet:** a property declared `Vector3`
  holding a 2D vector is still converted when Godot reads it. Making those
  strict would mean checking on assignment.
- **Tests:** `demo/checks.gd` covers:
  - both wrong constructor forms and the accepted Godot forms;
  - wrong-dimension property writes in both directions;
  - an engine method argument;
  - a builtin method argument (Godot's check), with a Vector2 still accepted
    as a Vector2i.

  The fork tests `vectorCtorArgs` with and without vector kinds.
