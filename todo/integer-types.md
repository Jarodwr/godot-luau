# Integers and integer vectors

**Area:** API surface

## What

Luau numbers are doubles: integers beyond 2^53 lose precision and whole floats can't be told from ints. `Vector2i`/`Vector3i` have no native form.

## Approach

Decide a policy (e.g. typed engine arguments already convert correctly; document the Variant-context rule). Possibly use Luau's experimental integer support if it stabilises.

## Done when

Documented behaviour, with tests for large IDs (e.g. `get_instance_id()`) round-tripping.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
