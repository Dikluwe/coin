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
`SbHash`; the separate local benchmark favored `SbSmallMap` for this lifecycle.

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
