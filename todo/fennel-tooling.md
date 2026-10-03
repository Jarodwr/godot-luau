# Fennel tooling parity

**Area:** Editor and tooling

## What

What fennel-gdextension offers Fennel users: macros (`extend-node`, `export`, `signal`…), error positions in Fennel source, fennel-ls support, the compile cache.

## Approach

Port the macros as plain Fennel macro modules targeting this project's script table format. These are features, not optimisations (ADR 0014 still applies: same runtime, same performance).

## Done when

A Fennel script written with `extend-node` runs here unchanged, or with a documented, mechanical difference.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
