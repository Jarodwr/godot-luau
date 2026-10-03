# 0009. Try Luau native code generation

- **Status:** Proposed
- **Date:** 2026-10-03

## Context

Luau ships a native code generator (`Luau.CodeGen`, arm64 and x64) that
compiles functions to machine code after loading. The VM is the largest share
of the property cases (~74% in `api_object_prop_get`) and 16% of `_process`.
Much of that is the lookup chain and metamethod calls, which native code may
or may not speed up.

## Decision

We will try native code generation as a separate, measured step after
[0002](0002-relocate-script-call-results.md)–[0008](0008-luau-compiler-options.md):
- link `Luau.CodeGen`;
- create the code generator for the state;
- compile each script's functions after loading.

We keep it only if it clearly helps `process_nodes` or the property cases.

## Consequences

- Adds a larger dependency, a few seconds on a clean build, and native code
  to every script function's memory.
- Not available on every platform Godot runs on (e.g. web), so the interpreter
  path must stay fully supported.
- The outcome is unknown until measured; a flat result is recorded as such.
