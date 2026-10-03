# Tool scripts

**Area:** Script features

## What

Scripts that run in the editor (`tool = true`).

## Approach

`_is_tool`, placeholder instances for non-tool scripts in the editor, reloading running tool scripts.

## Done when

A tool script draws something in the editor viewport.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
