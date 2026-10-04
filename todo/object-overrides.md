# Object overrides (_get_property_list, _validate_property, _to_string)

**Area:** Script features

## What

Scripts overriding `_get_property_list`, `_validate_property`, `_to_string`,
`_property_can_revert`/`_property_get_revert`. `_get`, `_set` and
`_notification` are done ([ADR 0032](../docs/adr/0032-script-declarations.md)).

## Approach

Route the matching instance-info callbacks to script methods when defined,
found once at load like `_get` (a method pointer, nothing per call otherwise).

## Done when

Each override is called in the right situations; scripts without them see no slowdown in the benchmark.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.

