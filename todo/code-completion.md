# Editor: code completion

**Area:** Editor and tooling

## What

Completion of engine API, script members and locals.

## Approach

`_complete_code`; possibly reuse Luau's analysis (Luau.Analysis) with generated type definitions for the Godot API.

## Done when

Typing `self:` lists the node's methods.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
