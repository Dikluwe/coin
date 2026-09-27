# SoLevelOfDetail bounding-box traversal (issue #374)

Issue: https://github.com/coin3d/coin/issues/374

`SoLevelOfDetail::getBoundingBox()` contained a `return` immediately after
`break` in the shared `OFF_PATH` / `IN_PATH` case. The issue discussion asked
for a test to clarify whether these cases should return early. The regression
checks are in [`SoLevelOfDetailBoundingBoxTest.cpp`](../../SoLevelOfDetailBoundingBoxTest.cpp)
and run through CTest as `LevelOfDetailBoundingBox`.

## Cases and expected behavior

- `NO_PATH`: calculate the union of all children's geometry, cache it, reuse
  it on repetition, and rebuild it after a cube's dimensions change.
- `BELOW_PATH`: when a path ends at the LOD, include all children and reuse
  the complete cache.
- `IN_PATH`: when a path continues to one geometry branch, compute that
  branch's box, even with a valid complete cache. Repeated partial traversal
  must execute again; it must not create or overwrite the complete cache.
- `OFF_PATH`: visit the state-affecting callback in the earlier sibling LOD,
  without including that LOD's geometry in the target sibling's bounding box.
  Check with and without an existing complete cache, and verify that the
  complete cache remains usable afterward.
- Camera-space traversal: bypass an existing cache, recompute on every
  traversal, and preserve the complete cache for normal traversal afterward.

The scene contains two translated cubes with independently known extents:
`[-6,-2,-3]..[-4,2,3]` and `[7,-1,-1]..[13,1,1]`. Their complete union is
`[-6,-2,-3]..[13,2,3]`; the reported center is `[2.5,0,0]`. A callback counts
actual child traversal. Child separator caches are disabled so they cannot
mask LOD cache behavior. No GL context or rendering backend is required.

## Run

```sh
cmake -S . -B build-issue374 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCOIN_BUILD_TESTS=ON
cmake --build build-issue374 --target CoinLevelOfDetailBoundingBoxTest
ctest --test-dir build-issue374 -R '^LevelOfDetailBoundingBox$' -V
```

## Study-based additions

The personal `Estudo coin` notes `estudos/So/SoCache.md`,
`estudos/So/SoGraphCore.md`, and `estudos/API/Build-CI-Tests-Distribution.md`
identify local bounding-box storage, DAG sharing, notification-driven cache
invalidation, and isolated CTest reproducers as contracts worth verifying.
The dedicated executable now also covers:

- a shared LOD under two independently translated parents, checking that a
  local cached box is transformed for each occurrence and is still local
  when the LOD is subsequently visited directly;
- removing, adding, and replacing children after the complete cache is warm,
  checking new extents and centers as well as reuse on the second traversal;
- empty-to-populated-to-empty transitions, including the absence of a center
  for an empty bounding box.

Parent separators in the sharing case have bounding-box caching disabled;
this isolates traversal state without allowing a parent cache to hide an LOD
cache error. The CTest entry has `scene`, `unit`, and `process-isolated` labels
and a 30-second timeout.

## Recorded validation

Validated on Linux with GCC 13.3.0 in Debug configuration for branch
`codex/issue-374-bbox-tests`:

| Source variant | Result |
| --- | --- |
| Corrected code with unreachable `return` removed | 8 tests / 261 checks passed |
| Deliberate mutation: reachable `return` before `break` | 2 of 8 tests failed (`IN_PATH` and `OFF_PATH`) |
| Corrected code restored after mutation | 8 tests / 261 checks passed |

The mutation causes the `IN_PATH` bounding box and center checks to fail and
skips the observable callback traversal in `OFF_PATH`. It confirms that the
tests distinguish continuing traversal from returning early. The mutation is
not part of the committed source. Removing the unreachable statement itself
has no runtime effect; these tests protect the intended traversal semantics.
