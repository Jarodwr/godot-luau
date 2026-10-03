# await / coroutines

**Area:** Script features

## What

Waiting on signals and timers inside script methods (`await` in GDScript), e.g. `wait(self:get_tree():create_timer(1).timeout)`.

## Approach

Run script methods that may yield as Luau coroutines (`lua_resume`), resume them from a one-shot Callable connected to the signal. Plain calls should keep today's direct `lua_pcall`.

## Done when

A script method can wait for a timer and continue. `api_async_method_call` keeps its current speed.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
