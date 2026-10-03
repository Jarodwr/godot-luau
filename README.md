# godot-luau-spike

A deliberately small Luau scripting extension for Godot 4.5+. It exists to
answer three questions:
- How fast does a Luau binding build?
- How close does it get to GDScript?
- Does Fennel run on it?

The findings are recorded in fennel-gdextension's
`docs/perf/15-luau-spike.md`.

## Build

```sh
mise install                       # cmake, ninja, python
mise exec -- cmake --preset dev    # configure (native arch, editor, optimised)
mise exec -- cmake --build --preset dev
```

The library lands in `addons/godot_luau/bin/`. `demo/` and `demo/benchmark/`
use it through a symlink.

**Build times** (Apple Silicon, 10 cores):

| Build | Time |
|---|---|
| Clean, including godot-cpp and Luau | 7.8 s |
| Nothing to rebuild | 0.03 s |
| Edit one source file or `api.h` | 0.65 s |

**What keeps it fast:**
- CMake + Ninja.
- A godot-cpp `build_profile.json` (101 generated files instead of ~2,000).
- No sol2 and no heavy templates.
- Native arch for dev builds.
- Engine bindings as generated **data** (`tools/gen_api_data.py` →
  `api_data.inc`), not code.

## Scripts

A script is a chunk returning a table; functions in it are methods. `.luau`
and `.fnl` (compiled by Fennel running inside Luau) are both loaded.

```lua
local Mover = { extends = "Node2D" }
function Mover:_ready() self.velocity = Vector2(1, 0.5) end
function Mover:_process(delta)
	self.position = self.position + self.velocity * delta
end
return Mover
```

```fennel
(local Mover {:extends "Node2D"})
(fn Mover._ready [self] (set self.velocity (Vector2 1 0.5)))
(fn Mover._process [self delta]
  (set self.position (+ self.position (* self.velocity delta))))
Mover
```

## Design

### `src/api.cpp`: bindings and values
- **Name atoms.** Every short string gets a small integer when Luau creates it
  (`useratom`). Engine members are looked up per class by that integer:
  `ClassInfo::by_atom`, resolved on first use and shared by all objects of
  that class.
- **Objects** are tagged userdata. `obj:method()` goes through `__namecall`
  (`lua_namecallatom`), which goes straight to the cached method bind. No
  string hashing and no per-object caches.
- **Engine calls** use `object_method_bind_ptrcall` with native argument and
  return layouts, from the generated signature table. Varargs calls, calls
  relying on default arguments, object returns and unusual types go through
  Variants (`object_method_bind_call`).
- **Properties** call their getter/setter binds the same way.
- **Values:**
  - Vector2 and Vector3 are Luau's native `vector`, so maths and `.x` happen
    in the VM with no allocation.
  - Other Godot values are Variant userdata.

### `src/script.cpp`: scripts and instances
- `self` is a Lua table:
  - Fields are plain entries.
  - Its metatable sends misses to a per-instance cache of script functions and
    engine method binds, then to the engine.
- `LuauScript`, `LuauLanguage`, the `.luau`/`.fnl` loader, and
  `script.new()`.

## Known gaps (it's a spike)

- **Vector2 and Vector3 share Luau's `vector`.** A vector passed where Godot
  expects a Variant becomes Vector2 if z is 0, else Vector3. Typed engine
  arguments are unaffected.
- **Luau numbers are doubles.** Integers beyond 2^53 lose precision (same as
  LuaJIT).
- **No exports, signals, script inheritance, tool scripts, editor features or
  debugging.** One Luau state, main thread only.
- **Engine methods returning strings convert to Lua strings on every call.**
  `get_name():length()` is 106 ns vs 18 for GDScript; caching those strings
  per name would help.
