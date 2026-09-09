# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Coin is an OpenGL-based, retained-mode 3D graphics library implementing the
Open Inventor 2.1 API (scene graph, actions/traversal, fields with
notification, engines, sensors, draggers/manipulators, nodekits). It is
BSD-licensed C++ and does not share code with SGI Open Inventor. GUI toolkit
bindings (SoQt, SoWin, SoXt, Quarter) live in sibling repositories, not here.

## Build

Coin uses CMake (out-of-source builds only — never run `cmake` inside the
source tree).

```
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Key CMake options (see `INSTALL` for the full list):
- `COIN_BUILD_SHARED_LIBS` (default ON) — shared vs. static library.
- `COIN_BUILD_TESTS` (default ON) — build the `CoinTests` executable.
- `COIN_BUILD_DOCUMENTATION` (default OFF) — Doxygen HTML/man pages.
- `CMAKE_PREFIX_PATH` — where to look for optional deps (simage, Qt, etc.).

Build scratch build trees under `/dev/shm/` (tmpfs, RAM-backed) instead of
inside or next to the working tree — faster, and keeps `git status` clean.
An existing configured build tree in this environment lives at
`/dev/shm/coin-build`; rebuild it with `cd /dev/shm/coin-build && make -j16`
rather than reconfiguring from scratch. When verifying a fix under multiple
compilers/sanitizers, use one `/dev/shm` tree per configuration (e.g.
`/dev/shm/coin-build-gcc`, `/dev/shm/coin-build-clang`,
`/dev/shm/coin-build-asan`) rather than reconfiguring one tree repeatedly —
delete each with `rm -rf` once its check is done.

## Tests

Tests are compiled into a single `CoinTests` executable using an in-tree,
minimal Boost.Test-alike runner (`testsuite/CoinTest.h`, macros like
`BOOST_AUTO_TEST_CASE`, `BOOST_CHECK_MESSAGE`).

Run the full suite:
```
ctest --test-dir build --output-on-failure
```
Run a single test binary directly (supports Boost.Test-style CLI filters):
```
./build/testsuite/CoinTests --run_test=<suite/case name>
```
To debug a crash, run `CoinTests` directly under a debugger.

**Writing tests**: test code lives inline in the `.cpp` file of the component
being tested, wrapped as:
```cpp
#ifdef COIN_TEST_SUITE
BOOST_AUTO_TEST_CASE(name)
{
  // test code
}
#endif // COIN_TEST_SUITE
```
The first `#include` in a file containing test blocks must be the header
declaring the component under test. Only public API can be exercised, since
the testsuite is built separately and linked against Coin. **When a `.cpp`
file gets its first test case, CMake must be reconfigured** so the
extractor (`testsuite/CMakeLists.txt`) discovers it and regenerates the
generated test source file.

## Git / PR workflow

**Never `git push` to the `fork` remote, never run `gh pr create`/`gh pr edit`/`gh pr ready`, and never force-push a branch backing an open PR, unless the user explicitly asks for that specific action in that message.** A prior broad approval ("submit all of them") does not carry forward to later, individual pushes/edits — including when fixing your own mistake in an already-open PR. Committing locally, building, and running the full verification suite (GCC + clang + `ctest` + Debug ASan/UBSan) needs no such permission; only the step that publishes to GitHub does. When a fix is ready, stop and present it, and ask before publishing.

### Required verification before opening or updating a PR

Complete these checks on the final diff before presenting a change as ready
to publish. A successful build or test run alone does not establish that a
migration is complete or that all affected behavior has been verified.

- **Documentation consistency:** search for the affected API names, old
  idioms, and behavioral claims across headers, source comments, Doxygen,
  examples, tests, and documentation. Update instructions that contradict
  the change, especially examples that still recommend the bug being fixed.
- **Code excluded from the build:** inspect matching code behind `#if 0`,
  debug guards, platform guards, and optional-feature flags. Do not dismiss
  an obsolete or unsafe pattern solely because the current build excludes
  it. Fix occurrences within scope; explicitly record remaining occurrences
  and why they are outside scope. Compile or exercise affected configurations
  when available, and identify those that could only be inspected statically.
