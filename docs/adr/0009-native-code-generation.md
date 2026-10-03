# 0009. Try Luau native code generation

- **Status:** Rejected
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

## Result

ns per op, measured with `tools/bench.sh` (9 repeats, macOS arm64). Each
row compares the build before and after this change.

Implemented behind the CMake option `GODOT_LUAU_CODEGEN`, now **off by
default**.

| Case | Interpreter | Native | GDScript |
|---|---:|---:|---:|
| `vm_fib` | 8.5 | 6.3 | 70.1 |
| `vm_function_calls` | 4.7 | 3.1 | 54.0 |
| `api_self_method` | 8.9 | 7.2 | 53.3 |
| `api_vector2_field` | 1.5 | 0.6 | 8.5 |
| `vm_loop_arith` | 4.3 | 5.3 | 14.3 |
| `api_object_prop_get` | 31.2 | 32.5 | 20.1 |
| `api_object_prop_set` | 38.0 | 40.0 | 25.9 |
| `process_nodes` | 145.8 (1.08×) | 160.3 (1.11×) | 134.6–143.8 |

It speeds up pure script logic, but not engine access or per-frame code, which
are what this ADR set out to improve. It also slows some loops and adds 2.4 s
to a clean build (10.2 s vs 7.8 s). Turn the option on for projects whose
scripts are mostly computation.
