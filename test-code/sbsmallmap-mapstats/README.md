# SbSmallMap candidate instrumentation

This directory is laboratory material. It is not intended for a Coin pull
request. The instrumentation in `SbMapStats` is enabled with
`-DCOIN_MAP_STATS_ENABLED` and writes its report to `COIN_MAP_STATS_FILE`.

The drivers cover the candidate groups that an ordinary single-view FreeCAD
session does not necessarily activate:

- `shader_multicontext.cpp`: GLSL program, shader-object and uniform-parameter
  maps in two real GLX contexts;
- `bump_multicontext.cpp`: the diffuse and specular legacy bump-program maps in
  two real GLX contexts;
- `bump_contexts_benchmark.cpp`: compares the final lazy `SbHash` with
  `SbSmallMap` using the corrected bump renderer's two-context lifecycle;
- `freecad_bump_smoke.py`: loads an explicitly selected lab `libCoin` in
  FreeCAD, renders A -> B -> A and rejects screenshots without visible scene
geometry;
- `profiler_textures.cpp`: all six static profiler texture categories, twice;
- `profiler_textures_benchmark.cpp`: compares the six-entry cache lifecycle
  and steady lookup cost against the current `std::map` representation;
- `vbo_offscreen.cpp`: an earlier offscreen probe retained for provenance; the
  scene does not engage the VBO path on this machine.

## Results recorded on 2026-09-18

An instrumented FreeCAD workload exercised 3,933 `SoVBO` owners and recorded
561,402 lookups. Every observed `vbohash` had at most one entry and only one
cache context was seen. This is the strongest GL candidate for `SbSmallMap`.

The two-context GLSL driver produced maximum cardinality two in all three maps:

| Map | gets | puts | max size | contexts |
|---|---:|---:|---:|---:|
| `SoGLSLShaderProgram::programHandles` | 13 | 2 | 2 | 2 |
| `SoShaderObjectP::glshaderobjects` | 60 | 4 | 2 | 2 |
| `SoUniformShaderParameterP::glparams` | 4 | 2 | 2 | 2 |

The migrated `codex/maps/use/gl-contexts-small` branch was then tested against
the same driver using two direct GLX contexts. Three passes in each context and
a final return to the first context completed successfully. A FreeCAD safe-mode
smoke test loaded the migrated standalone `libCoin`, created three Part shapes,
cycled through seven camera views and exited with
`SBSMALLMAP_FREECAD_GL_OK`. The complete Release CTest suite passed 8/8.

The profiler texture dictionary reached its fixed cardinality of six, with 18
lookups and six insertions. The relevant comparison is with `std::map`, not
`SbHash`. A Release benchmark reproducing six insertions, 18 lookups, one full
iteration and cleanup produced these medians over five complete runs:

| Container | Lifecycle | Steady lookup | Allocations per lifecycle |
|---|---:|---:|---:|
| `SbSmallMap` | 73.87 ns | 2.49 ns | 1 |
| `std::map` | 188.43 ns | 2.73 ns | 6 |

Four of the five lifecycle runs were between 71.50 and 75.43 ns for
`SbSmallMap`; one scheduler outlier measured 150.58 ns. The allocation count is
deterministic. The benchmark therefore supports the migration primarily by
reducing six tree-node allocations to one contiguous spill allocation; lookup
performance at six entries remains approximately tied.

The bump driver found a design issue that must be handled before considering a
container migration:

- `diffuseprogramdict` is behind a path forcibly disabled with
  `use_vertex_program = FALSE`, and recorded no operation;
- `specularprogramdict` recorded only the first context (one get and one put),
  even though the same shape was rendered in two contexts. The per-shape
  `programsinitialized` flag prevents `initPrograms()` from consulting the map
  for later contexts.

Consequently, the bump branch is not a valid performance-only migration. Its
context lifecycle needs a separate correctness study first.

## Corrected bump results recorded on 2026-09-19

The dedicated `lab/gl-bump-smallmap-evaluation` branch combines, without
changing any pull-request branch:

- the context and program lifecycle correction from PR #759;
- the `SbList`/`SbSmallMap` dependency stack;
- the six final `SbHash` fixes, including lazy storage;
- the lab-only `SbMapStats` instrumentation.

The lifecycle reproducer first ran with both bump registries migrated to
`SbSmallMap`. It then ran again at `a77ad21caa` with the dormant diffuse
registry restored to the final lazy `SbHash` and only the active specular
registry using `SbSmallMap`. Both variants produced the same functional and
instrumentation result:

| Map | gets | puts | erases | max size | distinct contexts |
|---|---:|---:|---:|---:|---:|
| `diffuseprogramdict` | 1 | 0 | 0 | 0 | 0 |
| `specularprogramdict` | 8 | 2 | 1 | 2 | 2 |

The sole diffuse lookup came from context cleanup. The disabled render path
never inserted an entry. The specular map created independent program sets for
contexts A and B, then reused A on the final pass.

### Validation matrix

| Validation | Result |
|---|---|
| fresh instrumented Release build | 1069/1069 targets |
| final-stack incremental Release rebuild | 59/59 targets |
| Release CTest | 8/8 |
| GLX A -> B -> A | A=1/2/3, B=4/5/6, A again=1/2/3; all programs non-empty |
| context-first cleanup | A programs 1/2/3 deleted; B remained valid |
| owner-first cleanup | B programs 4/5/6 deleted after the scheduled queue drained |
| ASan + UBSan, final selective variant | zero findings; same IDs and counters |
| legacy private ABI shims | both `init*` functions and the old diffuse `SbHash` destructor remain exported |

