# 0020. Direct indexed access on Array and Dictionary

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

`arr[i]` on a Godot Array goes through the generic Variant `__index`:
- the key is converted to a Variant;
- `Variant::get` runs;
- the result is copied and pushed.

`api_array_read` costs 23 ns vs 14 for GDScript.

## Decision

We will give Array and Dictionary userdata their own `__index`/`__newindex`:
- integer keys on arrays go through the interface's indexed getters and
  setters (`variant_get_indexed`/`variant_set_indexed`);
- keys on dictionaries go through the keyed ones;
- results are pushed through the plain-value path where possible.

Method calls (`arr:size()`) keep going through `__namecall`.

## Consequences

- Indexing becomes one engine call plus a push.
- Measure `api_array_read` and, once constructors and iteration exist,
  `api_dict_rw` and `api_array_iterate`.

## Result

ns per op, measured with `tools/bench.sh` and back-to-back runs (15 repeats,
macOS arm64).

Implemented for Array, packed arrays and Dictionary, both reading and
writing. Variant values had no `__newindex` before. A missing key or an
out-of-range index reads as `nil`. `demo/checks.gd` covers reads, writes and
out-of-range reads.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_array_read` | 23.7 | 20.0 | 14 |
