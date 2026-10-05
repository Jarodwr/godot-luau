# Godot fork of Luau

This fork carries a few small patches for
[godot-luau](https://github.com/Jarodwr/godot-luau). Each one is behind a build
option that is off by default: with every option off, Luau behaves as upstream.

## Rebasing

Patches are kept as separate commits on the `godot-*` branches, with call-site
changes of one line each where possible, so conflicts with upstream stay
small. After a rebase, run the conformance tests with each option on and off
(see each patch's "Tests").

## Vector kinds (`LUAU_VECTOR_KINDS`, branch `godot-vector2`)

**Why.** Godot has Vector2 and Vector3. Luau has one `vector` type, so a
Vector3 with z = 0 can't be told apart from a Vector2 when it crosses into
Godot. Shader parameters, tweens, metadata and other "any type" APIs then
receive the wrong type.

**What.**
- **The type:** a vector built from two components is a separate value type,
  `LUA_TVECTOR2`, right after `LUA_TVECTOR`. It's the same 16-byte value with
  z = 0, and `type()` and `typeof()` still say `"vector"`.
- **Rules:**
  - The two-argument constructor (`vector.create(x, y)`, or the compiler's
    `vectorCtor` with two arguments) makes a `LUA_TVECTOR2`.
  - Arithmetic gives a `LUA_TVECTOR2` only when every vector operand is one.
    The `vector` library follows the same rule over its vector arguments
    (`min` and `max` with any number of them, `clamp`, `lerp`), in both its
    fast calls and its library functions. `vector.cross` is always 3D.
  - `==` and table keys distinguish the two types (Luau compares types first).
  - Each type has its own metatable, so the host can give Vector2 and
    Vector3 their own methods. The `vector` library gives both its own
    metatable.
  - A 2D result's z is always 0 (`v2 / v2` doesn't leave NaN in it).
  - Field access on a `LUA_TVECTOR2` covers `x` and `y` (in the interpreter
    and in the library's `__index`). Other names go to the metatable.
  - A `LUA_TVECTOR2` prints two components.
- **C API:**
  - `lua_pushvector2(L, x, y)`;
  - `lua_userdatadirectfield_setvector2(result, x, y)`;
  - `lua_type` returns `LUA_TVECTOR2`;
  - `lua_isvector` and `lua_tovector` accept both types.
- **Sandbox:** `luaL_sandbox` freezes the 2D vector metatable too (with
  `LuauSandboxFreezesVectorMetatable`).
- **Compiler:** with the option on, two-argument constructor calls with
  constant arguments aren't folded into bytecode constants (a constant has no
  kind). They're built at run time by the constructor's fast call.
- **Not supported:**
  - Native code (`Luau.CodeGen`): with the option on it reports itself as
    unsupported (`luau_codegen_supported()` returns 0), so code stays in the
    interpreter.
  - 4-wide and double vectors (`luaconf.h` errors).

**Where.**
- `luaconf.h`: the option.
- `lua.h`: the type, `lua_isvector` and `lua_pushvector2`.
- `lobject.h`: `ttisvector`, `vectortag`, `vectortag2`, `case_vector2` and
  `setvvaluet`.
- One-line changes in `lvmexecute.cpp`, `lvmutils.cpp`, `lbuiltins.cpp`,
  `lapi.cpp`, `lobject.cpp`, `ltable.cpp`, `laux.cpp`, `lgcdebug.cpp` and
  `ltm.cpp`.
- `Compiler/src/BuiltinFolding.cpp`.
- `CMakeLists.txt`.

**Tests.**
- **New** (only with the option on):
  - `tests/conformance/vector_kinds.luau` runs at -O0, -O1 and -O2, each with
    both table lookup implementations (`DFFlag::LuauSplitTableLookups` off
    and on). It has examples, and a randomized check of every vector
    operation on random kinds against the rule and a double-precision
    reference. Library functions are called both as fast calls and through a
    table, so the two implementations are compared.
  - `VectorKindsApi` covers the C API and the sandbox.
  - `handler_setvector2_result` covers direct fields.
- **Mutation-tested:** each of these was broken on purpose and the new tests
  failed:
  - arithmetic, constructor and library kind rules;
  - z of 2D results;
  - table keys in the split lookup;
  - equality;
  - the sandbox freeze.
- **Option off:** every upstream test passes.
- **Option on:** three upstream tests fail, as they test the behaviour this
  patch changes:
  - `Conformance/VectorLibrary` (`vector_library.luau:27`): reads `.z` of
    `vector.create(1, 2)`, which is now 2D.
  - `IrLowering/TableNodeLoadStoreProp5`: native-code output for
    `vector.create(.5, .5)`, which is no longer folded.
  - `Compiler/VectorConstants`: expects `vector.create(1, 2)` to fold into a
    constant.

To run them:

```sh
cmake -B build-kinds -DLUAU_BUILD_TESTS=ON -DLUAU_VECTOR_KINDS=ON && cmake --build build-kinds
build-kinds/Luau.Conformance    # from the repository root
build-kinds/Luau.UnitTest
```
