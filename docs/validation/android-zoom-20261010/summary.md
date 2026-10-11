# Android AVD: camera across window recreation

APK x86_64 from the current `codex/coin-render` checkout, SHA-256
`3f4da53d1598ae0494907aa08d0b5085b64656748ba56d68556f3a2b83d0d4ce`.
Built with `.github/scripts/build-coin-render-android-apk.py`, NDK 30,
API 37, wgpu/OpenGL ES. The AVD `Medium_Phone_API_37.0` used AMD/radeonsi.

The first `INIT_WINDOW` in PID 4033 framed the city at focal distance
1880.807. An `adb input swipe 540 1200 540 1620 650` ended at 2668.993.
Three real HOME/return cycles generated `TERM_WINDOW`/`INIT_WINDOW` and
kept the same PID and focal distance:

| Generation | Focal distance | Offscreen serial after recreation | Offscreen FNV64 |
| --- | ---: | ---: | --- |
| 1 | 1880.807 | 3 | `0bde526d7930ef60` |
| 2 | 2668.993 | 1511 | `3395b68286896e42` |
| 3 | 2668.993 | 2150 | `3395b68286896e42` |
| 4 | 2668.993 | 2225 | `3395b68286896e42` |

`camera_surface` and capture checkpoints came from `adb logcat -d -s
CoinRenderP23:I`. The new surface retained the zoomed scene in every
generation, with increasing submission serials. The logcat SHA-256 is
`7653493307b293422541ab13544209618fce642018b254594891047ae663c1b5`;
the full log and APK build receipts are in the local artifact directory
`/mnt/Laranja/Git/externos/coin-render-artifacts/p23-apk-20261007/apk-zoom-closure-x86_64`.
The initial emulator attempt selected NVIDIA and crashed during boot; the
AMD ICD restart succeeded. This control covers camera preservation in one
Activity and does not qualify two simultaneous windows or cross-thread RTT.
