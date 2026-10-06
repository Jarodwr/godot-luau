# 0054. Builds and tests for every platform in CI

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

godot-luau was built and tested only on macOS, and `godot_luau.gdextension`
listed only a macOS library: a public checkout didn't run anywhere else, and
nothing showed whether the code built with GCC or MSVC.

## Decision

- **Libraries per platform** in `addons/godot_luau/bin/`, named in
  `CMakeLists.txt` and listed in the `.gdextension`:
  `libgodot_luau.dylib` (macOS, universal in CI),
  `libgodot_luau.linux.{x86_64,arm64}.so`,
  `godot_luau.windows.x86_64.dll`.
- **`.github/workflows/build.yml`:** on each of those four builds, configure
  with Ninja, build, download that platform's Godot (4.7.2) and run every
  suite with `tools/run_tests.sh`. A package job assembles the addon
  (libraries, `fennel.lua`, the `.gdextension`, the definitions) as the
  `godot-luau` artifact; a `v*` tag publishes it as a release.
- **`tools/run_tests.sh`:** the editor's scan, then `checks`, `smoke`,
  `fennel_smoke`, `global_class`, `hot_reload`, `check_errors` and
  `check_editor`, each with a time limit. A suite fails on a non-zero exit, a
  `FAIL` line, or missing expected output.
- **Portability:** MSVC needs `__forceinline` where GCC and Clang take
  `__attribute__((always_inline))`.

## Consequences

- A Linux build (GCC 13, in Docker) built without warnings, and the suites
  found a bug in a fresh project: the editor's first scan loads every script
  to find its `class_name`, and a script extending another Luau class by name
  ([0049](0049-extends-by-class-name.md)) couldn't load while its base wasn't
  registered yet, so it was never registered itself. Now `class_name` is read
  before `extends`; in the editor, an unknown base is pending (the class
  registers with that base, without an error), and the script loads again
  when it's next used or scanned. A failed `extends` also no longer leaves
  the script marked as reloading, which had blocked later reloads.
- The libraries are built for Godot's `editor` target and used for exported
  games too; exports aren't tested yet.
