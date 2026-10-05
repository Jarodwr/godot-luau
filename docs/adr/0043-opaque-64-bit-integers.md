# 0043. Integers beyond ±2^53 are opaque 64-bit values

- **Status:** Accepted
- **Date:** 2026-10-05

## Context

Luau numbers are doubles, exact for integers up to ±2^53. Godot ints are
64-bit, and some APIs routinely return values beyond that. A RefCounted's
instance ID has bit 63 set, so it came into Lua with its low digits lost, and
`instance_from_id(r:get_instance_id())` returned nothing.

From the API data, the values affected are:
- object IDs: `get_instance_id`, collider IDs, physics owner IDs;
- resource UIDs;
- `RandomNumberGenerator` seed and state;
- handles and addresses;
- raw 64-bit data: `FileAccess.get_64`, `PackedInt64Array`, decoded save or
  network data.

Godot's own type metadata can't single them out: frame counters, tick counts
and file sizes are tagged 64-bit too, and those must stay numbers.

Options considered:
- **API-based** (always pack values from ID-returning APIs): IDs would get a
  consistent type, but only from a hand-kept list. IDs arriving untyped (in
  Dictionaries, metadata, `call` results) would be missed, and Godot's
  "no object" `0` would no longer equal `0` in Lua.
- **A number subtype in a Luau fork** (Lua 5.3 style): fixes this and
  int/float together, but it's the largest fork change, it changes some
  language behaviour, and it diverges from upstream's own integer type.
- **Packed values with full arithmetic:** the IDs, UIDs and handles above
  are never used for arithmetic, and their arithmetic would need its own
  rules. Not needed.

## Decision

- **Chosen by value:** an int from Godot within ±2^53 is a number, as
  before. Beyond that it's an opaque 64-bit value: a tagged light userdata
  (`LUTAG_INT64`) holding the bits, with no allocation.
- **What opaque values do:**
  - compare with each other: `==` (exact, Luau's own), `<`, `<=`;
  - work as table keys;
  - print all their digits (`tostring`, `print`, `..`);
  - go back to Godot as the exact int, wherever an int or a Variant is
    accepted;
  - `typeof` gives `"int64"`.
- **Arithmetic on them is an error** that says why.
- `typeof` also names `Vector2i` and `RID` now.

## Consequences

- **Exact through Lua:** `instance_from_id(id)`, UIDs, `rng.state = saved`,
  and big values in `PackedInt64Array`, Dictionaries and metadata.
- **Small values are unchanged,** so `id == 0` still works for "no object".
  Node IDs, which fit, stay numbers: an ID's type depends on its size, which
  only shows if code inspects `typeof`.
- **Luau limits:**
  - comparing an opaque value with a number (`id < 5`) errors, because Luau
    only orders values of the same type;
  - `math`, `string.format` and `tonumber` don't accept them;
  - a big literal in Lua source is a double.
- **Cost:** no measurable cost. Integer crossings go through `push_int`/`to_int`
  (a range check), and `echo_int`, `call_add2`, `api_utility_fn` and
  `api_vector2i_math` are unchanged. The arithmetic check runs only after the
  Vector2i path.
- **Open:** whole numbers still go to untyped destinations as `int`
  (`todo/integer-types.md`).
- **32-bit platforms** have no packed values: big ints stay lossy numbers
  there.
- **Tests:** `demo/checks.gd` covers:
  - `instance_from_id`, equality, table keys, ordering against Godot's ints;
  - printing and concatenation;
  - the arithmetic and mixed-comparison errors;
  - small IDs staying numbers;
  - an RNG state restore;
  - `PackedInt64Array`, Dictionary and metadata.
