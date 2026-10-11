# Android AVD: camera overlay on one million buildings

The `Medium_Phone_API_37.0` x86_64 AVD ran with temporary `-memory 8192`,
AMD host GPU, wgpu/OpenGL ES, 1080×2400 window, and the same
133,200,021-byte city asset. Each APK loaded 1,000,001 cubes, rendered
12,000,012 triangles, and produced the same first offscreen checksum
`067c1efbc25642eb`. No window readback was used.

The initial APK copied a view, projection and fog end for every one of the
million render states into the transactional camera undo. The updated APK
keeps the previous camera once and records changed fog ends in a bitset.
The per-state patch still updates the view and projection. Rollback restores
all fields, including mixed fog ends, and preserves the previous revision.

| Measurement during horizontal drag | Initial | Compact undo |
| --- | ---: | ---: |
| Camera patch frames in trace | 10 | 17 |
| Median `frame_plan_ms` | 130.3225 | 34.1221 |
| Median `backend_ms` | 63.7485 | 64.2687 |
| Lowest ten-frame FPS group near drag | 5.52 | 10.15 |
| Largest ten-frame render/present mean near drag | 181.199 ms | 98.456 ms |

The two `adb input swipe 300 1200 850 1200 1500` gestures were separate
short runs on the same AVD. Their event counts differed, so the FPS groups
indicate improvement but are not a controlled long-tail benchmark. The trace
isolates the change in plan time; backend cost remains around 64 ms per
camera patch. The baseline steady groups were near 34 FPS. Startup still
took about 34 s from renderer request to first capture. Before the patch,
Android reported 4,970,935 KiB total PSS after the million stabilized.
The original 2 GiB AVD setting, long-tail memory, and multiple resumes remain
unqualified.

The base and compact traces, logcat captures and meminfo are saved under
`/mnt/Laranja/Git/externos/coin-render-artifacts/p23-fps-20261007/million-study-20261010`.
SHA-256: base trace `ee48edd44afeeb4d21be4f310a0033556b651db3384ec81a0ec7b0d0955aa2ce`,
compact trace `7aaf10a1777103abf4115606bd6cd326328397814cb7bba7da6fdb8f97b82b1d`.
The compact APK SHA-256 is
`306866a755237f839d0439fd657dd2ccef0456102a6bee3fe117ad85b9c3de0c`.
The C++ `CoinRenderFrameReuseCoreTest` covered 70 mixed fog ends across the
64-bit boundary and exact rollback. The NVIDIA/BGFX Vulkan camera gate kept
patch and full reconstruction at RGB 0, with CoinGL maximum RGB 1.
