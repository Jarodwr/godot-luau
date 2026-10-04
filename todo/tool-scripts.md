# Tool scripts

**Area:** Script features

## What

Scripts that run in the editor (`tool = true`).

## Status

`tool = true` in the class table, `_can_instantiate` and placeholder instances
for non-tool scripts in the editor are implemented
([ADR 0032](../docs/adr/0032-script-declarations.md)). Not yet checked in the
editor; reloading running tool scripts is part of hot reload.

## Done when

A tool script draws something in the editor viewport.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
