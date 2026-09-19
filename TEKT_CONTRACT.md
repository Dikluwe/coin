# Integration lab contracts

# Tekt contract: `SoPath` child append validation

## Frozen inputs and authority

- Baseline: `40c6372d2984a94c6ac17f059caee7b8d6374654`.
- Branch: `fix/sopath-child-append-validation`.
- L0 intent (frozen): in every build, `SoPath::append(int)` and
  `SoPath::push(int)` reject, without mutation, an empty path, a tail without
  a child list, a negative index, and an index greater than or equal to the
  number of children. `SoPath::append(SoNode *)` rejects a tail without
  children and a node which is not a child. `SoPath::append(SoPath *)` rejects
  a tail without children. Diagnostics may continue only under `COIN_DEBUG`.
  Valid operations and hidden-child behaviour are preserved.
- Role: contract author and independent A/B test author only. The allowed
  inputs were this L0 intent, the frozen baseline's public headers, and its
  test/build conventions. Candidate implementation and other worktrees/labs
  are forbidden inputs. Writable outputs are this contract and
  `testsuite/reproducers/sopath-child-append-validation/` only.
- Protocol regime: full segregated-materialization protocol, limited to the
  pre-implementation contract/oracle phase assigned here.

This document and its test are intentionally implementation-blind. They do
not prescribe a validation mechanism, diagnostic text, or internal data
structure.

## Observable contract

For this contract, **reject without mutation** means all of the following:

1. The call returns normally (no crash, abort, exception escaping the API, or
   non-termination).
2. The path remains equal to a copy taken immediately before the call.
3. Its public length, head, tail, visible nodes, and visible child indices are
   unchanged.
4. A rejected call does not prevent normal destruction/cleanup of the path
   and scene graph.

Diagnostics are deliberately not an oracle. A `COIN_DEBUG` build may emit a
diagnostic for a rejection. A non-`COIN_DEBUG` build must not depend on a
diagnostic to reject safely. Neither the presence nor exact wording of a
diagnostic is required.

The overload preconditions and outcomes are:

| Operation | Input class | Required result |
| --- | --- | --- |
| `append(int)`, `push(int)` | empty path | reject without mutation |
| `append(int)`, `push(int)` | tail has no child list | reject without mutation |
| `append(int)`, `push(int)` | index `< 0` | reject without mutation |
| `append(int)`, `push(int)` | index `>= child_count` (including zero children) | reject without mutation |
| `append(SoNode *)` | tail has no child list, or has zero children | reject without mutation |
| `append(SoNode *)` | node is not an immediate child of the full tail | reject without mutation |
| `append(SoPath *)` | tail has no child list, or has zero children | reject without mutation |
| all three overloads and `push` | valid immediate-child relation | preserve existing successful append semantics |

“Tail” for validation means the full stored tail, including a tail hidden by
the public `SoPath` view. This is necessary to preserve existing nodekit
hidden-child paths: a valid append below a hidden node must still succeed, and
an invalid append below it must remain non-mutating. The public hidden view
(visible length and visible tail) must not be exposed or otherwise changed by
the validation.

The `append(SoPath *)` positive case in the oracle uses a suffix whose head is
an immediate child of the receiver's full tail; the remainder of that suffix
is valid. The contract does not broaden the scope to unrelated malformed
suffixes or null arguments.

## Independent oracle

The standalone executable in
`testsuite/reproducers/sopath-child-append-validation/repro.cpp` exposes one
named case per process. `run.sh` compiles it against a selected Coin build and
runs every case independently, so one invalid-input crash cannot hide later
results.

Negative cases cover both `append(int)` and its `push(int)` alias, leaf tails,
zero-child group tails, both index boundaries, non-child nodes, and the path
overload. Positive controls cover every overload and `push(int)`. A nodekit
case checks successful continuation below a hidden child as well as rejection
without mutation below that same hidden tail.

Expected discrimination:

- A candidate which only adds debug assertions is rejected by Release runs.
- A candidate with an off-by-one upper bound is rejected by the
  `index == child_count` cases.
- A candidate which omits negative-index validation is rejected separately.
- A candidate which validates the visible tail instead of the full hidden
  tail is rejected by the nodekit case.
- A candidate which rejects all appends is rejected by the positive controls.
- A candidate which mutates before reporting an error is rejected by the
  before/after snapshots.

## `Unknown` policy and limits

`Unknown` is explicit and never counts as `Preserved` or as a passing test.
It is used when an observation cannot be built or run against the requested
configuration, when identity/equality is ambiguous, or when an opaque
construction cannot expose the required state through public API. A crash is
`Violated`, not `Unknown`. A compile/link/setup failure is `Unknown` for that
configuration and prevents a conclusive verdict.

The suite does not assert diagnostic wording/count, thread-safety, null-node
or null-path handling, malformed suffixes beyond the stated tail condition,
ABI compatibility, or functional equivalence outside this append fragment.
The nodekit case observes hidden-state mutation through path equality and the
unchanged public hidden view; if a platform's public equality cannot observe
the full stored path, that sub-observation is `Unknown` and must be reported.

## Gates and sealing policy

