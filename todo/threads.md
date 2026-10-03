# Threads

**Area:** Runtime

## What

Scripts used from threads other than the main one (`WorkerThreadPool`, `Thread`).

## Approach

Today there is one Luau state used only from the main thread. Options: a lock like fennel-gdextension's, or a Luau thread per Godot thread over the shared global state. Must not slow the main-thread path.

## Done when

A script method called from a WorkerThreadPool task works, and main-thread benchmarks don't move.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
