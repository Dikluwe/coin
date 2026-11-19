# Coin texture-coordinate PR evidence

[Published PR index: B01 upstream and 23 thematic drafts](PR-INDEX.md)

These images are actual frames from Coin's `SoOffscreenRenderer`, using the standard legacy OpenGL renderer under Xvfb/Mesa llvmpipe. No CoinRender or generated artwork is involved. Each comparison uses the exact parent and head recorded below, the same scene-generating executable linked against master, and only switches libCoin through LD_LIBRARY_PATH. Both libraries use Debug -O0 -g0, threads and legacy GL enabled; EGL disabled.

Every successful pair has byte-identical exported Inventor scenes and the same reported renderer. Original frames are 480 x 320 RGB; PNG conversion is lossless. Comparison panels display the complete original frames side by side with labels and picking values underneath. Rendered pixels are not retouched. A Debug abort before rendering is reported as an abort; no before frame is fabricated.

Image differences and picking differences are separate measurements. Some repaired CPU paths leave GL images unchanged. A visually unchanged scene is only a control for that scene; it does not prove universal absence of visual effects. Themes without a reliable graphical example use their numerical/state regression proof instead.

The accompanying regression tests were rerun using the candidate tests/executables against the exact parent and head library. Every baseline reproduces a failure; every candidate passes without skips. Results and scene metrics are in results.json. Per-theme logs and JUnit files record this additional focused run; earlier full-suite/ABI proofs are described in the PR bodies and were not reexecuted by this capture harness.

30 scene comparisons: nine change pixels, twenty have identical frames, and one baseline aborts before a frame. Sixteen comparisons change the recorded picking coordinates.

## Reproduction

Build the base and head from their pinned commits with the same options, then compile scenes.cpp against the baseline includes and libCoin:

```sh
g++ -std=c++11 -O0 -g0 -I "$tc_base_build/include" -I "$tc_base_src/include" \
  scenes.cpp -L "$tc_base_build/lib" -lCoin -lGL -o "$tc_ram/scenes"
LIBGL_ALWAYS_SOFTWARE=1 COIN_GLXGLUE_NO_PBUFFERS=1 \
COIN_GLX_PIXMAP_DIRECT_RENDERING=1 COIN_VBO=1 COIN_VBO_MIN_LIMIT=20 \
LD_LIBRARY_PATH="$tc_base_build/lib" \
xvfb-run -a "$tc_ram/scenes" transition-cube "$tc_ram/before"
# Repeat with LD_LIBRARY_PATH pointing to the head build and a different output prefix.
```

For the complete capture runner, prepare master in both base-src and candidate-src under COIN_CAPTURE_RAM, build base-build and candidate-build with the options above, compile the executable as scenes in that RAM directory, copy series.json there, and set COIN_CAPTURE_OUTPUT to an empty evidence directory before running capture-and-compare.py.

The executable writes the untouched RGB frame as PPM, the scene as Inventor text, and JSON context/picking observations. PNG files here are lossless conversions of those PPM frames. Use the comparison script included here to assemble panels and measure pixels. Builds/executables remain disposable in a task-exclusive RAM directory.

## Themes