Before sealing, run the suite against at least one Release and one Debug
build. Each named case must pass in both. Re-run in a different case order; the
classification must be identical. The intended mutation score is `1.0`: all
listed semantic mutants must be rejected while every positive control passes.
Any surviving mutant, `Unknown`, or changed frozen input prevents sealing.

This author does not issue the final implementation verdict or certificate.
Those belong to an independent verifier after candidate integration.
---

# Tekt A/B contract: legacy `SoPath` accessor bounds

## Frozen inputs and authority

- Regime: segregated A/B contract and independent tests.
- L0 intent: `getNode()` and `getNodeFromTail()` return `NULL`, while
  `getIndex()` and `getIndexFromTail()` return `-1`, for negative indices,
  the relevant limit, values above that limit, and empty paths, in Debug and
  Release, without mutation. Valid indices and the visible/full distinction
  remain unchanged. Diagnostics may remain in `COIN_DEBUG` only.
- Baseline: commit `40c6372d2984a94c6ac17f059caee7b8d6374654` on branch
  `fix/sopath-legacy-accessor-bounds`.
- Environment: `/tmp/coin-sopath-accessors`; no candidate implementation,
  primary worktree, or implementation lab was an input.
- Authority: the executor may write this contract and files below
  `testsuite/reproducers/sopath-legacy-accessor-bounds/`, plus test-only CMake
  registration. The executor may not write production code or issue the final
  implementation verdict.

This document is the canonical rendering of the supplied L0 intent for this
A/B artifact. The original chat transport has no stable repository path or
byte representation, so it has no independently reproducible file hash.

## Observable contract

Let `F` be the full path length and `V` the visible `SoPath` length.

| Accessor called through `SoPath` | Valid domain | Invalid result |
|---|---:|---:|
| `getNode(i)` | `0 <= i < F` | `NULL` |
| `getIndex(i)` | `0 <= i < F` | `-1` |
| `getNodeFromTail(i)` | `0 <= i < V` | `NULL` |
| `getIndexFromTail(i)` | `0 <= i < V` | `-1` |

The invalid domain includes every negative integer and every integer greater
than or equal to the relevant limit. When the path is empty, `F == V == 0`,
so every integer is invalid.

For a valid direct index `i`, the returned node and child index remain those
at full-path position `i`. For a valid tail index `i`, the returned node and
child index remain those at visible position `V - i - 1`. The head child index
remains the baseline value `0`; `-1` remains reserved here for an
invalid-index sentinel.

The visible/full distinction is preserved: a path with a hidden nodekit part
may have `V < F`; direct access through `SoPath` still observes the full
position, tail access through `SoPath` observes only the visible suffix, and
valid access through `SoFullPath` continues to observe the full suffix.
`SoNodeKitPath`'s private index accessors and bounds behavior of derived-class
overloads are outside this change; their valid full-path behavior is only a
positive control here.

An invalid accessor call must not alter either length, the sequence of valid
node pointers, or the sequence of valid child indices. It must return normally
with its sentinel. A crash, assertion termination, sanitizer finding, thrown
exception, or non-sentinel value is a violation, not an alternate diagnostic.

Diagnostics for these invalid calls are optional when the library was built
with `COIN_DEBUG`. They must not be emitted by these calls when `COIN_DEBUG` is
false. Diagnostics never replace the required return value.

## Independent A/B oracle

`testsuite/reproducers/sopath-legacy-accessor-bounds/repro.cpp` derives its
expectations only from the table above. CTest starts a fresh process for each
accessor/boundary pair so that one unsafe baseline access cannot mask another.
The boundary representatives are `-1`, `INT_MIN`, the relevant limit,
limit + 1, and `INT_MAX`; index zero is exercised on an empty path. Positive
controls cover every valid position, the baseline head child index, the
visible/full distinction, and post-call immutability.

Required configurations are independently configured and executed Debug and
Release builds. Passing only one configuration is insufficient.

## Classification and `Unknown`

- `Preserved`: every registered case exits zero in both Debug and Release,
  with the frozen test artifacts unchanged.
- `Violated`: any case observes a wrong valid value, wrong invalid sentinel,
  mutation, Release diagnostic, crash, signal, assertion termination,
  sanitizer finding, or nonzero exit caused by the exercised behavior.
- `Unknown`: a required configuration was not actually built; tests were not
  discovered; the executable could not start because of an environmental or
  dependency failure; a timeout occurred before the relevant observation; or
  the baseline/contract/test hashes do not match the frozen inputs.

`Unknown` is never success and must not be converted to `Preserved`. An
ordinary crash while evaluating an invalid accessor is classifiable as
`Violated`, not `Unknown`.

## Reproduction gate

For each of Debug and Release, configure and build the repository normally,
then run:

```sh
ctest --test-dir <build-dir> --output-on-failure \
  -L sopath-legacy-accessor-contract
```

The final verifier must record the generator, compiler, configuration flags,
test output, baseline and implementation commits, and SHA-256 hashes of this
contract, the reproducer, and the test registration. This A/B artifact covers
only the observable fragment stated above; it does not attest general
functional equivalence or implementation quality.