The FreeCAD 26.3 GUI smoke loaded
`/tmp/coin-gl-bump-smallmap-lab-build/lib/libCoin.so.80.0.10`, verified through
`/proc/self/maps`, and completed A -> B -> A without a crash. Its three 640x480
captures from the versioned script contained 54,964, 62,802 and 55,440
foreground pixels. The two onscreen viewers reported the same logical cache
ID, while `saveImage()` used
additional offscreen contexts; instrumentation consequently observed four
specular insertions and four distinct context IDs. Diffuse again stayed empty.
The screenshots establish visible integration, not pixel equivalence: the
focused GLX reproducer remains the deterministic correctness oracle.

### Bump-specific benchmark

`bump_contexts_benchmark.cpp` models the observed lifecycle with a three-GLuint
record: two insertions, seven lookups, one erase, iteration of the survivor and
clear. It compares the final lazy `SbHash`, not the old eager implementation.
The probe is laboratory material and must not be included in a production PR.

On GCC 13.3 (`-O3 -DNDEBUG`, Ryzen 7 5800H, CPU 2, warm-up discarded and the
median of 11 samples), a recorded run produced:

| Case | Median |
|---|---:|
| cold owner, hash + hash | 620.00 ns/cycle |
| cold owner, hash + small (specular only) | 30.19 ns/cycle |
| cold owner, small + small | 25.30 ns/cycle |
| warm active map, `SbHash` | 333.42 ns/cycle |
| warm active map, `SbSmallMap` | 14.50 ns/cycle |
| hit, one context: hash / small | 1.56 / 0.79 ns |
| hit, two contexts: hash / small | 1.57 / 1.18 ns |

Absolute nanoseconds varied between repeated runs, but the ordering and memory
measurements were stable. Linux/glibc allocation interposition measured:

| Map/configuration | Inline | Heap after specular use | Total |
|---|---:|---:|---:|
| lazy `SbHash` map | 32 B | 4176 B | 4208 B |
| `SbSmallMap` map | 80 B | 0 B | 80 B |
| diffuse hash + specular hash | 64 B | 4176 B | 4240 B |
| diffuse hash + specular small | 112 B | 0 B | 112 B |
| diffuse small + specular small | 160 B | 0 B | 160 B |

The hash retains its bucket array and first entry slab after `clear()` and
releases them in the destructor. `SbSmallMap` keeps both observed context
records inline.

### Decision

Only `specularprogramdict` should move to `SbSmallMap` in a future production
change. This keeps the complete active-cache benefit, removes approximately
4.1 KiB of heap from each used bump renderer in the measured configuration,
and uses 48 bytes less inline storage per owner than migrating the disabled
diffuse registry. Diffuse should remain on lazy `SbHash` until a real workload
demonstrates an insertion; if that path is re-enabled, the same lab probe can
be repeated.

### Reproducing the lab-only probes

With this worktree at `/tmp/coin-gl-bump-smallmap-lab` and its Release build at
`/tmp/coin-gl-bump-smallmap-lab-build`, build and run the timing probe with:

```sh
g++ -std=c++11 -O3 -DNDEBUG \
  -I/tmp/coin-gl-bump-smallmap-lab-build/include \
  -I/tmp/coin-gl-bump-smallmap-lab/include \
  -I/tmp/coin-gl-bump-smallmap-lab/src \
  test-code/sbsmallmap-mapstats/bump_contexts_benchmark.cpp \
  -L/tmp/coin-gl-bump-smallmap-lab-build/lib \
  -Wl,-rpath,/tmp/coin-gl-bump-smallmap-lab-build/lib -lCoin \
  -o /tmp/bump_contexts_benchmark
taskset -c 2 /tmp/bump_contexts_benchmark 11 300000 1000000 12000000
```

The Linux/glibc-only allocation mode uses the same command with
`-DTRACK_ALLOC -Wl,--export-dynamic` and a different output filename.

The GLX lifecycle reproducer needs a software X server:

```sh
Xvfb :97 -screen 0 1024x768x24 +extension GLX +render -noreset
env DISPLAY=:97 LIBGL_ALWAYS_SOFTWARE=1 \
  COIN_MAP_STATS_FILE=/tmp/coin-bump-smallmap-stats.txt \
  bash testsuite/reproducers/soshape-bumprender-context-lifecycle/run.sh \
  /tmp/coin-gl-bump-smallmap-lab-build/lib
```

The FreeCAD smoke uses a separate Xvfb display and FreeCAD's GUI test runner:

```sh
env DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 PYTHONNOUSERSITE=1 \
  LD_PRELOAD=/tmp/coin-gl-bump-smallmap-lab-build/lib/libCoin.so.80.0.10 \
  LD_LIBRARY_PATH=/tmp/coin-gl-bump-smallmap-lab-build/lib \
  COIN_LAB_LIBRARY=/tmp/coin-gl-bump-smallmap-lab-build/lib/libCoin.so.80.0.10 \
  COIN_LAB_OUTPUT_DIR=/tmp/coin-freecad-bump-visible-output \
  COIN_MAP_STATS_FILE=/tmp/coin-bump-smallmap-freecad-stats.txt \
  /mnt/Laranja/Git/externos/freecad-build/bin/FreeCAD --safe-mode \
  --python-path /tmp/coin-gl-bump-smallmap-lab/test-code/sbsmallmap-mapstats \
  --run-test freecad_bump_smoke.CoinBumpSmallMapSmoke.test_two_view_a_b_a_bump_render
```

`Xvfb :99` must be running with the same options as the `:97` server above.