- **Regression coverage:** exercise the behavior that distinguishes the fix
  from the old implementation, including relevant edge cases. For bug fixes,
  demonstrate that the regression test fails before the fix and passes after
  it when feasible. Expected results must come from the behavior contract or
  independently known values, not from another accessor delegating to the
  same implementation or from the invalid operation being removed. Add
  portable regression coverage to `CoinTests`; standalone reproducers should
  document their invocation and any requirements preventing routine execution.
- **Sanitizer coverage:** identify which library and test binaries were
  instrumented, the sanitizer flags used, and which affected paths actually
  ran. Instrument the affected library code as well as its driver when
  checking library behavior. A sanitized driver linked to an uninstrumented
  library does not validate the library's internal operations. Skipped tests,
  unavailable rendering contexts, disabled features, and unexecuted paths
  must be reported as validation gaps, not as passing coverage. Do not claim
  that a change is free of undefined behavior merely because sanitizers were
  silent on the executed paths.
- **Final evidence:** review the final diff and repeat the search for the old
  pattern after corrections. In the PR test plan, distinguish completed
  checks, static inspection, skips, and unverified configurations. For any
  inapplicable check, state why. Resolve in-scope findings before publishing;
  carry remaining validation limitations into the review summary.

## Architecture

The source tree under `src/` (mirrored 1:1 by public headers under
`include/Inventor/`) is organized by Open Inventor's core concept families
rather than by feature:

- `nodes/` — scene graph node classes (`SoNode` subclasses): shapes, groups,
  cameras, lights, etc. Nodes are traversed by actions.
- `actions/` — traversal visitors (`SoAction` subclasses: render, pick,
  bounding-box, handle-event, search, write, ...) applied to a scene graph.
- `elements/` — per-traversal state (`SoElement` subclasses) pushed/popped
  on the action's state stack as nodes are traversed (current transform,
  material, lights, etc.).
- `fields/` — typed, notifying data containers (`SoField`/`SoSField`/
  `SoMField` subclasses) attached to nodes/engines; changes propagate via
  the notification/sensor mechanism to trigger redraws.
- `engines/` — data-flow computation nodes (`SoEngine` subclasses) that
  connect field values together, including an embedded expression
  evaluator (Lex/Yacc grammar in `engines/evaluator.l`/`.y`) for
  `SoCalculator` and a small state-machine dialect under `scxml/`.
- `sensors/` — callbacks scheduled on field/node changes or timers, driving
  the delayed-redraw / notification queue.
- `draggers/`, `manips/` — interactive 3D widgets built from nodekits.
- `nodekits/` — composite, pre-assembled node graphs exposed as a single
  node ("kit") with named parts.
- `details/` — per-primitive pick/intersection detail objects returned
  alongside pick actions.
- `caches/` — render/bbox/primitive caching used during action traversal.
- `io/`, `foreignfiles/`, `vrml97/`, `3ds/` — file I/O, format import/export
  (Inventor `.iv`, VRML97, 3DS, etc.).
- `base/` — foundation types (`Sb*` classes: strings, containers, math
  primitives) with no scene-graph dependencies.
- `glue/` — thin runtime-loaded wrappers isolating platform/optional-library
  dependencies (OpenGL context bindings for GLX/WGL/AGL/CGL/EGL, plus
  optional simage, Cg, FreeType, zlib, bzip2, OpenAL, SpiderMonkey). Coin
  loads these dynamically at runtime rather than link-time where possible,
  so the library degrades gracefully when an optional dependency is absent.
- `misc/`, `threads/`, `errors/`, `profiler/` — cross-cutting infrastructure
  (the `SoDB`/`SoType` runtime, threading abstraction, error/debug
  reporting, profiling instrumentation).

Cross-cutting mechanism to keep in mind when changing node/field/engine
behavior: field value changes call `SoField::notify()`, which walks the
notification chain to dependent fields/engines/sensors, ultimately
scheduling a redraw via the sensor manager — this indirection is why a
field mutation is not synchronous with rendering.

Most public classes follow a `SO_NODE_HEADER`/`SO_NODE_SOURCE` (and
analogous `SO_ACTION_*`, `SO_ELEMENT_*`, `SO_ENGINE_*`, `SO_FIELD_*`) macro
pair for type registration, field initialization, and RTTI boilerplate —
look at a sibling class in the same directory before adding a new one, as
the macro-driven skeleton is largely copy-paste across a family.
