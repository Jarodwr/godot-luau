# Signals

**Area:** Script features

## What

Declaring script signals, emitting them (`self.ticked:emit(1)`), connecting to them from Lua and GDScript, receiving engine signals in script methods, and `Signal` values in Lua.

## Approach

Needs a declaration format, `_has_script_signal`/`_get_script_signal_list`, a `Signal` value type in Lua (Variant userdata with `emit`/`connect`), and Callables wrapping Lua functions for `connect` (see [callables](callables.md)).

## Done when

Benchmark cases `api_signal_emit` and `signal_into_script` run. A Luau script can connect a button's `pressed` to a Luau function.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