| Theme | Base | Head | Parent tests failing | Candidate tests | Graphical pairs |
|---|---|---|---:|---:|---:|
| B01 | `31a4ea1bbd1974616e6e25a9f3c2adf0de8bb642` | `2e931d057c528f128f44e783cb528ff9359e92f7` | 5/6 | 6/6 | 4 |
| B02 | `2e931d057c528f128f44e783cb528ff9359e92f7` | `35e9c7fe9678338d9205535ce7000ff095e1429c` | 6/6 | 6/6 | 1 |
| B03 | `35e9c7fe9678338d9205535ce7000ff095e1429c` | `38a43bf13a441f3967664c2a313ba9a042d98895` | 6/8 | 8/8 | 0 |
| B04 | `38a43bf13a441f3967664c2a313ba9a042d98895` | `bb2471c198b467fd55308eab999b40f7345b5e6a` | 28/45 | 45/45 | 0 |
| B05 | `bb2471c198b467fd55308eab999b40f7345b5e6a` | `f87d02a4c4c3ced7671986c3d9e9b367f3bc9dd6` | 3/3 | 3/3 | 1 |
| B06 | `f87d02a4c4c3ced7671986c3d9e9b367f3bc9dd6` | `431c984eb76141df258f5a5e60fb564c881366b2` | 3/3 | 3/3 | 2 |
| M07.01 | `431c984eb76141df258f5a5e60fb564c881366b2` | `b78966a35941d18e546042ddbdacf9f94d78e16e` | 1/2 | 2/2 | 0 |
| M07.02 | `b78966a35941d18e546042ddbdacf9f94d78e16e` | `4506fa189519dd2f449fd78ffe7a960993de9e5d` | 1/2 | 2/2 | 3 |
| M07.03 | `4506fa189519dd2f449fd78ffe7a960993de9e5d` | `589a8455cb7783c669344eb06474043b3ad66355` | 1/2 | 2/2 | 2 |
| M07.04 | `589a8455cb7783c669344eb06474043b3ad66355` | `589e6acab20ff0b5d6d3903747958b22b15369a3` | 1/2 | 2/2 | 0 |
| M07.05 | `589e6acab20ff0b5d6d3903747958b22b15369a3` | `330953c66cc02a8d228fa1a04fbe5bdd86c781cf` | 1/2 | 2/2 | 0 |
| M07.06 | `330953c66cc02a8d228fa1a04fbe5bdd86c781cf` | `35ecd383eb854b3fefe2c625b7e73cbce290ad18` | 1/2 | 2/2 | 0 |
| M07.07 | `35ecd383eb854b3fefe2c625b7e73cbce290ad18` | `409d099087fd6937a067a24507324f3373af7297` | 1/2 | 2/2 | 0 |
| M07.08 | `409d099087fd6937a067a24507324f3373af7297` | `69504ba0e62bf3da22010ac20c3a5c3509fccb9d` | 1/2 | 2/2 | 0 |
| M08.01 | `69504ba0e62bf3da22010ac20c3a5c3509fccb9d` | `db4aa40b4f2db66bfb38a08007cccd225b9be12f` | 1/2 | 2/2 | 0 |
| M08.02 | `db4aa40b4f2db66bfb38a08007cccd225b9be12f` | `9baae78bb62a03698bffc9a0f21d2e2a2ee1d79c` | 1/2 | 2/2 | 3 |
| M08.03 | `9baae78bb62a03698bffc9a0f21d2e2a2ee1d79c` | `073b993d1c9a978a948fdf113b415cfdb219146b` | 1/2 | 2/2 | 0 |
| M08.04 | `073b993d1c9a978a948fdf113b415cfdb219146b` | `ddfb72c00ec3de277320ab3eb2c496b3855f984f` | 1/2 | 2/2 | 0 |
| M09 | `ddfb72c00ec3de277320ab3eb2c496b3855f984f` | `990fcdcc9ca332065dc2a2ee2731e0549f0da332` | 1/2 | 2/2 | 1 |
| LIN | `990fcdcc9ca332065dc2a2ee2731e0549f0da332` | `9bd641686e21f03717674d2e75871d85474162a6` | 1/3 | 3/3 | 1 |
| CUBE | `9bd641686e21f03717674d2e75871d85474162a6` | `7f0e3dd83fceb4a121a07d5261955702aba8742a` | 6/6 | 6/6 | 2 |
| SPH | `7f0e3dd83fceb4a121a07d5261955702aba8742a` | `37c190d8ef22acf99929c9a33c2467b36c73abe6` | 4/5 | 5/5 | 3 |
| ANA | `37c190d8ef22acf99929c9a33c2467b36c73abe6` | `0d266d37a8e77b876f36ec422d9e1d2947b041b4` | 4/6 | 6/6 | 4 |
| VRML | `0d266d37a8e77b876f36ec422d9e1d2947b041b4` | `b9de21a5b6d8fdef9455c19e65557740102c1f7f` | 4/5 | 5/5 | 3 |

## Interpretation

- B01: replacement of Object texgen by Cube/Cylinder/Sphere changes the image. Object to Default on unit 1 aborts in the Debug baseline before a frame; the candidate renders it.
- B02: the VBO registration error changes the rendered grid.
- B05/B06: the chosen GL frames are identical, while the measured CPU picking coordinates change. The fallback scenes intentionally use a white Texture2 followed by CubeMap to exercise the CPU callback before pure CubeMap picking is enabled by its later PR.
- M07.02/M07.03: extreme finite geometry exposes radial-square and bounding-box range loss. The sphere-control image uses a normal radius.
- M08.02: bbox-bundle-gradient shows the extreme-box result using a diagnostic gradient. The explicit-reference scene supplies analytic UVs independently; bundle-normal keeps a regular checkerboard and ordinary box. This is not a general loss of detail: the extreme visible patch spans an infinitesimal fraction of the huge mapping box.
- M09/LIN: frames stay identical; the JSON and panel report the corrected picking values.
- CubeMap/SPH/ANA/VRML: pure in-memory cube-map frames are compared alongside CPU hit coordinates for mesh and analytic shapes. The native default sphere is a historical control.
- Numerical mechanisms and invalid-unit guards without a demonstrated frame use the named regression tests. No unrelated screenshot is presented as proof of those mechanisms.

The 24 code deltas remain separate from this evidence branch: no generated images or logs are added to their code diffs. Local originals and the publication manifest are retained separately. No merge or automatic merge was requested.
