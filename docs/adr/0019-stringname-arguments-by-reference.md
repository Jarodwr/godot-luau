# 0019. Pass StringName arguments by reference to the atom's name

- **Status:** Proposed
- **Date:** 2026-10-03

## Context

For a StringName argument (`has_method("helper")`, `Input:is_action_pressed
("jump")`), the native route copies the atom's `StringName` into the argument
slot and destroys the copy afterwards: two engine calls. The argument is
passed by pointer and never modified. `api_string_arg` costs 34 ns vs 18 for
GDScript.

## Decision

When the Lua string has an atom, we will pass a pointer to the atom's
`StringName` directly and construct nothing. We will also extend the
simple-call path ([0012](0012-simple-call-fast-path.md)) to one StringName
argument with a simple result, which covers `is_action_pressed`,
`has_method`, `is_in_group` and similar calls.

## Consequences

- No construction or destruction for name arguments that have atoms.
- Measure `api_string_arg`.
