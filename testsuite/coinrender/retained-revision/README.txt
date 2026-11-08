Strict retained-revision validation (Linux operational harness)

Run from this source directory, using a dedicated output directory:
  bash build-run.sh bgfx REPO EVIDENCE_ROOT
  bash build-run.sh wgpu REPO EVIDENCE_ROOT
  bash build-run.sh recording REPO EVIDENCE_ROOT
  python3 compare.py EVIDENCE_ROOT --backends recording bgfx wgpu --pipeline3

The default comparison requires both GPU backends; the explicit backend list is
recorded in its output. Missing/extra files, failed phases, missing/skipped CTest
controls, incomplete summaries, non-finite/truncated data, image/semantic/depth
mismatches and undetected negative controls all return a nonzero status.
expected-files.json fixes the inventory of the eight-case audit, not tolerances.
Color edge selection and the depth quantization bound are unchanged.

The build runner stores configure/build/audit/controls/run statuses, CTest JUnit,
logs, binaries and build-state. It continues collecting execution evidence after
an audit/CTest/pipeline failure, then returns failure. Cache-copy failure also
fails the command. Stale outputs are removed before a new run. BGFX includes a
second audit with readback pipeline depth 3. GPU suites are serialized with flock.
The runner does not substitute a renderer when Vulkan is unavailable.

Use frozen copies of this directory for a run; never edit a running shell script.
Each configure/build/run happens inside the same command with a private /dev/shm
build and temporary/driver caches. Optional environment overrides:
  COIN_BGFX_PREFIX: installed bgfx CMake package (default is this lab's prefix).
  COIN_RETAINED_BUILD_ROOT: build/cache directory; seeded CMake paths must match.
  COIN_RETAINED_SEED_ROOT: prior backend build-state root (absent means clean).
Rust dependency acquisition is offline. Dependency availability is a requirement,
not grounds for counting a skip as qualification.

Test the validator using copies of a complete evidence package:
  python3 test_validation.py BASELINE_EVIDENCE_ROOT
These tests copy real buffers and synthesize phase/JUnit receipts to exercise the
validator; PATH stubs test shell exit aggregation. They do not execute a GPU or
qualify renderer behavior. Original evidence is never changed. The negative
controls include corrupted color/depth/semantics, missing backend/control/files,
truncated files, a failed negative control, pipeline corruption and individual
runner phase failures.
