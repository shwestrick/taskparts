# taskparts (minimal)

A bare-minimum work-stealing scheduler for binary fork join, in about
500 lines of header-only C++17. The whole interface is:

```c++
#include <taskparts/taskparts.hpp>

taskparts::pardo(f, g);      // run f() and g() in parallel; return when both are done
taskparts::launch(f, P);     // run f() on a scheduler with P workers (optional)
```

`pardo` called outside of `launch` starts a scheduler for its duration.
The number of workers defaults to `TASKPARTS_NUM_WORKERS` if set, and
to the number of hardware threads otherwise.

## How it works

- `pardo(f, g)` creates a *join point* in its own stack frame, pushes
  it onto the worker's Chase-Lev deque, and runs `f()`. If the join
  point is still in the deque when `f()` returns, the worker pops it
  and calls `g()` directly, so the common case costs one push and one pop.
- A thief that steals a join point runs `g()` on a new *fiber*: a
  heap-allocated call stack with a guard page.
- If `g()` was stolen, the two branches race to the join. Each one
  first switches to its worker's scheduler stack and then decrements
  the join counter. The branch that arrives first goes back to
  stealing. The branch that arrives second resumes the pardo's
  continuation right away by restoring the context saved in the join
  point. This keeps the usual work-stealing bounds (at most P busy
  leaves), because no worker ever blocks at a join.
- Contexts are saved and restored by a few lines of assembly
  (`context.hpp`) for x86-64 and arm64.

## Files

- `include/taskparts/context.hpp`: context save/restore and fiber trampolines
- `include/taskparts/deque.hpp`: fixed-capacity Chase-Lev deque
- `include/taskparts/taskparts.hpp`: join points, fibers, workers, `launch`, `pardo`
- `example/fib.cpp`, `test/test_pardo.cpp`

## Building

```
make           # builds ./fib and ./test_pardo
make test
TASKPARTS_NUM_WORKERS=8 ./fib 42
```

Compile-time knobs: `TASKPARTS_STACK_SZB` (fiber stack size, default 1 MiB)
and `TASKPARTS_DEQUE_CAPACITY` (default 4096).

## Limitations

- `f` and `g` must not throw.
- A fiber may migrate between threads at any `pardo`. Don't hold
  thread-local addresses or thread-affine resources such as locks
  across a `pardo`.
- Idle workers spin, yielding every 64 failed steal attempts.
