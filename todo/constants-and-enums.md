# Class constants, enums and global enums

**Area:** API surface

## What

`Node.NOTIFICATION_READY`, `Node.PROCESS_MODE_ALWAYS`, `KEY_SPACE`, `MOUSE_BUTTON_LEFT`, …

## Approach

Class tables (`Node`, …) answer constants through ClassDB (`class_get_integer_constant`), cached per class. Global enums generated as data from `extension_api.json`.

## Done when

Benchmark case `api_constant` runs.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.

## Benchmark cases

- `api_global_enum`

GDScript and godot-luau-script numbers for these are in
[`docs/comparisons/godot-luau-script.md`](../docs/comparisons/godot-luau-script.md#feature-cases).
