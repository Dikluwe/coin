# Texture-coordinate PR review order — corrected

Active texture series: **23 themes / 52 commits**, composition `91be1a447e32f0fa93eef22b96db174ea8260182`.

The duplicate [fork LIN #26](https://github.com/Dikluwe/coin/pull/26) is closed and removed from this stack. [Upstream #871](https://github.com/coin3d/coin/pull/871), including its #864 dependency, is the single segment-attribute correction. Its code/head is unchanged. CUBE now targets M09 directly; the four later themes were replayed and revalidated. The earlier B01–M09 heads are unchanged.

[Native Coin proof of the double inversion and its removal](dedup-20261119/README.md). The standalone series passes 241/241 complete and 52/52 CPU tests. A disposable combined profile with #871/#864 passes 250/250 complete and 9/9 segment/normal tests; its three historical oracle adaptations are disclosed in the linked proof. No CoinRender, no skipped tests.

B01 is open in the upstream; the remaining 22 active themes are draft reviews in the fork, each against its immediate predecessor. No merge or auto-merge has been performed.

| Order | Theme / PR | State | Base | Head |
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
| 20 | [CUBE: Enable CPU coordinate generation for in-memory cube maps](https://github.com/Dikluwe/coin/pull/27) | fork / draft | `990fcdcc9ca` | `cd0611402f6` |
| 21 | [SPH: Honor texture functions in specialized SoSphere picking](https://github.com/Dikluwe/coin/pull/28) | fork / draft | `cd0611402f6` | `f4a09055de5` |
| 22 | [ANA: Honor texture functions in Cube Cone Cylinder and VRMLSphere picking](https://github.com/Dikluwe/coin/pull/29) | fork / draft | `f4a09055de5` | `4a1c115202a` |
| 23 | [VRML: Honor texture functions in VRML Box Cone and Cylinder picking](https://github.com/Dikluwe/coin/pull/30) | fork / draft | `4a1c115202a` | `91be1a447e3` |

The initial 24-theme captures remain archived in the original results.json and images, with their original SHA labels. They are historical evidence, not captures of replayed heads. The deduplication proof is in dedup-20261119/validation.json and its original frames.
