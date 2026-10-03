# Hot reload

**Area:** Editor and tooling

## What

Editing a script while the game runs updates running instances.

## Approach

`_reload` with `keep_state`: rebuild the class table, keep self tables' fields, clear routes and B caches.

## Done when

Changing a method body while the game runs takes effect without restarting.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
