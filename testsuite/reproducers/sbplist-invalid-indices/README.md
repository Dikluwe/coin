# SbPList invalid-index sanitizer matrix

This local study reproducer deliberately violates the integer-index
preconditions of the public `SbPList` API. It is separate from `CoinTests`
because ASan or UBSan terminates the process after most of these operations.

Configure Coin so both the library and the reproducer are instrumented. For
example:

```sh
cmake -S . -B /dev/shm/coin-build-sbplist-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /dev/shm/coin-build-sbplist-asan --parallel
CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
LDFLAGS='-fsanitize=address,undefined' \
  testsuite/reproducers/sbplist-invalid-indices/run.sh \
  /dev/shm/coin-build-sbplist-asan/lib
```

A passing matrix means every invalid operation was rejected by an exception or
assertion, or diagnosed by a sanitizer, and that an injected allocation failure
left the list reusable. `SbPList` now rejects invalid logical indices with
`std::out_of_range` in normal builds as well as debug builds. The expanding
`operator[]` retains its historical behavior for non-negative indices.

The allocation tests override array allocation in the reproducer process and
throw `std::bad_alloc` at deterministic allocation boundaries. They verify the
complete pre-call state -- length, capacity, backing-array identity and item
values -- before reusing the list. The matrix covers inline-to-heap and
heap-to-heap append growth, insert growth, adjacent and distant `operator[]`
growth, assignment, `fit()`, construction and copy construction without
depending on host memory pressure or overcommit.

`callback-allocation.cpp` interposes both scalar and array allocation and
walks every allocation point observed during `SoCallbackList` assignment, up
to a defensive limit of 32. At each injected failure the original callback,
userdata and owned-adapter state must remain callable, and the destination is
then regrown across its inline-to-heap boundary to prove it remains reusable.

## Baseline before the refactoring

Against `origin/master` on 2026-09-13, the ASan/UBSan pass diagnoses 12 of 21
invalid-index modes and leaves nine observable violations silent. The MSan
pass diagnoses 14 modes and leaves seven silent. Together they diagnose every
mode except `get(-1)`, `*getArrayPtr(-1)`, `set(length)`, `remove(length)` and
`removeFast(length)`.

The two negative reads remain invisible because they stay within the
`SbPList` object; both expose the value of the private `itembuffer` member
immediately before `builtinbuffer`. The three past-end mutations stay within
reserved storage: `set(length)` writes outside the logical list, while both
removal functions silently discard the actual last item.

Six injected growth/allocation modes fail: both append growth boundaries,
growth reached through `insert()`, adjacent and distant `operator[]`, and
assignment. `grow()` updates `itembuffersize` before `new[]` succeeds, so
retries trust stale capacity and access beyond the actual allocation.
`fit()`, direct construction and copy construction preserve usable state under
the same deterministic allocation failure.

The callback matrix includes failures in either `SbPList` member and in the
owned-callback side table. The current memberwise assignment can leave stale
capacity, split function/userdata pairs, or metadata that describes a
different logical list. Invocation and subsequent growth expose those partial
states.

On `codex/diagnose-sbplist-indices`, all 21 invalid-index modes are rejected
before an array access. The `SoCallbackList` failures remain a separate,
higher-level issue and are not fixed by the index diagnostics.

## MemorySanitizer pass

ASan cannot diagnose initialized accesses that remain inside the object or
allocation, and it does not track uninitialized pointer values. A separate
Clang/MSan build exercises those observables:

```sh
cmake -S . -B /dev/shm/coin-build-sbplist-msan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS='-fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=memory' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=memory'
cmake --build /dev/shm/coin-build-sbplist-msan --parallel
testsuite/reproducers/sbplist-invalid-indices/run-msan.sh \
  /dev/shm/coin-build-sbplist-msan/lib
```

This configuration is valid only when the Clang MemorySanitizer runtime is
available. Diagnostics originating solely in uninstrumented system libraries
must be classified as `Unknown`, not attributed to `SbPList`.
`run-msan.sh` disables the allocation interposer because Clang's MSan runtime
provides its own strong array-allocation operators; the allocation-failure
matrix remains the responsibility of the ASan/UBSan pass.
