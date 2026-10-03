# Modules (require)

**Area:** Runtime

## What

`require` for `.luau` and `.fnl` modules from `res://`, with caching and reload.

## Approach

A `require` that resolves `res://` paths, compiles with the same options, and caches by path; Fennel's `require` uses the same searcher.

## Done when

Two scripts share a module; editing it reloads it.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
