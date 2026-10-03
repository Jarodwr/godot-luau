# Editor: validation and error reporting

**Area:** Editor and tooling

## What

Syntax and compile errors shown in the script editor with line numbers, for both `.luau` and `.fnl`.

## Approach

`_validate` using `luau_compile`'s error output and Fennel's compiler errors (with Fennel source positions; see fennel-gdextension's source maps).

## Done when

A syntax error is underlined in the editor at the right line, for Luau and Fennel.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
