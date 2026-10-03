# Object overrides (_notification, _get, _set, _get_property_list)

**Area:** Script features

## What

Scripts overriding `_notification`, `_get`, `_set`, `_get_property_list`, `_validate_property`, `_to_string`.

## Approach

Route the matching instance-info callbacks to script methods when defined. ADR 0005's routes already treat `_get`/`_set` specially; keep those paths off the hot path when a script doesn't define them.

## Done when

Each override is called in the right situations; scripts without them see no slowdown in the benchmark.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.

## Benchmark cases

- `api_get_override`
- `notification_into_script`

GDScript and godot-luau-script numbers for these are in
[`docs/comparisons/godot-luau-script.md`](../docs/comparisons/godot-luau-script.md#feature-cases).
