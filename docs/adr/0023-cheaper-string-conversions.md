# 0023. Cheaper string conversions instead of a cache

- **Status:** Accepted
- **Date:** 2026-10-03
- Supersedes [0018](0018-string-conversion-cache.md)

## Context

Godot's `String` is UTF-32. Luau's strings are interned UTF-8 bytes, and
Lua's string semantics (`#`, `string.sub`, patterns, `==`) depend on that. So
every string crossing between them must be re-encoded. GDScript never
converts: its values are Variants, and a string is a reference-counted
`String`.

[0018](0018-string-conversion-cache.md) hid the conversion behind a two-way
cache:
- 1024 slots per direction;
- entries holding both sides alive;
- strings admitted on their second sighting, judged by address plus a hash of
  the bytes.

It made repeated strings nearly free but had real downsides:
- hidden state, and memory kept alive by the entries;
- an address-plus-hash heuristic;
- a shared `static` fallback string that wasn't thread- or
  re-entrancy-safe;
- no help for strings that don't repeat.

The conversions themselves also wasted work:
- **Godot → Lua:** `String::utf8()` allocated a temporary `CharString`.
- **Lua → Godot:** we decoded into a temporary `String`, copied it into the
  Variant or argument (an engine call) and destroyed the temporary (another).

## Decision

We remove the cache and make each conversion cheaper:
- **Godot → Lua:** `string_to_utf8_chars` encodes into a 512-byte local
  buffer on the stack. Each call has its own, so it's thread- and
  re-entrancy-safe. Longer strings get one exact-size allocation.
- **Lua → Godot:** the `String` is constructed directly in its destination,
  with nothing in between:
  - the argument slot of a native call;
  - the data of a Variant (argument arrays, script call results).
- **ASCII fast path:** all-ASCII strings use
  `string_new_with_latin1_chars_and_len`, which widens bytes without UTF-8
  validation; others use the UTF-8 constructor.
- **No copy out of a STRING Variant:** it's read in place.
- **Layout check:** `check_variant_layout` confirms that a STRING Variant
  holds its `String` at the data offset. If not, the generic path is used.

## Consequences

- No hidden state, no retained memory, no heuristics. Repeated and one-off
  strings cost the same.
- Repeated strings are slower than with the cache (117 vs 72 ns per round
  trip). One-off strings are faster than with it (166 vs 210).
- **What's left** is inherent to having two string types:
  - Godot allocating and freeing its buffer;
  - Luau hashing and interning each incoming string;
  - the re-encoding pass.

  Closing it fully would need both sides to share one representation, which
  neither Luau nor Godot can offer.
- `demo/checks.gd` covers ASCII, non-ASCII, empty and longer-than-buffer
  strings, Lua concatenation, `#` on UTF-8, and StringName arguments.

## Result

ns per op, three builds back to back, two rounds (15 repeats, macOS arm64).
"No cache" is ADR 0017's build.

| Case | No cache | Cache (0018) | This | GDScript |
|---|---:|---:|---:|---:|
| `echo_string` | 157–172 | 72–74 | 117 | 70 |
| `echo_string_unique` | 202–207 | 208–213 | 165–168 | 97–99 |
| `call_args6` | 146–153 | 86–88 | 101–105 | 73 |
| `api_object_method` | 40 | 39–41 | 40 | 18 |
