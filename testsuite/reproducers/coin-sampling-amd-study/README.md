# Coin/OpenGL: isolating AMD projective sampling

This reproducer builds outside the main project. `coin-native-sampling` links
only Coin and OpenGL/EGL; `pure-egl-sampling` neither includes nor links Coin.
`glx-visual-probe` queries GLX configurations without Coin. None of these targets
depends on CoinRender, BGFX, wgpu or their intermediate formats.

Recorded validated build: Coin `master` at `01b360af32`, Linux, OpenGL 4.6
compatibility, AMD Renoir/Mesa 25.2.8 and NVIDIA RTX 3060 Laptop/610.57.04.
Actual paths and hashes are recorded in the original report and manifest. Dated
reports are preserved at their original paths in the companion archive; see the
[evidence guide](../../../docs/coin-render-evidence.md). Portable example,
starting from the checkout:

```sh
cmake -S . -B /path/to/build-coin -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_TESTS=OFF -DCOIN_BUILD_DOCUMENTATION=OFF
cmake --build /path/to/build-coin --target Coin -j 6
cmake -S testsuite/reproducers/coin-sampling-amd-study \
  -B /path/to/build-probe -G Ninja \
  -DCOIN_SOURCE_DIR=/path/to/coin -DCOIN_BINARY_DIR=/path/to/build-coin
cmake --build /path/to/build-probe -j 6
python3 testsuite/reproducers/coin-sampling-amd-study/run.py \
  --build /path/to/build-probe --output /path/to/permanent-results
python3 testsuite/reproducers/coin-sampling-amd-study/analyze.py /path/to/permanent-results
```

The runner selects two vendors on the original machine through GLVND and checks
the identity reported by each process. It needs the user's display/Xauthority.
Exit 77 does not count as physical execution; every case must return 0.
On NVIDIA, it uses `SoGLRenderAction` in the current EGL context: that master
revision fails before drawing during `SoOffscreenRenderer` GLX bootstrap because
single-buffer visuals are absent. AMD/GLX and NVIDIA/EGL are identified separately;
this does not count as a PASS for NVIDIA GLX bootstrap.

The cases use two quality values (0.5/0.8) and three offsets (zero/-0.001/+0.001).
Each process retains top-down 64×64 RGB, runtime identity, filters, matrix and
readbacks of all eight mip levels:

- `coin`: authored scene graph, with Coin image, UV and projective matrix.
- `fixed`: direct OpenGL upload/mips and S/T/Q coordinates.
- `implicit`: OpenGL shader using `texture`, without Coin capture.
- `explicit-lod`: LOD from the largest fine-derivative norm, with a native sampler.
- `fetch`: computed LOD and selection/bilinear reconstruction through `texelFetch`.
- `fetch-round256`: diagnostic 1/256-texel rounding before nearest fetch.
- `query-lod`: native query, with LOD encoded into two channels at 1/4096.
- `boundary-scan`: fixed mip 3 and exact binary coordinates per fragment.
- `boundary-fetch`: the same scan with floor/fetch selection. Both scans
  characterize nearest at quality 0.5 only; quality 0.8 is a linear control.

`analyze.py` uses an independent formula oracle without reading CoinRender IR or
sampler code. Center comparisons use the same 100 pixels and MAE≤1.5/maximum≤4
as the preceding gate. Scans check all 4,096 pixels with no tolerance. Precision
models characterize this machine; they are neither a global production contract
nor an OpenGL conformance test.
