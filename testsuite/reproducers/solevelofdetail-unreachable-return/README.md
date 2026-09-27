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

## Historical origin of the unreachable return

### Precursor: SoSeparator, February 13, 2001

Commit [`3203d285d3e462649b258cb2222cf2fd5a7f138e`](https://github.com/coin3d/coin/commit/3203d285d3e462649b258cb2222cf2fd5a7f138e),
by Peder Blekken, has the message "Fix for cache to cache dependencies."
It introduced this switch in `SoSeparator::getBoundingBox()`:

```cpp
case SoAction::IN_PATH:
  iscaching = FALSE;
  break;
case SoAction::OFF_PATH:
  return; // no need to do any more work
```

In that implementation the return was reachable and applied only to the
separate `OFF_PATH` case. This exact structure and comment were still present
in `SoSeparator.cpp` at the parent of the LOD change.

### Introduction in SoLevelOfDetail, July 3, 2002

Commit [`3b8ca9b8538839bad6ba2f08efa992cc23ca5d01`](https://github.com/coin3d/coin/commit/3b8ca9b8538839bad6ba2f08efa992cc23ca5d01),
authored and committed by Peder Blekken at 14:55:36 UTC, states:
"Improved/bugfixed bounding box calculations for this node. Problem reported
by Kristofer Tingdahl."

Before this commit, LOD did not override `getBoundingBox()` and inherited the
SoGroup implementation. Its detail-selection code applied the bbox action
directly to `this`. The change introduced the LOD's bbox cache, its own
`getBoundingBox()` implementation, cache invalidation in `notify()`, and
path-based bbox calculation with a reset path to retain upstream state while
obtaining local geometry bounds.

The new implementation already contained:

```cpp
case SoAction::OFF_PATH:
case SoAction::IN_PATH:
  // can't cache if we're not traversing all children
  iscaching = FALSE;
  break;
  return; // no need to do any more work
```

Thus the return was unreachable from its first appearance in LOD; it did not
become unreachable through a later insertion of `break`.

The strong inference from the matching function structure, exact return
comment, cache protocol, and the shared 1999 FIXME is that the implementation
was adapted from SoSeparator, with the separate OFF_PATH case moved alongside
IN_PATH while the old return was left behind. The commit message does not
explicitly describe copying or explain why the return was retained, so the
editing mistake is an inference rather than a documented author statement.

The issue discussion attributes the introduction to Kristofer Tingdahl.
Git records Peder Blekken as the author; Tingdahl is named as the reporter.

### Follow-up on the same day

At 15:25:01 UTC, commit
[`fe0244a308af12ffd895d11503a71a6d810bfaaa`](https://github.com/coin3d/coin/commit/fe0244a308af12ffd895d11503a71a6d810bfaaa),
also by Peder Blekken, says "Bounding box bugfix." It preserves/restores the
accumulated action box, always pushes/pops state during child traversal, and
sets the temporary box transform to identity for local cache storage.
It leaves the unreachable return untouched.

A Git pickaxe search over all local refs identifies the introduction above
and the removal in `fed98ccbe9` on September 27, 2026. The exact dead statement
therefore survived from its introduction for over 24 years. The history
supports removing the leftover statement; it does not establish an intention
to add an early return to the LOD traversal. The IN_PATH and OFF_PATH mutation
tests below provide behavioral evidence against that alternative.

Reproduce the historical inspection:

```sh
git log --all -S 'return; // no need to do any more work' -- src/nodes/SoLevelOfDetail.cpp
git show 3203d285d3 -- src/nodes/SoSeparator.cpp
git show 3b8ca9b85388 -- src/nodes/SoLevelOfDetail.cpp
git show 3b8ca9b85388^:src/nodes/SoSeparator.cpp
git show fe0244a308 -- src/nodes/SoLevelOfDetail.cpp
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
