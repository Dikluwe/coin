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

A passing matrix means every invalid operation was rejected by an assertion
or diagnosed by a sanitizer, and that an injected allocation failure left the
list reusable. It does **not** mean the API handles invalid indices safely. On
a normal build without `COIN_EXTRA_DEBUG`, these preconditions are unchecked
and executing the same operations is undefined behavior.

The allocation test overrides array allocation in the reproducer process and
throws `std::bad_alloc` exactly when appending the fifth item forces the first
heap growth. It then retries the append. This deterministically checks the
exception guarantee without depending on host memory pressure or overcommit.

## Current baseline

Against `origin/master` on 2026-09-13, eight invalid-index modes terminate
under ASan/UBSan. `get(-1)` and `*getArrayPtr(-1)` are not diagnosed because
they read within the `SbPList` object; both expose the value of the private
`itembuffer` member immediately before `builtinbuffer`.

The injected-allocation test also fails: `grow()` updates `itembuffersize`
before `new[]` succeeds. After `std::bad_alloc`, a retrying `append()` trusts
that stale capacity and ASan reports a stack-buffer-overflow past
`builtinbuffer`.
