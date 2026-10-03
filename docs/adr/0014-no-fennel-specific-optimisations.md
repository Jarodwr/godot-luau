# 0014. No Fennel-specific optimisations

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

After [0011](0011-instance-handle-upvalue.md)–[0013](0013-skip-freed-checks-that-cannot-fail.md),
most of the remaining property-access cost is Luau's own lookup chain
([0010](0010-self-stays-a-table.md)). A Fennel macro could compile
`self.position` to a direct accessor call and skip that chain, but only for
Fennel scripts.

## Decision

We won't add optimisations that only apply to Fennel. Luau and Fennel
scripts go through the same runtime and get the same performance; Fennel
support stays a compiler in front of it.

## Consequences

- One runtime to optimise and test. Fennel performance follows Luau's.
- Engine property access keeps the cost of the lookup chain for both
  languages (27 vs 20 ns read, 33 vs 27 ns write).
