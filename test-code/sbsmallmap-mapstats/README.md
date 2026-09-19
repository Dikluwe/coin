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
