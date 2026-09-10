# Ignored transformer picks (issue #174)

Run against a configured and built Coin tree:

```sh
sh testsuite/reproducers/transformerdragger-notfound-nocrash/run.sh /path/to/build/lib
```

`repro` calls the protected drag/finish methods in their initial NONE state.
`repro_surrogate` uses real ray picking to press, move and release on a cube
registered as the surrogate for an unrecognized part. `repro_metakey` also sends
Shift and Control events while the base dragger is active. Both public-API
scenarios run in CoinTests through the shared `TransformerIgnoredPickTest.h`.
No GL context is required. Failure to activate the dragger is a test failure,
not a successful skip. State, matrix, value-change callbacks and release of the
mouse grab are checked. Cleanup detaches the surrogate before releasing the
scene; standalone drivers call SoDB::finish after local objects are destroyed.

The tests verify observable ignored-drag behavior. They do not intercept the
non-virtual drag() method, detect removal of the modifier guard alone, or prove
initialization of private modifier fields. With drag(NONE) already a no-op,
removing that guard has no observable effect checked here. MSan can check
uninitialized reads only on paths that execute; the early-return guard prevents
those modifier-field reads in this scenario.

CXX, CXXFLAGS and LDFLAGS can instrument the standalone drivers. The linked Coin
library must also be instrumented. For example, against an ASan/UBSan build:

```sh
CXX=clang++ \
CXXFLAGS='-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
LDFLAGS='-fsanitize=address,undefined' \
ASAN_OPTIONS=detect_leaks=1 \
sh testsuite/reproducers/transformerdragger-notfound-nocrash/run.sh /path/to/build/lib
```

The shell driver uses Unix library paths. The shared scenarios are portable
CoinTests cases. Optional platform/rendering paths and real multi-selection GUI
interaction are not covered by these synthetic ray-picking scenarios.
