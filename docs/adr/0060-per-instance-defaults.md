# 0060. Defaults are evaluated for each instance

- **Status:** Accepted
- **Date:** 2026-10-15

## Context

A property's default ([0032](0032-script-declarations.md)) was a value,
converted to a Variant once, when the script loaded. Arrays, dictionaries
and objects are shared by reference, so every instance started with the
same one: after `a.items.append(1)`, a second node's `items` was `[1]` too.
A type alone (`items = gd.Array`) shared its one empty Array the same way.
GDScript runs `var items = []` for each instance, so each node has its
own.

Copying the value for each instance would fix arrays, but not objects
(`gd.Curve.new()` ran once), and a copy isn't the same as running the
expression again.

Declarations had several shapes: `speed = 1.5` (a default),
`target = gd.Node` (a type) and `{ type = …, default = … }`.

An empty Lua table converts to an Array, since nothing in `{}` says it's a
map.

## Decision

- **A function default is called for each instance,** when it's created,
  as GDScript runs an initializer:
  `items = { default = function() return {} end }`. The result is
  converted to the declared type. A reload that adds a property calls it
  for live instances, and the editor's revert calls it too.
- **It's also called once at load,** for the editor's default value (what
  the inspector shows and reverts to) and, without a `type`, for the type.
  That value is never given to an instance.
- **Any other default is a value no instance can change for another:** a
  number, string, boolean, vector, colour, packed array and so on. A table,
  Array, Dictionary or object default is an error that names the function
  form.
- **A type alone builds a new zero value per instance,** so
  `{ type = gd.Array }` is a new empty Array for each.
- **Every declaration is a table:** `{ default = 1.5 }`, `{ type = gd.Node }`.
  A bare value or type in `exports` or `properties` is an error.
- **An empty table typed Dictionary is a Dictionary:** converting an empty
  Array to `Dictionary` gives an empty Dictionary wherever a value is
  converted to a declared type (defaults, typed arguments and returns).
- **Fennel's `prop` writes the function itself**
  ([0057](0057-defnode-macro.md)): a literal number, string or boolean is
  the value, and anything else, such as `[]` or `(gd.Curve.new)`, becomes
  `(fn [] …)`. `{}` is a map in Fennel, so the macro makes it
  `(gd.Dictionary)`. A default that reads `self` is a compile error at
  its form, rather than a nil error when the script loads.

## Consequences

- Defaults behave as GDScript's do. A non-literal default costs a Lua call
  and a conversion per instance, which is what GDScript's initializer
  costs; literal defaults cost what they did.
- A default function takes no arguments: it can't read `self` or another
  property. Code that needs them goes in `_init` or `_ready`.
- A default function runs once more at load. If it raises, the error is
  reported and the property falls back to its type's zero value.
- Every Luau shorthand declaration changes; the old forms fail loudly.
- Tests:
  - `features/fresh_defaults.luau` (`checks.gd`) and
    `features/fennel_fresh_defaults.fnl` (`fennel_smoke.gd`) change one
    instance's values and read another's: arrays, a nested array, a
    Dictionary, a type-only Array and a Curve;
  - `tools/check_errors.sh` checks the errors for a shorthand export and a
    shared table default, and a `defnode` default that reads `self`.
