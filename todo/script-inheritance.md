# Script inheritance

**Area:** Script features

## What

A script extending another Luau/Fennel script (`extends = "res://base.luau"`), with overriding and calling the base implementation.

## Approach

`_get_base_script`, `_inherits_script`, method lookup falling back to the base script's class table, member routes per script chain.

## Done when

A derived script overrides one method, calls the base version, and inherits the rest.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
