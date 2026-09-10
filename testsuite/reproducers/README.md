# Standalone regression programs

Call `SoDB::finish()` after releasing application-owned Coin objects when
checking leaks. Most programs use `CoinReproducerCleanup` from `CoinCleanup.h`,
declared immediately after `SoDB::init()`: automatic objects declared later
are destroyed first, and global library cleanup runs even on an early return.
Do not use this guard together with an explicit `SoDB::finish()` in the same
scope. Reference-counted graphs still need matching `ref()`/`unref()` calls;
paths stored inside their own graph must be detached to break ownership cycles.

For sanitizer validation, instrument the library as well as the driver. Use
`ASAN_OPTIONS=detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1`. Some scripts
support `CXXFLAGS`/`LDFLAGS`; for older scripts, set `CXX` to a compiler wrapper
that adds the sanitizer flags and the source/generated header directories.
A PASS printed before process exit does not override a LeakSanitizer failure.
An unavailable GL context or script engine is an inconclusive result.

The STL round-trip cases in `CoinTests` cover reader header reuse and writer
cleanup under LeakSanitizer. Keep `src/foreignfiles/steel.l` and its checked-in
generated implementation `steel.cpp` synchronized: the default build uses
the latter through `steel-wrapper.cpp`.
