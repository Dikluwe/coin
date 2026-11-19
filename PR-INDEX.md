# Published texture-coordinate PRs

B01 is open for review in coin3d/coin. The other 23 PRs are drafts in Dikluwe/coin, each targeting its immediate predecessor so that the diff remains thematic. They can be rebased and submitted upstream separately after prerequisites land. No merge or auto-merge has been performed.

All published heads/bases were checked against the locally reviewed commits; every remote PR contains exactly its expected files and commits. The separate evidence branch contains native Coin rendering comparisons and test logs. No CoinRender was used.

Fresh focused regression runs: all 24 parents reproduce a failure, all 24 candidates pass, no skips. Scene captures: 30 comparisons, nine pixel changes, twenty identical frames, one baseline Debug abort before a frame; sixteen comparisons change the measured picking coordinates. Images in each PR distinguish graphical changes from CPU picking changes. Themes without a representative render rely on their numerical/state test evidence.

| Order | Theme / PR | Repository / state | Base | Head |
|---:|---|---|---|---|
| 01 | [B01: Fix texture generator replacement on the selected GL unit](https://github.com/coin3d/coin/pull/873) | upstream / ready | `31a4ea1bbd1` | `2e931d057c5` |
| 02 | [B02: Register SoTextureCoordinate3 VBOs as texture coordinates](https://github.com/Dikluwe/coin/pull/8) | fork / draft | `2e931d057c5` | `35e9c7fe967` |
| 03 | [B03: Guard SoTextureCoordinate2 state changes on invalid GL units](https://github.com/Dikluwe/coin/pull/9) | fork / draft | `35e9c7fe967` | `38a43bf13a4` |
| 04 | [B04: Guard texture generators against invalid GL units](https://github.com/Dikluwe/coin/pull/10) | fork / draft | `38a43bf13a4` | `bb2471c198b` |
| 05 | [B05: Generate NormalMap CPU coordinates in eye space](https://github.com/Dikluwe/coin/pull/11) | fork / draft | `bb2471c198b` | `f87d02a4c4c` |
| 06 | [B06: Correct CPU Environment and ReflectionMap coordinates](https://github.com/Dikluwe/coin/pull/12) | fork / draft | `f87d02a4c4c` | `431c984eb76` |
| 07 | [M07.01: Recover NormalMap range loss under extreme affine transforms](https://github.com/Dikluwe/coin/pull/13) | fork / draft | `431c984eb76` | `b78966a3594` |
| 08 | [M07.02: Recover Sphere latitude after radial square range loss](https://github.com/Dikluwe/coin/pull/14) | fork / draft | `b78966a3594` | `4506fa18951` |
| 09 | [M07.03: Recover Cube and Cylinder coordinates for extreme finite boxes](https://github.com/Dikluwe/coin/pull/15) | fork / draft | `4506fa18951` | `589a8455cb7` |
| 10 | [M07.04: Recover lost Object and Plane texture dot products](https://github.com/Dikluwe/coin/pull/16) | fork / draft | `589a8455cb7` | `589e6acab20` |
| 11 | [M07.05: Recover collapsed point transforms for CPU reflection mapping](https://github.com/Dikluwe/coin/pull/17) | fork / draft | `589e6acab20` | `330953c66cc` |
| 12 | [M07.06: Recover collapsed normal cofactor sums for reflection mapping](https://github.com/Dikluwe/coin/pull/18) | fork / draft | `330953c66cc` | `35ecd383eb8` |
| 13 | [M07.07: Recover lost Cylinder and Sphere longitude near the seam](https://github.com/Dikluwe/coin/pull/19) | fork / draft | `35ecd383eb8` | `409d099087f` |
| 14 | [M07.08: Recover lost Sphere latitude near the south pole](https://github.com/Dikluwe/coin/pull/20) | fork / draft | `409d099087f` | `69504ba0e62` |
| 15 | [M08.01: Recover get2 quotients when a finite Q overflows its reciprocal](https://github.com/Dikluwe/coin/pull/21) | fork / draft | `69504ba0e62` | `db4aa40b4f2` |
| 16 | [M08.02: Recover default Bundle texture coordinates for extreme boxes](https://github.com/Dikluwe/coin/pull/22) | fork / draft | `db4aa40b4f2` | `9baae78bb62` |
| 17 | [M08.03: Recover Cache texture coordinates for extreme finite boxes](https://github.com/Dikluwe/coin/pull/23) | fork / draft | `9baae78bb62` | `073b993d1c9` |
| 18 | [M08.04: Recover range loss in picked texture coordinate transforms](https://github.com/Dikluwe/coin/pull/24) | fork / draft | `073b993d1c9` | `ddfb72c00ec` |
| 19 | [M09: Recover lost triangle texture components during picking](https://github.com/Dikluwe/coin/pull/25) | fork / draft | `ddfb72c00ec` | `990fcdcc9ca` |
| 20 | [LIN: Fix reversed texture interpolation weights in line picking](https://github.com/Dikluwe/coin/pull/26) | fork / draft | `990fcdcc9ca` | `9bd641686e2` |
| 21 | [CUBE: Enable CPU coordinate generation for in-memory cube maps](https://github.com/Dikluwe/coin/pull/27) | fork / draft | `9bd641686e2` | `7f0e3dd83fc` |
| 22 | [SPH: Honor texture functions in specialized SoSphere picking](https://github.com/Dikluwe/coin/pull/28) | fork / draft | `7f0e3dd83fc` | `37c190d8ef2` |
| 23 | [ANA: Honor texture functions in Cube Cone Cylinder and VRMLSphere picking](https://github.com/Dikluwe/coin/pull/29) | fork / draft | `37c190d8ef2` | `0d266d37a8e` |
| 24 | [VRML: Honor texture functions in VRML Box Cone and Cylinder picking](https://github.com/Dikluwe/coin/pull/30) | fork / draft | `0d266d37a8e` | `b9de21a5b6d` |

[Native Coin evidence and reproduction](README.md) · [Exact scene/test results](results.json) · [Verified remote PR metadata](remote-verification.json).

The original screenshot URLs are pinned to evidence commit `6f2d260d1b7ac436de82a0163ec9d1f7195084ec`; later index updates do not change those frames.
