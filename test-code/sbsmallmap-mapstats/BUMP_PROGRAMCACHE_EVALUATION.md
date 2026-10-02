# Bump ProgramCache evaluation on the current integration base

This lab descends from `lab/map-instrumentation`, which descends from
`lab/open-prs-integration`. It keeps the historical bump benchmark in
`legacy/` and the GLX lifecycle reproducer in
`testsuite/reproducers/soshape-bumprender-context-lifecycle/`.
The inherited `bump_multicontext.cpp` and `SbMapStats` collector measure
the current cache.

The old lab evaluated replacing `specularprogramdict`, an `SbHash` of
trivial program IDs, with `SbSmallMap`. The integrated
`soshape_bumprender::ProgramCache` has one `std::map<uint32_t, Context>`
for both program families. Its `Context` owns nontrivial error state and
tracks EMPTY, PENDING, READY and FAILED separately for diffuse and specular
programs. `SbSmallMap` requires trivial values, so the earlier container
substitution is not valid on this base. The historical timing and memory
figures in `legacy/bump_contexts_benchmark.cpp` apply to the old data model.

The current cache also relies on ordered `upper_bound` traversal during
destruction, stable entries while callbacks are pending, and allocation
failure handling around insertion. A candidate compact map must preserve
those behaviors before a benchmark result can justify a code change.

## Current validation

Build this lab with `COIN_BUILD_TESTS=ON` and run CTest. The
`BumpProgramFailuresAndLifetime` test exercises allocation failure,
reentrancy, deferred callbacks and destruction. For a real GLX context, run:

```sh
testsuite/reproducers/soshape-bumprender-context-lifecycle/run.sh /absolute/path/to/build/lib
```

The GLX reproducer exits 2 if no suitable context is available. The
`bump_multicontext.cpp` probe and enabled `SbMapStats` build can then
measure the number of contexts reached by the current cache. No container
change is proposed by this lab yet.
