# Qualification commands

Build directories were configured against the existing branch; selected cache
values are in build-configuration.json. Native/Rust dependency pins were unchanged.

```
cmake --build /tmp/coin-retained-recording -j3 --target CoinRenderRetainedFeaturesTest CoinRenderRetainedCityTest CoinRenderRetainedIncrementalTest CoinRenderRetainedConnectorTest CoinRenderCompositionTest CoinRenderDepthContractTest
cmake --build /tmp/coin-retained-wgpu -j3 --target CoinRenderRetainedFeaturesTest CoinRenderRetainedCityTest CoinRenderRetainedIncrementalTest CoinRenderRetainedConnectorTest CoinRenderCompositionTest CoinRenderDepthContractTest
cmake --build /tmp/coin-retained-bgfx -j3 --target CoinRenderRetainedFeaturesTest CoinRenderRetainedCityTest CoinRenderRetainedIncrementalTest CoinRenderRetainedConnectorTest CoinRenderCompositionTest CoinRenderDepthContractTest CoinBgfxCoreTest CoinBgfxInstancingTest
cmake --build /tmp/coin-retained-android-wgpu -j3 --target CoinRenderRetainedFeaturesTest CoinRenderRetainedCityTest CoinRenderRetainedIncrementalTest CoinRenderRetainedConnectorTest coin_render_android_smoke
ctest --test-dir /tmp/coin-retained-recording --output-on-failure -R '^(CoinRenderRetained.*Test|CoinRenderCompositionTest|CoinRenderDepthContractTest)$'
ctest --test-dir /tmp/coin-retained-wgpu --output-on-failure -R '^(CoinRenderRetained.*Test|CoinRenderCompositionTest|CoinRenderDepthContractTest)$'
ctest --test-dir /tmp/coin-retained-bgfx --output-on-failure -R '^(CoinRenderRetained.*Test|CoinRenderCompositionTest|CoinRenderDepthContractTest|CoinBgfxCoreTest|CoinBgfxInstancingTest)$'
/tmp/coin-retained-wgpu/bin/CoinRenderRetainedFeaturesTest
```

The wgpu JUnit output truncates successful stdout at 1 KiB. The direct feature
log, recording/BGFX LastTest logs and Android logs preserve the complete fixture
list. Separate existing-path logs include the initial failure, GLX-policy retry
and explicitly enabled camera control; regression-status.json does not count the
failing projective CoinGL oracle as passing.

The saved Android runner contains all push/execute commands. The measured
runners contain the launch, timing/readback, dumpsys and camera checks. All six
processes are included in million/results.json, including the first current
process that paged. APK signing material stays outside this repository.
