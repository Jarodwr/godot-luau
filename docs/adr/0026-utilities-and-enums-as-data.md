# 0026. Utility functions, global enums and class constants from generated data

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Scripts need Godot's global functions (`lerp`, `clampf`, `randf`,
`deg_to_rad`, `max`…), global enum values (`KEY_SPACE`, `OK`,
`MOUSE_BUTTON_LEFT`) and class constants (`Node.NOTIFICATION_READY`). Calling
a utility function needs its hash, from `extension_api.json`.

## Decision

- **Generated as data.** `tools/gen_api_data.py` emits sorted tables:
  - `utility_data.inc`: name, hash, return and argument type codes, vararg;
  - `global_enum_data.inc`: name and value.
- **Resolved through the globals table's `__index`,** then stored in `_G`, so
  later reads are plain table reads.
- **Utility functions** are called through their typed pointers
  (`variant_get_ptr_utility_function`), with arguments converted like native
  engine calls. Vararg ones (`max`, `str`, `push_warning`…) take Variants.
- **Class constants** are resolved on first use through ClassDB and stored on
  the class table.
- Names follow Godot exactly (no renaming, unlike GLS).

## Consequences

- Reading a constant or enum costs a table read after the first time (2 ns,
  against GDScript's 6.8).
- The six utilities taking or returning packed arrays or `RID`
  (`var_to_bytes`, `rand_from_seed`…) raise "invalid arguments" for now.
- Utilities typed `Variant` (`lerp`, `clamp`) build Variants per argument
  (`api_utility_fn`: 37 ns vs 9). The float/int versions (`lerpf`, `clampf`)
  are cheaper.
