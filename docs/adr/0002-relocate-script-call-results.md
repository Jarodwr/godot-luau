# 0002. Return script call results without a Variant move

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Profiling `call_noop` (a bare call from Godot into a script method) shows
`godot::Variant::operator=(Variant&&)` as the single largest symbol. `call_func`
writes the result with `*(Variant *)r_ret = to_variant(...)`. godot-cpp
implements the move as `std::swap` of the Variant's 24-byte buffer, out of line
and byte by byte. The same cost was found and removed in fennel-gdextension
(its docs/perf/14).

## Decision

We will write script call results without godot-cpp's move:
- When the result is `nil` and the return slot is already nil (as Godot passes
  it), nothing is written.
- Otherwise the converted value is relocated into the slot with `memcpy`, after
  destroying what the slot held. This is the same thing the swap does.

## Consequences

- Removes the largest binding cost from every call into a script (`_process`,
  signals, calls from GDScript).
- Relies on Variants being relocatable by bytes, which godot-cpp's own move
  constructor already assumes.
- Measure `call_noop`, `call_add2` and `process_nodes` before and after.

## Result

ns per op, measured with `tools/bench.sh` (9 repeats, macOS arm64). Each
row compares the build before and after this change.

Implemented as described (`to_variant_into_nil`).

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `call_noop` | 48.1 | 37.9 | 37.3 |
| `call_add2` | 68.8 | 61.4 | 59–65 |
| `echo_int` | 61.0 | 54.7 | 52 |
| `echo_vector2` | 67.0 | 60.2 | 57–63 |
