# 0037. Script errors with location and backtrace, at no cost when nothing fails

- **Status:** Accepted
- **Date:** 2026-10-05

## Context

Errors were printed with `push_error` and Luau's one-line message
(`res://player.luau:12: attempt to index nil`), which has three problems:
- Godot treated them as engine errors from `variant_utility.cpp`, not script
  errors, so the editor's debugger couldn't link to the script line.
- There was no backtrace.
- Failed engine calls said `error 2 calling Node.add_child (argument 0)`.

Fennel errors pointed at lines of the compiled Lua, not the `.fnl` file.

A traceback is usually collected by passing a message handler to
`lua_pcall`, which runs while the failing frames still exist. Pushing a
handler on every call costs a few nanoseconds, and calls from Godot are the
hot path.

## Decision

- **Report errors as script errors.** Reports go through
  `print_script_error` (Godot's `SCRIPT ERROR`) with:
  - the message, without Luau's `file:line:` prefix;
  - the innermost script location as function, file and line, which the
    debugger links to;
  - a `Luau backtrace (most recent call first)` of the script frames, when
    there is more than one. C frames are left out, as GDScript leaves out
    engine frames.
  - Unnamed functions (closures, Fennel `fn`s) show as
    `function at line N`.
  - Godot adds its own GDScript backtrace below.
- **Calls from Godot** (methods, `_notification`, Callables, resumed awaits,
  `spawn`) run on coroutines ([0035](0035-await-on-pooled-threads.md)). When
  one fails, Luau keeps its frames, so the backtrace is read after the
  failure. Nothing runs on the success path.
- **Getters, setters, `_get`/`_set` and loading a chunk** use `lua_pcall` on
  the main thread with a reporting message handler. The handler is installed
  once, in the main thread's first stack slot, so each pcall passes its index
  and pushes nothing.
- **Syntax and Fennel compile errors** are parsed for their `res://file:line:`
  prefix and reported at that location.
- **Fennel compiles with `correlate`,** so the Lua lines match the `.fnl`
  lines and runtime errors point at the Fennel source.
- **Engine call errors** say what was wrong:
  - `Node.add_child: argument 1 should be Object`;
  - `too few arguments (expected at least 2)`;
  - `no such method`;
  - `the object is null`.

## Consequences

- **Errors look and link like GDScript's:**
  ```
  SCRIPT ERROR: attempt to index nil with 'field'
  Luau backtrace (most recent call first):
      [0] level3 (res://errors/thrower.luau:19)
      [1] level2 (res://errors/thrower.luau:20)
      [2] chain (res://errors/thrower.luau:21)
            at: level3 (res://errors/thrower.luau:19)
  ```
- **No cost when nothing fails:**
  - `call_noop` 37 ns;
  - `prop_get_accessor` 54 ns (57 before);
  - `api_property_accessor` 77 ns (unchanged).
- **Inlined functions don't appear as frames.** Luau's optimisation level 2
  inlines small local functions, so an error inside one is reported at the
  right file and line but under the calling function's name.
  - Level 1 keeps every frame, but `vm_function_calls` goes from 4.4 to
    7.2 ns, and `vm_string` from 70 to 75, so level 2 stays.
  - A project setting could choose level 1 for debugging if this turns out
    to matter.
- **Tests:** `tools/check_errors.sh` runs `demo/errors.gd`, which triggers
  each kind of error, and checks the reported messages, locations and
  backtraces:
  - nested and chained calls;
  - a getter;
  - an engine call;
  - an error after `await`;
  - a signal handler closure;
  - a syntax error;
  - a Fennel runtime error.
