# Mesa Renoir: an external, isolated workaround

This directory belongs to the **study**, not the CoinRender backend. The patch
applies to official Mesa 25.2.8 source, SHA256
`097842f3e49d996868b38688db87b006f7d4541e93ce86d2f341d8b3e7be7c93`.
It neither modifies Coin nor installs libraries into the system.

The NIR transformation is restricted to Renoir, a normalized non-array 2D
sampler, no comparison, float32, nearest/mip-linear minification, linear
magnification, no anisotropy, zero sampler bias and zero minLOD. Unsupported
offsets, explicit gradients and instruction sources retain the original path.
The sampler remains bound: magnification uses the native sample; minification
computes LOD from the original UVs and centers separately in each mip, including
NPOT. Hardware still decodes the formats.

The prototype computes the native sample before the LOD-dependent branch to
preserve implicit derivatives. Minification can therefore execute **three
samples**: one native and two reconstructed. Performance, other chips and general
applications are not qualified; this is not a driver ready for distribution.
Radeonsi uses ACO for the fragment stage; RADV also receives the pass.

`COIN_MESA_MIXED_FILTER_STUDY` is enabled by **presence**. Remove the variable to
disable it; the value `0` also enables it. The guard is absent from the shader
cache key, so launchers disable caching in both A/B controls. Environment
variables and libraries are confined to the child process.

## Reproduction

Extract the verified source into a permanent directory and apply:

```sh
patch -d "$mesa_source" -p1 -i "$study_repo/testsuite/reproducers/portable-sampling-study/mesa/mesa-25.2.8-renoir-mixed-filter.patch"
```

Use a private prefix, Meson 1.4.2 or compatible, Mako, PyYAML and the development
dependencies recorded in the evidence. The exact commands, dependencies,
libraries and hashes for the original PC are preserved at
`docs/validation/failure-closure-20261008/mesa-build` in the companion archive;
see the [evidence guide](../../../../docs/coin-render-evidence.md).
The ACO variant uses `-Dllvm=disabled -Damd-use-llvm=false`. The LLVM variant
uses LLVM 18.1.3, `-Dllvm=enabled -Damd-use-llvm=true`; the launcher can retain
LLVM for the vertex shader and ACO for the fragment shader using
`--gl-compiler llvm-vs-aco-ps`.

Other build options: Release, Gallium radeonsi, Vulkan amd, platform x11, GLX dri,
EGL enabled, GBM/VA/VDPAU/build-tests disabled and empty video-codecs. Before
installation, check that **all** destinations in the Meson install plan belong
to the private prefix. No command in this laboratory should target `/usr`,
`/usr/local` or the system driver installation.

```sh
python3 "$study_repo/testsuite/reproducers/portable-sampling-study/mesa/with_mesa.py" --prefix "$mesa_prefix" -- "$program"
python3 "$study_repo/testsuite/reproducers/portable-sampling-study/mesa/with_mesa.py" --prefix "$mesa_prefix" --disabled -- "$program"
python3 "$study_repo/testsuite/reproducers/portable-sampling-study/mesa/run_cases.py" --artifacts "$study_artifacts" --mesa-prefix "$mesa_prefix" --name qualification --profiles all6 --cases projective,sampling,advanced,procedural,rtt,deep,direct,viewport,large,devices,stress,instancing,shadow-eight,shadow-viewport,shadow-alpha,curved,styles
```

`study_artifacts` must contain the wgpu/BGFX builds from the study branch. GPU
tests run sequentially, require the CoinGL reference and preserve the original
gates. Exit 77/SKIP is not a pass. Programs inapplicable to a backend
(LargeBindings/MultiDevice on BGFX, wgpu-specific instancing) are not launched.
NVIDIA uses its installed libraries as a counter-control.

`../sampler_state_probe.cpp` builds with `-lEGL -lOpenGL` and does not link Coin.
It is diagnostic: it accepts the native-mode divergence and requires the
independent controls. To close failures, use the strict integrated tests and the
28-result ledger; exit 0 from the isolated probe does not prove native parity.

Primary sources:
[Mesa 25.2.8](https://docs.mesa3d.org/relnotes/25.2.8.html) and
[Mesa's explanation of TRUNC_COORD and mixed filters](https://cgit.freedesktop.org/mesa/mesa/commit/?id=6fac2889317e80b601ac63f7e46047bf8893d597).

The LLVM variant was a rejected counter-control: the same CoinGL edge failed
with LLVM, ACO, the installed driver and the earlier frozen runtime. The main
campaign uses the ACO prefix. `--cases portable-styles` runs the CPU/GPU contract
without the optional CoinGL comparison; it is a separate cohort. `styles` still
requires CoinGL, preserves FAIL results and does not change tolerances.
